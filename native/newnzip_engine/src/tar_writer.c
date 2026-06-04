#include "tar_writer.h"

#if NEWNZIP_HAS_BZIP2
#include <bzlib.h>
#endif

#if NEWNZIP_HAS_LZMA
#include <lzma.h>
#endif

#if NEWNZIP_HAS_ZSTD
#include <zstd.h>
#endif

#define TAR_BLOCK_SIZE 512u

typedef struct {
    char bytes[TAR_BLOCK_SIZE];
} TarHeader;

static uint64_t tar_padding(uint64_t size) {
    uint64_t remainder = size % TAR_BLOCK_SIZE;
    return remainder == 0 ? 0 : (TAR_BLOCK_SIZE - remainder);
}

static void tar_write_all(FILE *output, const void *data, size_t size, const char *path) {
    if (size > 0 && fwrite(data, 1, size, output) != size) {
        fail_errno(path);
    }
}

static void tar_copy_file(FILE *output, const char *path, uint64_t *written_size, size_t chunk_size) {
    FILE *input = fopen(path, "rb");
    if (!input) {
        fail_errno(path);
    }

    unsigned char *buffer = malloc(chunk_size);
    if (!buffer) {
        fclose(input);
        fail("메모리가 부족합니다");
    }

    uint64_t total = 0;
    while (1) {
        size_t read_size = fread(buffer, 1, chunk_size, input);
        if (read_size > 0) {
            tar_write_all(output, buffer, read_size, path);
            total += read_size;
        }
        if (read_size < chunk_size) {
            if (ferror(input)) {
                free(buffer);
                fclose(input);
                fail_errno(path);
            }
            break;
        }
    }

    free(buffer);
    fclose(input);
    *written_size = total;
}

static void tar_zero_fill(char *buffer, size_t size) {
    memset(buffer, 0, size);
}

static void tar_format_octal(char *field, size_t size, uint64_t value) {
    if (size == 0) {
        return;
    }
    memset(field, '0', size);
    field[size - 1] = '\0';
    if (size == 1) {
        return;
    }
    char temp[32];
    snprintf(temp, sizeof(temp), "%llo", (unsigned long long) value);
    size_t length = strlen(temp);
    if (length > size - 1) {
        fail("tar 헤더 숫자 필드가 너무 큽니다");
    }
    memcpy(field + (size - 1 - length), temp, length);
}

static char tar_typeflag_for_source(const SourceEntry *source) {
    if (source->is_symlink) {
        return '2';
    }
    if (S_ISDIR(source->mode)) {
        return '5';
    }
    return '0';
}

static char *tar_entry_name(const SourceEntry *source) {
    char *name = normalize_archive_name(source->archive_name);
    if (S_ISDIR(source->mode)) {
        size_t length = strlen(name);
        if (length == 0 || name[length - 1] != '/') {
            char *with_slash = malloc(length + 2);
            if (!with_slash) {
                free(name);
                fail("메모리가 부족합니다");
            }
            memcpy(with_slash, name, length);
            with_slash[length] = '/';
            with_slash[length + 1] = '\0';
            free(name);
            return with_slash;
        }
    }
    return name;
}

static void tar_split_name(const char *name, char *name_field, size_t name_size, char *prefix_field, size_t prefix_size) {
    size_t length = strlen(name);
    if (length <= name_size) {
        memcpy(name_field, name, length);
        return;
    }

    const char *split = NULL;
    const char *cursor = name;
    while ((cursor = strchr(cursor, '/')) != NULL) {
        size_t prefix_length = (size_t) (cursor - name);
        size_t tail_length = length - prefix_length - 1;
        if (prefix_length <= prefix_size && tail_length <= name_size) {
            split = cursor;
        }
        cursor += 1;
    }

    if (!split) {
        fail("tar 경로가 너무 깁니다. 긴 경로 pax 헤더는 아직 구현되지 않았습니다");
    }

    size_t prefix_length = (size_t) (split - name);
    size_t tail_length = strlen(split + 1);
    memcpy(prefix_field, name, prefix_length);
    memcpy(name_field, split + 1, tail_length);
}

static void tar_fill_header(TarHeader *header, const SourceEntry *source, const char *entry_name) {
    tar_zero_fill(header->bytes, sizeof(header->bytes));

    tar_split_name(entry_name, header->bytes, 100u, header->bytes + 345u, 155u);
    tar_format_octal(header->bytes + 100u, 8u, source->mode & 07777u);
    tar_format_octal(header->bytes + 108u, 8u, 0u);
    tar_format_octal(header->bytes + 116u, 8u, 0u);
    tar_format_octal(header->bytes + 124u, 12u, source->is_symlink || S_ISDIR(source->mode) ? 0u : source->size);
    tar_format_octal(header->bytes + 136u, 12u, (uint64_t) time(NULL));
    memset(header->bytes + 148u, ' ', 8u);
    header->bytes[156u] = tar_typeflag_for_source(source);

    if (source->is_symlink && source->link_target) {
        size_t target_length = strlen(source->link_target);
        if (target_length > 100u) {
            fail("tar 심볼릭 링크 대상이 너무 깁니다");
        }
        memcpy(header->bytes + 157u, source->link_target, target_length);
    }

    memcpy(header->bytes + 257u, "ustar", 5u);
    memcpy(header->bytes + 263u, "00", 2u);

    unsigned int checksum = 0;
    for (size_t index = 0; index < sizeof(header->bytes); index++) {
        checksum += (unsigned char) header->bytes[index];
    }
    tar_format_octal(header->bytes + 148u, 8u, checksum);
}

static void tar_write_zero_blocks(FILE *output, size_t count, const char *path) {
    char zero[TAR_BLOCK_SIZE] = {0};
    for (size_t index = 0; index < count; index++) {
        tar_write_all(output, zero, sizeof(zero), path);
    }
}

static void tar_create_file(const char *archive_path, const SourceEntry *sources, size_t source_count, const RuntimeOptions *options) {
    FILE *output = fopen(archive_path, "wb");
    if (!output) {
        fail_errno(archive_path);
    }

    ProgressState progress;
    progress_state_init(&progress, source_count);

    for (size_t index = 0; index < source_count; index++) {
        const SourceEntry *source = &sources[index];
        char *entry_name = tar_entry_name(source);
        TarHeader header;
        tar_fill_header(&header, source, entry_name);
        tar_write_all(output, header.bytes, sizeof(header.bytes), archive_path);

        if (!source->is_symlink && !S_ISDIR(source->mode)) {
            uint64_t written = 0;
            tar_copy_file(output, source->path, &written, options->chunk_size);
            if (written != source->size) {
                free(entry_name);
                fclose(output);
                fail("tar 파일 크기가 예상과 다릅니다");
            }
            uint64_t padding = tar_padding(written);
            if (padding > 0) {
                char zero[TAR_BLOCK_SIZE] = {0};
                tar_write_all(output, zero, (size_t) padding, archive_path);
            }
        }

        progress_step(&progress, "compress", entry_name);
        free(entry_name);
    }

    tar_write_zero_blocks(output, 2u, archive_path);
    progress_state_destroy(&progress);
    fclose(output);
}

static void tar_gzip_file(const char *source_path, const char *destination_path, size_t chunk_size) {
    FILE *input = fopen(source_path, "rb");
    if (!input) {
        fail_errno(source_path);
    }
    gzFile output = gzopen(destination_path, "wb");
    if (!output) {
        fclose(input);
        fail("tar.gz 출력을 만들지 못했습니다");
    }

    unsigned char *buffer = malloc(chunk_size);
    if (!buffer) {
        gzclose(output);
        fclose(input);
        fail("메모리가 부족합니다");
    }

    while (1) {
        size_t read_size = fread(buffer, 1, chunk_size, input);
        if (read_size > 0) {
            if (gzwrite(output, buffer, (unsigned int) read_size) != (int) read_size) {
                free(buffer);
                gzclose(output);
                fclose(input);
                fail("tar.gz 데이터를 기록하지 못했습니다");
            }
        }
        if (read_size < chunk_size) {
            if (ferror(input)) {
                free(buffer);
                gzclose(output);
                fclose(input);
                fail_errno(source_path);
            }
            break;
        }
    }

    free(buffer);
    gzclose(output);
    fclose(input);
}

#if NEWNZIP_HAS_BZIP2
static void tar_bzip2_file(const char *source_path, const char *destination_path, size_t chunk_size) {
    FILE *input = fopen(source_path, "rb");
    if (!input) {
        fail_errno(source_path);
    }
    FILE *output_file = fopen(destination_path, "wb");
    if (!output_file) {
        fclose(input);
        fail_errno(destination_path);
    }

    int bz_error = BZ_OK;
    BZFILE *output = BZ2_bzWriteOpen(&bz_error, output_file, 9, 0, 30);
    if (bz_error != BZ_OK || !output) {
        fclose(output_file);
        fclose(input);
        fail("tar.bz2 출력을 만들지 못했습니다");
    }

    unsigned char *buffer = malloc(chunk_size);
    if (!buffer) {
        BZ2_bzWriteClose(&bz_error, output, 1, NULL, NULL);
        fclose(output_file);
        fclose(input);
        fail("메모리가 부족합니다");
    }

    while (1) {
        size_t read_size = fread(buffer, 1, chunk_size, input);
        if (read_size > 0) {
            BZ2_bzWrite(&bz_error, output, buffer, (int) read_size);
            if (bz_error != BZ_OK) {
                free(buffer);
                BZ2_bzWriteClose(&bz_error, output, 1, NULL, NULL);
                fclose(output_file);
                fclose(input);
                fail("tar.bz2 데이터를 기록하지 못했습니다");
            }
        }
        if (read_size < chunk_size) {
            if (ferror(input)) {
                free(buffer);
                BZ2_bzWriteClose(&bz_error, output, 1, NULL, NULL);
                fclose(output_file);
                fclose(input);
                fail_errno(source_path);
            }
            break;
        }
    }

    free(buffer);
    BZ2_bzWriteClose(&bz_error, output, 0, NULL, NULL);
    fclose(output_file);
    fclose(input);
}
#else
static void tar_bzip2_file(const char *source_path, const char *destination_path, size_t chunk_size) {
    (void) source_path;
    (void) destination_path;
    (void) chunk_size;
    fail("현재 빌드에는 tar.bz2 네이티브 코덱이 포함되지 않았습니다");
}
#endif

#if NEWNZIP_HAS_LZMA
static void tar_xz_file(const char *source_path, const char *destination_path, size_t chunk_size) {
    FILE *input = fopen(source_path, "rb");
    if (!input) {
        fail_errno(source_path);
    }
    FILE *output = fopen(destination_path, "wb");
    if (!output) {
        fclose(input);
        fail_errno(destination_path);
    }

    lzma_stream stream = LZMA_STREAM_INIT;
    if (lzma_easy_encoder(&stream, 6, LZMA_CHECK_CRC64) != LZMA_OK) {
        fclose(output);
        fclose(input);
        fail("tar.xz 인코더를 초기화하지 못했습니다");
    }

    unsigned char *in_buffer = malloc(chunk_size);
    unsigned char *out_buffer = malloc(chunk_size);
    if (!in_buffer || !out_buffer) {
        free(in_buffer);
        free(out_buffer);
        lzma_end(&stream);
        fclose(output);
        fclose(input);
        fail("메모리가 부족합니다");
    }

    lzma_action action = LZMA_RUN;
    stream.avail_in = 0;
    stream.next_in = NULL;
    do {
        if (stream.avail_in == 0 && action != LZMA_FINISH) {
            size_t read_size = fread(in_buffer, 1, chunk_size, input);
            if (ferror(input)) {
                free(in_buffer);
                free(out_buffer);
                lzma_end(&stream);
                fclose(output);
                fclose(input);
                fail_errno(source_path);
            }
            stream.next_in = in_buffer;
            stream.avail_in = read_size;
            action = feof(input) ? LZMA_FINISH : LZMA_RUN;
        }

        stream.next_out = out_buffer;
        stream.avail_out = chunk_size;
        lzma_ret ret = lzma_code(&stream, action);
        size_t produced = chunk_size - stream.avail_out;
        if (produced > 0 && fwrite(out_buffer, 1, produced, output) != produced) {
            free(in_buffer);
            free(out_buffer);
            lzma_end(&stream);
            fclose(output);
            fclose(input);
            fail_errno(destination_path);
        }
        if (ret == LZMA_STREAM_END) {
            break;
        }
        if (ret != LZMA_OK) {
            free(in_buffer);
            free(out_buffer);
            lzma_end(&stream);
            fclose(output);
            fclose(input);
            fail("tar.xz 데이터를 기록하지 못했습니다");
        }
    } while (1);

    free(in_buffer);
    free(out_buffer);
    lzma_end(&stream);
    fclose(output);
    fclose(input);
}
#else
static void tar_xz_file(const char *source_path, const char *destination_path, size_t chunk_size) {
    (void) source_path;
    (void) destination_path;
    (void) chunk_size;
    fail("현재 빌드에는 tar.xz 네이티브 코덱이 포함되지 않았습니다");
}
#endif

#if NEWNZIP_HAS_ZSTD
static void tar_zstd_file(const char *source_path, const char *destination_path, size_t chunk_size) {
    FILE *input = fopen(source_path, "rb");
    if (!input) {
        fail_errno(source_path);
    }
    FILE *output = fopen(destination_path, "wb");
    if (!output) {
        fclose(input);
        fail_errno(destination_path);
    }

    ZSTD_CCtx *context = ZSTD_createCCtx();
    if (!context) {
        fclose(output);
        fclose(input);
        fail("tar.zstd 컨텍스트를 만들지 못했습니다");
    }
    ZSTD_CCtx_setParameter(context, ZSTD_c_compressionLevel, 6);

    size_t in_capacity = chunk_size;
    size_t out_capacity = ZSTD_CStreamOutSize();
    unsigned char *in_buffer = malloc(in_capacity);
    unsigned char *out_buffer = malloc(out_capacity);
    if (!in_buffer || !out_buffer) {
        free(in_buffer);
        free(out_buffer);
        ZSTD_freeCCtx(context);
        fclose(output);
        fclose(input);
        fail("메모리가 부족합니다");
    }

    while (1) {
        size_t read_size = fread(in_buffer, 1, in_capacity, input);
        if (ferror(input)) {
            free(in_buffer);
            free(out_buffer);
            ZSTD_freeCCtx(context);
            fclose(output);
            fclose(input);
            fail_errno(source_path);
        }
        ZSTD_inBuffer in_view = { in_buffer, read_size, 0 };
        ZSTD_EndDirective mode = feof(input) ? ZSTD_e_end : ZSTD_e_continue;
        while (in_view.pos < in_view.size || mode == ZSTD_e_end) {
            ZSTD_outBuffer out_view = { out_buffer, out_capacity, 0 };
            size_t remaining = ZSTD_compressStream2(context, &out_view, &in_view, mode);
            if (ZSTD_isError(remaining)) {
                free(in_buffer);
                free(out_buffer);
                ZSTD_freeCCtx(context);
                fclose(output);
                fclose(input);
                fail("tar.zstd 데이터를 기록하지 못했습니다");
            }
            if (out_view.pos > 0 && fwrite(out_buffer, 1, out_view.pos, output) != out_view.pos) {
                free(in_buffer);
                free(out_buffer);
                ZSTD_freeCCtx(context);
                fclose(output);
                fclose(input);
                fail_errno(destination_path);
            }
            if (mode == ZSTD_e_end && remaining == 0) {
                free(in_buffer);
                free(out_buffer);
                ZSTD_freeCCtx(context);
                fclose(output);
                fclose(input);
                return;
            }
            if (mode == ZSTD_e_continue && in_view.pos == in_view.size) {
                break;
            }
        }
    }
}
#else
static void tar_zstd_file(const char *source_path, const char *destination_path, size_t chunk_size) {
    (void) source_path;
    (void) destination_path;
    (void) chunk_size;
    fail("현재 빌드에는 tar.zstd 네이티브 코덱이 포함되지 않았습니다");
}
#endif

static void tar_source_list_push(
    SourceEntry **sources,
    size_t *source_count,
    size_t *source_capacity,
    const char *path,
    const char *archive_name,
    const char *link_target,
    uint64_t size,
    mode_t mode,
    bool is_symlink
) {
    if (*source_count == *source_capacity) {
        size_t new_capacity = *source_capacity == 0 ? 16 : (*source_capacity * 2);
        SourceEntry *new_items = realloc(*sources, new_capacity * sizeof(SourceEntry));
        if (!new_items) {
            fail("메모리가 부족합니다");
        }
        *sources = new_items;
        *source_capacity = new_capacity;
    }
    (*sources)[*source_count].path = duplicate_string(path);
    (*sources)[*source_count].archive_name = duplicate_string(archive_name);
    (*sources)[*source_count].link_target = link_target ? duplicate_string(link_target) : NULL;
    (*sources)[*source_count].size = size;
    (*sources)[*source_count].mode = mode;
    (*sources)[*source_count].is_symlink = is_symlink;
    *source_count += 1;
}

static void tar_collect_source_recursive(
    const char *path,
    const char *archive_name,
    SourceEntry **sources,
    size_t *source_count,
    size_t *source_capacity
) {
    if (should_exclude_archive_path(path, archive_name)) {
        return;
    }

    struct stat item_stat;
    if (lstat(path, &item_stat) != 0) {
        fail_errno(path);
    }

    if (S_ISLNK(item_stat.st_mode)) {
        size_t buffer_size = item_stat.st_size > 0 ? (size_t) item_stat.st_size + 1u : PATH_MAX;
        char *target = malloc(buffer_size);
        if (!target) {
            fail("메모리가 부족합니다");
        }
        ssize_t target_length = readlink(path, target, buffer_size - 1u);
        if (target_length < 0) {
            free(target);
            fail_errno(path);
        }
        target[target_length] = '\0';
        tar_source_list_push(sources, source_count, source_capacity, path, archive_name, target, (uint64_t) target_length, item_stat.st_mode, true);
        free(target);
        return;
    }

    if (S_ISREG(item_stat.st_mode)) {
        tar_source_list_push(sources, source_count, source_capacity, path, archive_name, NULL, (uint64_t) item_stat.st_size, item_stat.st_mode, false);
        return;
    }

    if (!S_ISDIR(item_stat.st_mode)) {
        return;
    }

    tar_source_list_push(sources, source_count, source_capacity, path, archive_name, NULL, 0, item_stat.st_mode, false);

    DIR *dir = opendir(path);
    if (!dir) {
        fail_errno(path);
    }

    struct dirent *entry = NULL;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        char *child_path = join_path(path, entry->d_name);
        char *child_name = join_path(archive_name, entry->d_name);
        tar_collect_source_recursive(child_path, child_name, sources, source_count, source_capacity);
        free(child_path);
        free(child_name);
    }
    closedir(dir);
}

static void tar_collect_sources_from_argv(int argc, char **argv, SourceEntry **sources, size_t *source_count, size_t *source_capacity) {
    for (int index = 3; index < argc; index++) {
        const char *input_path = argv[index];
        const char *name = strrchr(input_path, '/');
        name = name ? name + 1 : input_path;
        tar_collect_source_recursive(input_path, name, sources, source_count, source_capacity);
    }
}

static void tar_prepare_sources_or_fail(int argc, char **argv, SourceEntry **sources, size_t *source_count, size_t *source_capacity) {
    tar_collect_sources_from_argv(argc, argv, sources, source_count, source_capacity);
    if (*source_count == 0) {
        free_sources(*sources, *source_count);
        fail("압축할 일반 파일을 찾지 못했습니다");
    }
}

static void tar_create_with_codec(
    const char *temporary_prefix,
    const char *output_path,
    const SourceEntry *sources,
    size_t source_count,
    const RuntimeOptions *options,
    void (*compressor)(const char *, const char *, size_t),
    const char *label
) {
    char *temporary_tar = create_temp_path(temporary_prefix);
    RuntimeOptions temp_options = *options;
    tar_create_file(temporary_tar, sources, source_count, &temp_options);
    compressor(temporary_tar, output_path, options->chunk_size);
    remove_file_if_exists(temporary_tar);
    free(temporary_tar);
    printf("생성 완료: %s (%zu개 항목, %s native)\n", output_path, source_count, label);
}

void command_create_tar(int argc, char **argv, const RuntimeOptions *options) {
    if (argc < 4) {
        fail("사용법: newnzip-engine create output.tar <파일-또는-폴더>...");
    }

    SourceEntry *sources = NULL;
    size_t source_count = 0;
    size_t source_capacity = 0;
    tar_prepare_sources_or_fail(argc, argv, &sources, &source_count, &source_capacity);

    tar_create_file(argv[2], sources, source_count, options);
    printf("생성 완료: %s (%zu개 항목, tar native)\n", argv[2], source_count);
    free_sources(sources, source_count);
}

void command_create_targz(int argc, char **argv, const RuntimeOptions *options) {
    if (argc < 4) {
        fail("사용법: newnzip-engine create output.tar.gz <파일-또는-폴더>...");
    }

    SourceEntry *sources = NULL;
    size_t source_count = 0;
    size_t source_capacity = 0;
    tar_prepare_sources_or_fail(argc, argv, &sources, &source_count, &source_capacity);
    tar_create_with_codec("newnzip-targz-", argv[2], sources, source_count, options, tar_gzip_file, "tar.gz");
    free_sources(sources, source_count);
}

void command_create_tarbz2(int argc, char **argv, const RuntimeOptions *options) {
    if (argc < 4) {
        fail("사용법: newnzip-engine create output.tar.bz2 <파일-또는-폴더>...");
    }

    SourceEntry *sources = NULL;
    size_t source_count = 0;
    size_t source_capacity = 0;
    tar_prepare_sources_or_fail(argc, argv, &sources, &source_count, &source_capacity);
    tar_create_with_codec("newnzip-tarbz2-", argv[2], sources, source_count, options, tar_bzip2_file, "tar.bz2");
    free_sources(sources, source_count);
}

void command_create_tarxz(int argc, char **argv, const RuntimeOptions *options) {
    if (argc < 4) {
        fail("사용법: newnzip-engine create output.tar.xz <파일-또는-폴더>...");
    }

    SourceEntry *sources = NULL;
    size_t source_count = 0;
    size_t source_capacity = 0;
    tar_prepare_sources_or_fail(argc, argv, &sources, &source_count, &source_capacity);
    tar_create_with_codec("newnzip-tarxz-", argv[2], sources, source_count, options, tar_xz_file, "tar.xz");
    free_sources(sources, source_count);
}

void command_create_tarzstd(int argc, char **argv, const RuntimeOptions *options) {
    if (argc < 4) {
        fail("사용법: newnzip-engine create output.tar.zstd <파일-또는-폴더>...");
    }

    SourceEntry *sources = NULL;
    size_t source_count = 0;
    size_t source_capacity = 0;
    tar_prepare_sources_or_fail(argc, argv, &sources, &source_count, &source_capacity);
    tar_create_with_codec("newnzip-tarzstd-", argv[2], sources, source_count, options, tar_zstd_file, "tar.zstd");
    free_sources(sources, source_count);
}
