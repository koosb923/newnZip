#include "common.h"
#include "zip_writer.h"

typedef struct {
    SourceEntry *sources;
    size_t source_count;
    size_t next_index;
    pthread_mutex_t mutex;
    int compression_level;
} CompressionQueue;

typedef struct {
    CentralEntry entry;
    char *temp_path;
} CompressionResult;

typedef struct {
    CompressionQueue *queue;
    CompressionResult *results;
    ProgressState *progress;
} CompressionWorkerContext;

static CentralEntry compress_source_to_temp(const char *source_path, const char *archive_name, const char *temp_path, int compression_level) {
    struct stat file_stat;
    if (stat(source_path, &file_stat) != 0) {
        fail_errno(source_path);
    }

    FILE *input = fopen(source_path, "rb");
    if (!input) {
        fail_errno(source_path);
    }

    FILE *temp_output = fopen(temp_path, "wb");
    if (!temp_output) {
        fclose(input);
        fail_errno(temp_path);
    }

    char *normalized_name = normalize_archive_name(archive_name);

    CentralEntry entry;
    entry.name = normalized_name;
    entry.crc32 = crc32(0L, Z_NULL, 0);
    entry.compressed_size = 0;
    entry.uncompressed_size = 0;
    entry.local_header_offset = 0;
    entry.method = ZIP_METHOD_DEFLATE;
    entry.mod_time = dos_time_value(file_stat.st_mtime);
    entry.mod_date = dos_date_value(file_stat.st_mtime);

    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    if (deflateInit2(&stream, compression_level, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        fclose(temp_output);
        fclose(input);
        fail("압축기를 초기화하지 못했습니다");
    }

    unsigned char in_buffer[CHUNK_SIZE];
    unsigned char out_buffer[CHUNK_SIZE];
    int flush = Z_NO_FLUSH;

    do {
        stream.avail_in = (uInt) fread(in_buffer, 1, CHUNK_SIZE, input);
        if (ferror(input)) {
            deflateEnd(&stream);
            fclose(temp_output);
            fclose(input);
            fail_errno("원본 파일을 읽지 못했습니다");
        }
        entry.uncompressed_size += (uint32_t) stream.avail_in;
        entry.crc32 = crc32(entry.crc32, in_buffer, stream.avail_in);
        flush = feof(input) ? Z_FINISH : Z_NO_FLUSH;
        stream.next_in = in_buffer;

        do {
            stream.avail_out = CHUNK_SIZE;
            stream.next_out = out_buffer;
            int result = deflate(&stream, flush);
            if (result == Z_STREAM_ERROR) {
                deflateEnd(&stream);
                fclose(temp_output);
                fclose(input);
                fail("압축 데이터 생성에 실패했습니다");
            }
            size_t have = CHUNK_SIZE - stream.avail_out;
            if (have > 0 && fwrite(out_buffer, 1, have, temp_output) != have) {
                deflateEnd(&stream);
                fclose(temp_output);
                fclose(input);
                fail_errno("임시 압축 데이터를 쓰지 못했습니다");
            }
            entry.compressed_size += (uint32_t) have;
        } while (stream.avail_out == 0);
    } while (flush != Z_FINISH);

    if (deflateEnd(&stream) != Z_OK) {
        fclose(temp_output);
        fclose(input);
        fail("압축기를 정상 종료하지 못했습니다");
    }
    fclose(temp_output);
    fclose(input);
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
    while (1) {
        size_t index = next_source_index(context->queue);
        if (index == SIZE_MAX) {
            break;
        }

        char *temp_path = create_temp_path("newnzip-deflate-");
        CentralEntry entry = compress_source_to_temp(
            context->queue->sources[index].path,
            context->queue->sources[index].archive_name,
            temp_path,
            context->queue->compression_level
        );

        context->results[index].entry = entry;
        context->results[index].temp_path = temp_path;
        progress_step(context->progress, "compress", entry.name);
    }
    return NULL;
}

static CompressionResult *compress_sources_parallel(SourceEntry *sources, size_t source_count, const RuntimeOptions *options) {
    CompressionQueue queue;
    queue.sources = sources;
    queue.source_count = source_count;
    queue.next_index = 0;
    queue.compression_level = options->compression_level;
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
    ProgressState progress;
    progress_state_init(&progress, source_count);
    context.progress = &progress;

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
    progress_state_destroy(&progress);
    free(threads);
    return results;
}

static void write_local_header(FILE *output, const CentralEntry *entry) {
    uint16_t name_length = (uint16_t) strlen(entry->name);
    write_u32(output, ZIP_LOCAL_FILE_HEADER);
    write_u16(output, ZIP_VERSION);
    write_u16(output, 0);
    write_u16(output, entry->method);
    write_u16(output, entry->mod_time);
    write_u16(output, entry->mod_date);
    write_u32(output, entry->crc32);
    write_u32(output, entry->compressed_size);
    write_u32(output, entry->uncompressed_size);
    write_u16(output, name_length);
    write_u16(output, 0);
    if (fwrite(entry->name, 1, name_length, output) != name_length) {
        fail_errno("파일 이름을 기록하지 못했습니다");
    }
}

static void copy_file_contents(FILE *output, const char *temp_path, uint32_t *written_size) {
    FILE *input = fopen(temp_path, "rb");
    if (!input) {
        fail_errno(temp_path);
    }

    unsigned char buffer[CHUNK_SIZE];
    uint32_t total = 0;
    while (1) {
        size_t read_size = fread(buffer, 1, sizeof(buffer), input);
        if (read_size > 0) {
            if (fwrite(buffer, 1, read_size, output) != read_size) {
                fclose(input);
                fail_errno("압축 결과를 아카이브에 쓰지 못했습니다");
            }
            total += (uint32_t) read_size;
        }
        if (read_size < sizeof(buffer)) {
            if (ferror(input)) {
                fclose(input);
                fail_errno("임시 압축 파일을 읽지 못했습니다");
            }
            break;
        }
    }
    fclose(input);
    *written_size = total;
}

static void write_central_directory(FILE *output, const CentralList *entries) {
    long central_offset = ftell(output);
    if (central_offset < 0) {
        fail_errno("failed to determine central directory offset");
    }

    for (size_t i = 0; i < entries->count; i++) {
        const CentralEntry *entry = &entries->items[i];
        uint16_t name_length = (uint16_t) strlen(entry->name);
        write_u32(output, ZIP_CENTRAL_DIRECTORY_HEADER);
        write_u16(output, ZIP_VERSION);
        write_u16(output, ZIP_VERSION);
        write_u16(output, 0);
        write_u16(output, entry->method);
        write_u16(output, entry->mod_time);
        write_u16(output, entry->mod_date);
        write_u32(output, entry->crc32);
        write_u32(output, entry->compressed_size);
        write_u32(output, entry->uncompressed_size);
        write_u16(output, name_length);
        write_u16(output, 0);
        write_u16(output, 0);
        write_u16(output, 0);
        write_u16(output, 0);
        write_u32(output, 0);
        write_u32(output, entry->local_header_offset);
        if (fwrite(entry->name, 1, name_length, output) != name_length) {
            fail_errno("failed to write central directory entry");
        }
    }

    long central_end = ftell(output);
    if (central_end < 0) {
        fail_errno("failed to determine central directory size");
    }
    uint32_t central_size = (uint32_t) (central_end - central_offset);

    write_u32(output, ZIP_END_OF_CENTRAL_DIRECTORY);
    write_u16(output, 0);
    write_u16(output, 0);
    write_u16(output, (uint16_t) entries->count);
    write_u16(output, (uint16_t) entries->count);
    write_u32(output, central_size);
    write_u32(output, (uint32_t) central_offset);
    write_u16(output, 0);
}

void command_create(int argc, char **argv, const RuntimeOptions *options) {
    if (argc < 4) {
        fail("사용법: newnzip-engine create output.zip <파일-또는-폴더>...");
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
        fail("no regular files found in sources");
    }

    CompressionResult *results = compress_sources_parallel(sources, source_count, options);
    CentralList entries = {0};
    for (size_t i = 0; i < source_count; i++) {
        long offset = ftell(output);
        if (offset < 0) {
            fail_errno("출력 오프셋을 계산하지 못했습니다");
        }
        results[i].entry.local_header_offset = (uint32_t) offset;
        write_local_header(output, &results[i].entry);

        uint32_t written_size = 0;
        copy_file_contents(output, results[i].temp_path, &written_size);
        if (written_size != results[i].entry.compressed_size) {
            fail("임시 압축 데이터 크기가 메타데이터와 다릅니다");
        }

        central_list_push(&entries, results[i].entry);
        remove_file_if_exists(results[i].temp_path);
        free(results[i].temp_path);
    }
    write_central_directory(output, &entries);
    fclose(output);

    printf("생성 완료: %s (%zu개 항목, %d개 스레드)\n", archive_path, entries.count, options->thread_count);
    free(results);
    free_central_list(&entries);
    free_sources(sources, source_count);
}
