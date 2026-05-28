#include "common.h"
#include "zip_writer.h"

typedef struct {
    unsigned char *data;
    size_t size;
    size_t capacity;
} MemoryBuffer;

typedef struct {
    SourceEntry *sources;
    size_t source_count;
    size_t next_index;
    pthread_mutex_t mutex;
    int compression_level;
    size_t small_file_threshold;
    size_t chunk_size;
    const char *zip_method;
    const char *performance_mode;
} CompressionQueue;

typedef struct {
    CentralEntry entry;
    char *temp_path;
    unsigned char *memory_payload;
    uint64_t memory_payload_size;
    bool ready;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
} CompressionResult;

typedef struct {
    CompressionQueue *queue;
    CompressionResult *results;
    ProgressState *progress;
} CompressionWorkerContext;

typedef struct {
    unsigned char *read_buffer;
    unsigned char *write_buffer;
} CompressionScratch;

static void memory_buffer_reserve(MemoryBuffer *buffer, size_t needed) {
    if (buffer->capacity >= needed) {
        return;
    }
    size_t next_capacity = buffer->capacity == 0 ? 65536u : buffer->capacity;
    while (next_capacity < needed) {
        next_capacity *= 2u;
    }
    unsigned char *next = realloc(buffer->data, next_capacity);
    if (!next) {
        fail("메모리가 부족합니다");
    }
    buffer->data = next;
    buffer->capacity = next_capacity;
}

static void memory_buffer_append(MemoryBuffer *buffer, const unsigned char *data, size_t size) {
    memory_buffer_reserve(buffer, buffer->size + size);
    memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
}

static int resolved_compression_level(const RuntimeOptions *options, uint64_t size) {
    if (strcmp(options->performance_mode, "low-memory") == 0) {
        return 1;
    }
    if (size < 64 * 1024u) {
        return 1;
    }
    if (size < 1024 * 1024u) {
        return options->compression_level > 3 ? 3 : options->compression_level;
    }
    return options->compression_level;
}

static bool zip_method_is(const CompressionQueue *queue, const char *method) {
    return strcasecmp(queue->zip_method, method) == 0;
}

static CentralEntry store_source_to_payload(
    const SourceEntry *source,
    const CompressionQueue *queue,
    CompressionScratch *scratch,
    char **temp_path,
    unsigned char **memory_payload,
    uint64_t *memory_payload_size
) {
    struct stat file_stat;
    if (lstat(source->path, &file_stat) != 0) {
        fail_errno(source->path);
    }

    char *normalized_name = normalize_archive_name(source->archive_name);

    CentralEntry entry;
    entry.name = normalized_name;
    entry.crc32 = crc32(0L, Z_NULL, 0);
    entry.compressed_size = 0;
    entry.uncompressed_size = 0;
    entry.local_header_offset = 0;
    entry.external_attributes = ((uint32_t) (source->mode & 0xffffu)) << 16;
    entry.method = ZIP_METHOD_STORE;
    entry.mod_time = dos_time_value(file_stat.st_mtime);
    entry.mod_date = dos_date_value(file_stat.st_mtime);

    if (source->is_symlink) {
        size_t target_length = strlen(source->link_target);
        unsigned char *payload = malloc(target_length == 0 ? 1 : target_length);
        if (!payload) {
            fail("메모리가 부족합니다");
        }
        if (target_length > 0) {
            memcpy(payload, source->link_target, target_length);
        }
        entry.crc32 = crc32(entry.crc32, payload, (uInt) target_length);
        entry.compressed_size = (uint64_t) target_length;
        entry.uncompressed_size = (uint64_t) target_length;
        *memory_payload = payload;
        *memory_payload_size = (uint64_t) target_length;
        return entry;
    }

    FILE *input = fopen(source->path, "rb");
    if (!input) {
        fail_errno(source->path);
    }

    FILE *temp_output = NULL;
    MemoryBuffer memory_output = {0};
    bool use_memory_payload = source->size <= queue->small_file_threshold;
    if (!use_memory_payload) {
        *temp_path = create_temp_path("newnzip-store-");
        temp_output = fopen(*temp_path, "wb");
        if (!temp_output) {
            fclose(input);
            fail_errno(*temp_path);
        }
    }

    while (1) {
        size_t read_size = fread(scratch->read_buffer, 1, queue->chunk_size, input);
        if (read_size > 0) {
            entry.crc32 = crc32(entry.crc32, scratch->read_buffer, (uInt) read_size);
            entry.compressed_size += (uint64_t) read_size;
            entry.uncompressed_size += (uint64_t) read_size;
            if (use_memory_payload) {
                memory_buffer_append(&memory_output, scratch->read_buffer, read_size);
            } else if (fwrite(scratch->read_buffer, 1, read_size, temp_output) != read_size) {
                fclose(temp_output);
                fclose(input);
                fail_errno("임시 저장 데이터를 쓰지 못했습니다");
            }
        }
        if (read_size < queue->chunk_size) {
            if (ferror(input)) {
                if (temp_output) {
                    fclose(temp_output);
                }
                free(memory_output.data);
                fclose(input);
                fail_errno("원본 파일을 읽지 못했습니다");
            }
            break;
        }
    }

    if (temp_output) {
        fclose(temp_output);
    } else {
        if (!memory_output.data) {
            memory_output.data = malloc(1);
            if (!memory_output.data) {
                fclose(input);
                fail("메모리가 부족합니다");
            }
        }
        *memory_payload = memory_output.data;
        *memory_payload_size = (uint64_t) memory_output.size;
    }
    fclose(input);
    return entry;
}

static CentralEntry compress_source_to_payload(
    const SourceEntry *source,
    const CompressionQueue *queue,
    CompressionScratch *scratch,
    char **temp_path,
    unsigned char **memory_payload,
    uint64_t *memory_payload_size
) {
    struct stat file_stat;
    if (lstat(source->path, &file_stat) != 0) {
        fail_errno(source->path);
    }

    if (source->is_symlink || zip_method_is(queue, "store")) {
        return store_source_to_payload(source, queue, scratch, temp_path, memory_payload, memory_payload_size);
    }

    char *normalized_name = normalize_archive_name(source->archive_name);

    CentralEntry entry;
    entry.name = normalized_name;
    entry.crc32 = crc32(0L, Z_NULL, 0);
    entry.compressed_size = 0;
    entry.uncompressed_size = 0;
    entry.local_header_offset = 0;
    entry.external_attributes = ((uint32_t) (source->mode & 0xffffu)) << 16;
    entry.method = ZIP_METHOD_DEFLATE;
    entry.mod_time = dos_time_value(file_stat.st_mtime);
    entry.mod_date = dos_date_value(file_stat.st_mtime);

    FILE *input = fopen(source->path, "rb");
    if (!input) {
        fail_errno(source->path);
    }

    FILE *temp_output = NULL;
    MemoryBuffer memory_output = {0};
    bool use_memory_payload = source->size <= queue->small_file_threshold;
    if (!use_memory_payload) {
        *temp_path = create_temp_path("newnzip-deflate-");
        temp_output = fopen(*temp_path, "wb");
        if (!temp_output) {
            fclose(input);
            fail_errno(*temp_path);
        }
    }

    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    RuntimeOptions tuning_view = {
        .detected_cpu_count = 0,
        .thread_count = 0,
        .compression_level = queue->compression_level,
        .chunk_size = queue->chunk_size,
        .small_file_threshold = queue->small_file_threshold,
        .split_size = 0,
        .archive_format = "zip",
        .zip_method = queue->zip_method,
        .performance_mode = queue->performance_mode
    };
    int compression_level = resolved_compression_level(&tuning_view, source->size);
    if (deflateInit2(&stream, compression_level, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        if (temp_output) {
            fclose(temp_output);
        }
        fclose(input);
        fail("압축기를 초기화하지 못했습니다");
    }

    int flush = Z_NO_FLUSH;

    do {
        stream.avail_in = (uInt) fread(scratch->read_buffer, 1, queue->chunk_size, input);
        if (ferror(input)) {
            deflateEnd(&stream);
            if (temp_output) {
                fclose(temp_output);
            }
            free(memory_output.data);
            fclose(input);
            fail_errno("원본 파일을 읽지 못했습니다");
        }
        entry.uncompressed_size += (uint64_t) stream.avail_in;
        entry.crc32 = crc32(entry.crc32, scratch->read_buffer, stream.avail_in);
        flush = feof(input) ? Z_FINISH : Z_NO_FLUSH;
        stream.next_in = scratch->read_buffer;

        do {
            stream.avail_out = (uInt) queue->chunk_size;
            stream.next_out = scratch->write_buffer;
            int result = deflate(&stream, flush);
            if (result == Z_STREAM_ERROR) {
                deflateEnd(&stream);
                if (temp_output) {
                    fclose(temp_output);
                }
                free(memory_output.data);
                fclose(input);
                fail("압축 데이터 생성에 실패했습니다");
            }
            size_t have = queue->chunk_size - stream.avail_out;
            if (have > 0) {
                if (use_memory_payload) {
                    memory_buffer_append(&memory_output, scratch->write_buffer, have);
                } else if (fwrite(scratch->write_buffer, 1, have, temp_output) != have) {
                    deflateEnd(&stream);
                    fclose(temp_output);
                    fclose(input);
                    fail_errno("임시 압축 데이터를 쓰지 못했습니다");
                }
            }
            entry.compressed_size += (uint64_t) have;
        } while (stream.avail_out == 0);
    } while (flush != Z_FINISH);

    if (deflateEnd(&stream) != Z_OK) {
        if (temp_output) {
            fclose(temp_output);
        }
        free(memory_output.data);
        fclose(input);
        fail("압축기를 정상 종료하지 못했습니다");
    }
    if (temp_output) {
        fclose(temp_output);
    } else {
        *memory_payload = memory_output.data;
        *memory_payload_size = (uint64_t) memory_output.size;
    }
    fclose(input);

    if (zip_method_is(queue, "auto") && entry.compressed_size >= entry.uncompressed_size) {
        free(entry.name);
        free(*memory_payload);
        *memory_payload = NULL;
        *memory_payload_size = 0;
        remove_file_if_exists(*temp_path);
        free(*temp_path);
        *temp_path = NULL;
        return store_source_to_payload(source, queue, scratch, temp_path, memory_payload, memory_payload_size);
    }

    return entry;
}

static size_t next_source_index(CompressionQueue *queue) {
    size_t index = SIZE_MAX;
    pthread_mutex_lock(&queue->mutex);
    if (queue->next_index < queue->source_count) {
        index = queue->next_index;
        queue->next_index += 1;
    }
    pthread_mutex_unlock(&queue->mutex);
    return index;
}

static void *compression_worker_main(void *raw_context) {
    CompressionWorkerContext *context = raw_context;
    CompressionScratch scratch;
    scratch.read_buffer = malloc(context->queue->chunk_size);
    scratch.write_buffer = malloc(context->queue->chunk_size);
    if (!scratch.read_buffer || !scratch.write_buffer) {
        fail("메모리가 부족합니다");
    }

    while (1) {
        size_t index = next_source_index(context->queue);
        if (index == SIZE_MAX) {
            break;
        }

        char *temp_path = NULL;
        unsigned char *memory_payload = NULL;
        uint64_t memory_payload_size = 0;
        CentralEntry entry = compress_source_to_payload(
            &context->queue->sources[index],
            context->queue,
            &scratch,
            &temp_path,
            &memory_payload,
            &memory_payload_size
        );

        pthread_mutex_lock(&context->results[index].mutex);
        context->results[index].entry = entry;
        context->results[index].temp_path = temp_path;
        context->results[index].memory_payload = memory_payload;
        context->results[index].memory_payload_size = memory_payload_size;
        context->results[index].ready = true;
        pthread_cond_signal(&context->results[index].condition);
        pthread_mutex_unlock(&context->results[index].mutex);
        progress_step(context->progress, "compress", entry.name);
    }
    free(scratch.read_buffer);
    free(scratch.write_buffer);
    return NULL;
}

static CompressionResult *compress_sources_parallel(SourceEntry *sources, size_t source_count, const RuntimeOptions *options, ProgressState *progress) {
    CompressionQueue queue;
    queue.sources = sources;
    queue.source_count = source_count;
    queue.next_index = 0;
    queue.compression_level = options->compression_level;
    queue.small_file_threshold = options->small_file_threshold;
    queue.chunk_size = options->chunk_size;
    queue.zip_method = options->zip_method;
    queue.performance_mode = options->performance_mode;
    pthread_mutex_init(&queue.mutex, NULL);

    CompressionResult *results = calloc(source_count, sizeof(CompressionResult));
    if (!results) {
        pthread_mutex_destroy(&queue.mutex);
        fail("메모리가 부족합니다");
    }

    int worker_count = options->thread_count;
    if ((size_t) worker_count > source_count) {
        worker_count = (int) source_count;
    }
    if (worker_count < 1) {
        worker_count = 1;
    }

    pthread_t *threads = calloc((size_t) worker_count, sizeof(pthread_t));
    if (!threads) {
        pthread_mutex_destroy(&queue.mutex);
        free(results);
        fail("메모리가 부족합니다");
    }

    CompressionWorkerContext context;
    context.queue = &queue;
    context.results = results;
    context.progress = progress;

    for (size_t i = 0; i < source_count; i++) {
        pthread_mutex_init(&results[i].mutex, NULL);
        pthread_cond_init(&results[i].condition, NULL);
    }

    for (int i = 0; i < worker_count; i++) {
        if (pthread_create(&threads[i], NULL, compression_worker_main, &context) != 0) {
            pthread_mutex_destroy(&queue.mutex);
            free(threads);
            free(results);
            fail("압축 작업 스레드를 만들지 못했습니다");
        }
    }

    for (int i = 0; i < worker_count; i++) {
        pthread_join(threads[i], NULL);
    }

    pthread_mutex_destroy(&queue.mutex);
    free(threads);
    return results;
}

static bool entry_needs_zip64(const CentralEntry *entry) {
    return entry->compressed_size > UINT32_MAX ||
           entry->uncompressed_size > UINT32_MAX ||
           entry->local_header_offset > UINT32_MAX;
}

static void write_zip64_size_extra(FILE *output, const CentralEntry *entry, bool include_offset) {
    uint16_t payload_size = include_offset ? 24 : 16;
    write_u16(output, ZIP64_EXTRA_FIELD_ID);
    write_u16(output, payload_size);
    write_u64(output, entry->uncompressed_size);
    write_u64(output, entry->compressed_size);
    if (include_offset) {
        write_u64(output, entry->local_header_offset);
    }
}

static void write_local_header(FILE *output, const CentralEntry *entry) {
    uint16_t name_length = (uint16_t) strlen(entry->name);
    bool use_zip64 = entry_needs_zip64(entry);
    write_u32(output, ZIP_LOCAL_FILE_HEADER);
    write_u16(output, use_zip64 ? ZIP64_VERSION : ZIP_VERSION);
    write_u16(output, ZIP_GENERAL_PURPOSE_UTF8);
    write_u16(output, entry->method);
    write_u16(output, entry->mod_time);
    write_u16(output, entry->mod_date);
    write_u32(output, entry->crc32);
    write_u32(output, use_zip64 ? UINT32_MAX : (uint32_t) entry->compressed_size);
    write_u32(output, use_zip64 ? UINT32_MAX : (uint32_t) entry->uncompressed_size);
    write_u16(output, name_length);
    write_u16(output, use_zip64 ? 20 : 0);
    if (fwrite(entry->name, 1, name_length, output) != name_length) {
        fail_errno("파일 이름을 기록하지 못했습니다");
    }
    if (use_zip64) {
        write_zip64_size_extra(output, entry, false);
    }
}

static void copy_file_contents(FILE *output, const char *temp_path, uint64_t *written_size, size_t chunk_size) {
    FILE *input = fopen(temp_path, "rb");
    if (!input) {
        fail_errno(temp_path);
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
            if (fwrite(buffer, 1, read_size, output) != read_size) {
                free(buffer);
                fclose(input);
                fail_errno("압축 결과를 아카이브에 쓰지 못했습니다");
            }
            total += (uint64_t) read_size;
        }
        if (read_size < chunk_size) {
            if (ferror(input)) {
                free(buffer);
                fclose(input);
                fail_errno("임시 압축 파일을 읽지 못했습니다");
            }
            break;
        }
    }
    free(buffer);
    fclose(input);
    *written_size = total;
}

static void copy_memory_contents(FILE *output, const unsigned char *payload, uint64_t payload_size, uint64_t *written_size) {
    if (payload_size > SIZE_MAX) {
        fail("메모리 압축 결과가 너무 큽니다");
    }
    size_t size = (size_t) payload_size;
    if (size > 0 && fwrite(payload, 1, size, output) != size) {
        fail_errno("메모리 압축 결과를 아카이브에 쓰지 못했습니다");
    }
    *written_size = payload_size;
}

static void split_file(const char *source_path, const char *destination_base, uint64_t split_size, size_t chunk_size) {
    FILE *input = fopen(source_path, "rb");
    if (!input) {
        fail_errno(source_path);
    }

    unsigned char *buffer = malloc(chunk_size);
    if (!buffer) {
        fclose(input);
        fail("메모리가 부족합니다");
    }

    int part = 1;
    while (1) {
        char part_path[PATH_MAX];
        snprintf(part_path, sizeof(part_path), "%s.%03d", destination_base, part);
        FILE *output = fopen(part_path, "wb");
        if (!output) {
            free(buffer);
            fclose(input);
            fail_errno(part_path);
        }

        uint64_t written = 0;
        bool wrote_any = false;
        while (written < split_size) {
            size_t want = chunk_size;
            uint64_t remaining = split_size - written;
            if (remaining < want) {
                want = (size_t) remaining;
            }
            size_t read_size = fread(buffer, 1, want, input);
            if (read_size > 0) {
                wrote_any = true;
                if (fwrite(buffer, 1, read_size, output) != read_size) {
                    fclose(output);
                    free(buffer);
                    fclose(input);
                    fail_errno(part_path);
                }
                written += read_size;
            }
            if (read_size < want) {
                if (ferror(input)) {
                    fclose(output);
                    free(buffer);
                    fclose(input);
                    fail_errno(source_path);
                }
                break;
            }
        }

        fclose(output);
        if (!wrote_any) {
            remove_file_if_exists(part_path);
            break;
        }
        part += 1;
        if (feof(input)) {
            break;
        }
    }

    free(buffer);
    fclose(input);
}

static void command_create_split(int argc, char **argv, const RuntimeOptions *options) {
    char *temp_path = create_temp_path("newnzip-split-source-");
    RuntimeOptions single_options = *options;
    single_options.split_size = 0;

    char **temp_argv = calloc((size_t) argc + 1, sizeof(char *));
    if (!temp_argv) {
        free(temp_path);
        fail("메모리가 부족합니다");
    }
    for (int i = 0; i < argc; i++) {
        temp_argv[i] = argv[i];
    }
    temp_argv[2] = temp_path;

    command_create(argc, temp_argv, &single_options);
    split_file(temp_path, argv[2], options->split_size, options->chunk_size);
    remove_file_if_exists(temp_path);
    free(temp_path);
    free(temp_argv);
    printf("분할 생성 완료: %s.001\n", argv[2]);
}

static void write_central_directory(FILE *output, const CentralList *entries) {
    long central_offset = ftell(output);
    if (central_offset < 0) {
        fail_errno("failed to determine central directory offset");
    }
    uint64_t central_offset64 = (uint64_t) central_offset;
    bool needs_zip64 = entries->count > UINT16_MAX || central_offset64 > UINT32_MAX;

    for (size_t i = 0; i < entries->count; i++) {
        const CentralEntry *entry = &entries->items[i];
        uint16_t name_length = (uint16_t) strlen(entry->name);
        bool entry_zip64 = entry_needs_zip64(entry);
        if (entry_zip64) {
            needs_zip64 = true;
        }
        write_u32(output, ZIP_CENTRAL_DIRECTORY_HEADER);
        write_u16(output, (uint16_t) ((3u << 8) | (entry_zip64 ? ZIP64_VERSION : ZIP_VERSION)));
        write_u16(output, entry_zip64 ? ZIP64_VERSION : ZIP_VERSION);
        write_u16(output, ZIP_GENERAL_PURPOSE_UTF8);
        write_u16(output, entry->method);
        write_u16(output, entry->mod_time);
        write_u16(output, entry->mod_date);
        write_u32(output, entry->crc32);
        write_u32(output, entry_zip64 ? UINT32_MAX : (uint32_t) entry->compressed_size);
        write_u32(output, entry_zip64 ? UINT32_MAX : (uint32_t) entry->uncompressed_size);
        write_u16(output, name_length);
        write_u16(output, entry_zip64 ? 28 : 0);
        write_u16(output, 0);
        write_u16(output, 0);
        write_u16(output, 0);
        write_u32(output, entry->external_attributes);
        write_u32(output, entry_zip64 ? UINT32_MAX : (uint32_t) entry->local_header_offset);
        if (fwrite(entry->name, 1, name_length, output) != name_length) {
            fail_errno("failed to write central directory entry");
        }
        if (entry_zip64) {
            write_zip64_size_extra(output, entry, true);
        }
    }

    long central_end = ftell(output);
    if (central_end < 0) {
        fail_errno("failed to determine central directory size");
    }
    uint64_t central_size64 = (uint64_t) (central_end - central_offset);
    if (central_size64 > UINT32_MAX) {
        needs_zip64 = true;
    }

    uint64_t zip64_eocd_offset = (uint64_t) central_end;
    if (needs_zip64) {
        write_u32(output, ZIP64_END_OF_CENTRAL_DIRECTORY);
        write_u64(output, 44);
        write_u16(output, (uint16_t) ((3u << 8) | ZIP64_VERSION));
        write_u16(output, ZIP64_VERSION);
        write_u32(output, 0);
        write_u32(output, 0);
        write_u64(output, (uint64_t) entries->count);
        write_u64(output, (uint64_t) entries->count);
        write_u64(output, central_size64);
        write_u64(output, central_offset64);

        write_u32(output, ZIP64_END_OF_CENTRAL_DIRECTORY_LOCATOR);
        write_u32(output, 0);
        write_u64(output, zip64_eocd_offset);
        write_u32(output, 1);
    }

    write_u32(output, ZIP_END_OF_CENTRAL_DIRECTORY);
    write_u16(output, 0);
    write_u16(output, 0);
    write_u16(output, entries->count > UINT16_MAX ? UINT16_MAX : (uint16_t) entries->count);
    write_u16(output, entries->count > UINT16_MAX ? UINT16_MAX : (uint16_t) entries->count);
    write_u32(output, central_size64 > UINT32_MAX ? UINT32_MAX : (uint32_t) central_size64);
    write_u32(output, central_offset64 > UINT32_MAX ? UINT32_MAX : (uint32_t) central_offset64);
    write_u16(output, 0);
}

void command_create(int argc, char **argv, const RuntimeOptions *options) {
    if (argc < 4) {
        fail("사용법: newnzip-engine create output.zip <파일-또는-폴더>...");
    }
    if (strcasecmp(options->zip_method, "deflate") != 0 &&
        strcasecmp(options->zip_method, "store") != 0 &&
        strcasecmp(options->zip_method, "auto") != 0) {
        fail("현재 ZIP 생성 방식은 deflate, store, auto만 지원합니다");
    }
    if (options->split_size > 0) {
        command_create_split(argc, argv, options);
        return;
    }
    const char *archive_path = argv[2];
    FILE *output = fopen(archive_path, "wb+");
    if (!output) {
        fail_errno(archive_path);
    }

    SourceEntry *sources = NULL;
    size_t source_count = 0;
    size_t source_capacity = 0;
    for (int i = 3; i < argc; i++) {
        const char *input_path = argv[i];
        const char *name = strrchr(input_path, '/');
        name = name ? name + 1 : input_path;
        collect_sources(input_path, name, &sources, &source_count, &source_capacity);
    }
    if (source_count == 0) {
        fclose(output);
        free_sources(sources, source_count);
        fail("압축할 일반 파일을 찾지 못했습니다");
    }

    sort_sources_by_size_desc(sources, source_count);
    RuntimeOptions tuned_options = *options;
    tune_runtime_for_source_count(&tuned_options, source_count, total_source_size(sources, source_count));

    ProgressState progress;
    progress_state_init(&progress, source_count);
    CompressionResult *results = compress_sources_parallel(sources, source_count, &tuned_options, &progress);
    CentralList entries = {0};
    for (size_t i = 0; i < source_count; i++) {
        pthread_mutex_lock(&results[i].mutex);
        while (!results[i].ready) {
            pthread_cond_wait(&results[i].condition, &results[i].mutex);
        }
        pthread_mutex_unlock(&results[i].mutex);

        long offset = ftell(output);
        if (offset < 0) {
            fail_errno("출력 오프셋을 계산하지 못했습니다");
        }
        results[i].entry.local_header_offset = (uint64_t) offset;
        write_local_header(output, &results[i].entry);

        uint64_t written_size = 0;
        if (results[i].temp_path) {
            copy_file_contents(output, results[i].temp_path, &written_size, tuned_options.chunk_size);
        } else {
            copy_memory_contents(output, results[i].memory_payload, results[i].memory_payload_size, &written_size);
        }
        if (written_size != results[i].entry.compressed_size) {
            fail("임시 압축 데이터 크기가 메타데이터와 다릅니다");
        }

        central_list_push(&entries, results[i].entry);
        remove_file_if_exists(results[i].temp_path);
        free(results[i].temp_path);
        free(results[i].memory_payload);
        pthread_mutex_destroy(&results[i].mutex);
        pthread_cond_destroy(&results[i].condition);
    }
    write_central_directory(output, &entries);
    fclose(output);

    progress_state_destroy(&progress);
    printf("생성 완료: %s (%zu개 항목, %d개 스레드, %s 모드)\n",
           archive_path,
           entries.count,
           tuned_options.thread_count,
           tuned_options.performance_mode);
    free(results);
    free_central_list(&entries);
    free_sources(sources, source_count);
}
