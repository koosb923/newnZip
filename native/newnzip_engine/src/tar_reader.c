#include "tar_reader.h"

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

static uint64_t tar_parse_octal(const char *field, size_t size) {
    uint64_t value = 0;
    size_t index = 0;
    while (index < size && (field[index] == ' ' || field[index] == '\0')) {
        index += 1;
    }
    for (; index < size; index++) {
        if (field[index] < '0' || field[index] > '7') {
            break;
        }
        value = (value << 3u) + (uint64_t) (field[index] - '0');
    }
    return value;
}

static bool tar_header_is_zero(const TarHeader *header) {
    for (size_t index = 0; index < sizeof(header->bytes); index++) {
        if (header->bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static void tar_header_path(const TarHeader *header, char *buffer, size_t buffer_size) {
    char name[101] = {0};
    char prefix[156] = {0};
    memcpy(name, header->bytes, 100u);
    memcpy(prefix, header->bytes + 345u, 155u);
    if (prefix[0] != '\0') {
        snprintf(buffer, buffer_size, "%s/%s", prefix, name);
    } else {
        snprintf(buffer, buffer_size, "%s", name);
    }
}

static void tar_skip_padding(FILE *input, uint64_t size) {
    uint64_t padding = size % TAR_BLOCK_SIZE;
    if (padding == 0) {
        return;
    }
    long skip = (long) (TAR_BLOCK_SIZE - padding);
    if (fseek(input, skip, SEEK_CUR) != 0) {
        fail_errno("tar 패딩을 건너뛰지 못했습니다");
    }
}

static void tar_extract_file(FILE *input, const char *target_path, uint64_t size, size_t chunk_size) {
    FILE *output = fopen(target_path, "wb");
    if (!output) {
        fail_errno(target_path);
    }

    unsigned char *buffer = malloc(chunk_size);
    if (!buffer) {
        fclose(output);
        fail("메모리가 부족합니다");
    }

    uint64_t remaining = size;
    while (remaining > 0) {
        size_t want = remaining < chunk_size ? (size_t) remaining : chunk_size;
        size_t read_size = fread(buffer, 1, want, input);
        if (read_size != want) {
            free(buffer);
            fclose(output);
            fail("tar 파일 데이터를 읽지 못했습니다");
        }
        if (fwrite(buffer, 1, read_size, output) != read_size) {
            free(buffer);
            fclose(output);
            fail_errno(target_path);
        }
        remaining -= read_size;
    }

    free(buffer);
    fclose(output);
    tar_skip_padding(input, size);
}

static void tar_extract_file_archive(const char *archive_path, const char *display_path, const char *destination, const RuntimeOptions *options) {
    FILE *input = fopen(archive_path, "rb");
    if (!input) {
        fail_errno(archive_path);
    }

    ProgressState progress;
    progress_state_init(&progress, 1);
    progress.total = 0;

    while (1) {
        TarHeader header;
        size_t read_size = fread(header.bytes, 1, sizeof(header.bytes), input);
        if (read_size == 0) {
            break;
        }
        if (read_size != sizeof(header.bytes)) {
            fclose(input);
            fail("tar 헤더를 끝까지 읽지 못했습니다");
        }
        if (tar_header_is_zero(&header)) {
            TarHeader next_header;
            size_t next_size = fread(next_header.bytes, 1, sizeof(next_header.bytes), input);
            if (next_size == sizeof(next_header.bytes) && tar_header_is_zero(&next_header)) {
                break;
            }
            if (next_size > 0 && fseek(input, -(long) next_size, SEEK_CUR) != 0) {
                fclose(input);
                fail_errno("tar 종료 블록을 되감지 못했습니다");
            }
            continue;
        }

        char relative_path[PATH_MAX];
        tar_header_path(&header, relative_path, sizeof(relative_path));
        if (relative_path[0] == '\0') {
            fclose(input);
            fail("tar 엔트리 경로가 비어 있습니다");
        }

        char *target_path = join_path(destination, relative_path);
        char typeflag = header.bytes[156u];
        uint64_t size = tar_parse_octal(header.bytes + 124u, 12u);

        if (typeflag == '5') {
            ensure_parent_directories(target_path);
            if (mkdir(target_path, 0755) != 0 && errno != EEXIST) {
                free(target_path);
                fclose(input);
                fail_errno(relative_path);
            }
        } else if (typeflag == '2') {
            char link_target[101] = {0};
            memcpy(link_target, header.bytes + 157u, 100u);
            ensure_parent_directories(target_path);
            if (unlink(target_path) != 0 && errno != ENOENT) {
                free(target_path);
                fclose(input);
                fail_errno(target_path);
            }
            if (symlink(link_target, target_path) != 0) {
                free(target_path);
                fclose(input);
                fail_errno(target_path);
            }
        } else {
            ensure_parent_directories(target_path);
            tar_extract_file(input, target_path, size, options->chunk_size);
        }

        progress.total += 1;
        progress_step(&progress, "extract", relative_path);
        free(target_path);

        if (typeflag == '5' || typeflag == '2') {
            tar_skip_padding(input, size);
        }
    }

    progress_state_destroy(&progress);
    fclose(input);
    printf("해제 완료: %s -> %s (tar native)\n", display_path, destination);
}

void command_extract_tar(const char *archive_path, const char *destination, const RuntimeOptions *options) {
    tar_extract_file_archive(archive_path, archive_path, destination, options);
}

static void tar_extract_from_temp(
    const char *archive_path,
    const char *destination,
    const RuntimeOptions *options,
    const char *temporary_prefix,
    void (*decompressor)(const char *, const char *, size_t)
) {
    char *temporary_tar = create_temp_path(temporary_prefix);
    decompressor(archive_path, temporary_tar, options->chunk_size);
    tar_extract_file_archive(temporary_tar, archive_path, destination, options);
    remove_file_if_exists(temporary_tar);
    free(temporary_tar);
}

static void tar_gzip_to_file(const char *archive_path, const char *output_path, size_t chunk_size) {
    gzFile input = gzopen(archive_path, "rb");
    if (!input) {
        fail("tar.gz 입력을 열지 못했습니다");
    }
    FILE *output = fopen(output_path, "wb");
    if (!output) {
        gzclose(input);
        fail_errno(output_path);
    }
    unsigned char *buffer = malloc(chunk_size);
    if (!buffer) {
        fclose(output);
        gzclose(input);
        fail("메모리가 부족합니다");
    }
    while (1) {
        int read_size = gzread(input, buffer, (unsigned int) chunk_size);
        if (read_size < 0) {
            free(buffer);
            fclose(output);
            gzclose(input);
            fail("tar.gz 데이터를 읽지 못했습니다");
        }
        if (read_size == 0) {
            break;
        }
        if (fwrite(buffer, 1, (size_t) read_size, output) != (size_t) read_size) {
            free(buffer);
            fclose(output);
            gzclose(input);
            fail_errno(output_path);
        }
    }
    free(buffer);
    fclose(output);
    gzclose(input);
}

#if NEWNZIP_HAS_BZIP2
static void tar_bzip2_to_file(const char *archive_path, const char *output_path, size_t chunk_size) {
    FILE *input_file = fopen(archive_path, "rb");
    if (!input_file) {
        fail_errno(archive_path);
    }
    FILE *output = fopen(output_path, "wb");
    if (!output) {
        fclose(input_file);
        fail_errno(output_path);
    }
    int bz_error = BZ_OK;
    BZFILE *input = BZ2_bzReadOpen(&bz_error, input_file, 0, 0, NULL, 0);
    if (bz_error != BZ_OK || !input) {
        fclose(output);
        fclose(input_file);
        fail("tar.bz2 입력을 열지 못했습니다");
    }
    unsigned char *buffer = malloc(chunk_size);
    if (!buffer) {
        BZ2_bzReadClose(&bz_error, input);
        fclose(output);
        fclose(input_file);
        fail("메모리가 부족합니다");
    }
    while (1) {
        int read_size = BZ2_bzRead(&bz_error, input, buffer, (int) chunk_size);
        if (bz_error != BZ_OK && bz_error != BZ_STREAM_END) {
            free(buffer);
            BZ2_bzReadClose(&bz_error, input);
            fclose(output);
            fclose(input_file);
            fail("tar.bz2 데이터를 읽지 못했습니다");
        }
        if (read_size > 0 && fwrite(buffer, 1, (size_t) read_size, output) != (size_t) read_size) {
            free(buffer);
            BZ2_bzReadClose(&bz_error, input);
            fclose(output);
            fclose(input_file);
            fail_errno(output_path);
        }
        if (bz_error == BZ_STREAM_END) {
            break;
        }
    }
    free(buffer);
    BZ2_bzReadClose(&bz_error, input);
    fclose(output);
    fclose(input_file);
}
#else
static void tar_bzip2_to_file(const char *archive_path, const char *output_path, size_t chunk_size) {
    (void) archive_path;
    (void) output_path;
    (void) chunk_size;
    fail("현재 빌드에는 tar.bz2 네이티브 코덱이 포함되지 않았습니다");
}
#endif

#if NEWNZIP_HAS_LZMA
static void tar_xz_to_file(const char *archive_path, const char *output_path, size_t chunk_size) {
    FILE *input = fopen(archive_path, "rb");
    if (!input) {
        fail_errno(archive_path);
    }
    FILE *output = fopen(output_path, "wb");
    if (!output) {
        fclose(input);
        fail_errno(output_path);
    }
    lzma_stream stream = LZMA_STREAM_INIT;
    if (lzma_stream_decoder(&stream, UINT64_MAX, 0) != LZMA_OK) {
        fclose(output);
        fclose(input);
        fail("tar.xz 디코더를 초기화하지 못했습니다");
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
                fail_errno(archive_path);
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
            fail_errno(output_path);
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
            fail("tar.xz 데이터를 읽지 못했습니다");
        }
    } while (1);
    free(in_buffer);
    free(out_buffer);
    lzma_end(&stream);
    fclose(output);
    fclose(input);
}
#else
static void tar_xz_to_file(const char *archive_path, const char *output_path, size_t chunk_size) {
    (void) archive_path;
    (void) output_path;
    (void) chunk_size;
    fail("현재 빌드에는 tar.xz 네이티브 코덱이 포함되지 않았습니다");
}
#endif

#if NEWNZIP_HAS_ZSTD
static void tar_zstd_to_file(const char *archive_path, const char *output_path, size_t chunk_size) {
    FILE *input = fopen(archive_path, "rb");
    if (!input) {
        fail_errno(archive_path);
    }
    FILE *output = fopen(output_path, "wb");
    if (!output) {
        fclose(input);
        fail_errno(output_path);
    }
    ZSTD_DCtx *context = ZSTD_createDCtx();
    if (!context) {
        fclose(output);
        fclose(input);
        fail("tar.zstd 컨텍스트를 만들지 못했습니다");
    }
    size_t in_capacity = chunk_size;
    size_t out_capacity = ZSTD_DStreamOutSize();
    unsigned char *in_buffer = malloc(in_capacity);
    unsigned char *out_buffer = malloc(out_capacity);
    if (!in_buffer || !out_buffer) {
        free(in_buffer);
        free(out_buffer);
        ZSTD_freeDCtx(context);
        fclose(output);
        fclose(input);
        fail("메모리가 부족합니다");
    }
    while (1) {
        size_t read_size = fread(in_buffer, 1, in_capacity, input);
        if (ferror(input)) {
            free(in_buffer);
            free(out_buffer);
            ZSTD_freeDCtx(context);
            fclose(output);
            fclose(input);
            fail_errno(archive_path);
        }
        if (read_size == 0 && feof(input)) {
            break;
        }
        ZSTD_inBuffer in_view = { in_buffer, read_size, 0 };
        while (in_view.pos < in_view.size) {
            ZSTD_outBuffer out_view = { out_buffer, out_capacity, 0 };
            size_t remaining = ZSTD_decompressStream(context, &out_view, &in_view);
            if (ZSTD_isError(remaining)) {
                free(in_buffer);
                free(out_buffer);
                ZSTD_freeDCtx(context);
                fclose(output);
                fclose(input);
                fail("tar.zstd 데이터를 읽지 못했습니다");
            }
            if (out_view.pos > 0 && fwrite(out_buffer, 1, out_view.pos, output) != out_view.pos) {
                free(in_buffer);
                free(out_buffer);
                ZSTD_freeDCtx(context);
                fclose(output);
                fclose(input);
                fail_errno(output_path);
            }
        }
    }
    free(in_buffer);
    free(out_buffer);
    ZSTD_freeDCtx(context);
    fclose(output);
    fclose(input);
}
#else
static void tar_zstd_to_file(const char *archive_path, const char *output_path, size_t chunk_size) {
    (void) archive_path;
    (void) output_path;
    (void) chunk_size;
    fail("현재 빌드에는 tar.zstd 네이티브 코덱이 포함되지 않았습니다");
}
#endif

void command_extract_targz(const char *archive_path, const char *destination, const RuntimeOptions *options) {
    tar_extract_from_temp(archive_path, destination, options, "newnzip-targz-extract-", tar_gzip_to_file);
}

void command_extract_tarbz2(const char *archive_path, const char *destination, const RuntimeOptions *options) {
    tar_extract_from_temp(archive_path, destination, options, "newnzip-tarbz2-extract-", tar_bzip2_to_file);
}

void command_extract_tarxz(const char *archive_path, const char *destination, const RuntimeOptions *options) {
    tar_extract_from_temp(archive_path, destination, options, "newnzip-tarxz-extract-", tar_xz_to_file);
}

void command_extract_tarzstd(const char *archive_path, const char *destination, const RuntimeOptions *options) {
    tar_extract_from_temp(archive_path, destination, options, "newnzip-tarzstd-extract-", tar_zstd_to_file);
}
