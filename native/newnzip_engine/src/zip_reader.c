#include "common.h"
#include "zip_reader.h"

typedef struct {
    const char *archive_path;
    const char *destination;
    CentralList *entries;
    size_t next_index;
    pthread_mutex_t mutex;
    ProgressState *progress;
} ExtractQueue;

static unsigned char *read_archive_tail(FILE *file, size_t *tail_size, long *archive_size) {
    if (fseek(file, 0, SEEK_END) != 0) {
        fail_errno("failed to seek archive");
    }
    long size = ftell(file);
    if (size < 0) {
        fail_errno("failed to determine archive size");
    }
    long start = size > 65557 ? size - 65557 : 0;
    size_t length = (size_t) (size - start);
    if (fseek(file, start, SEEK_SET) != 0) {
        fail_errno("failed to seek archive tail");
    }
    unsigned char *buffer = malloc(length);
    if (!buffer && length > 0) {
        fail("out of memory");
    }
    if (length > 0 && fread(buffer, 1, length, file) != length) {
        fail_errno("failed to read archive tail");
    }
    *tail_size = length;
    *archive_size = size;
    return buffer;
}

static long find_eocd_offset(const unsigned char *tail, size_t tail_size, long archive_size) {
    size_t i = tail_size >= 4 ? tail_size - 4 : 0;
    while (1) {
        if (read_u32(tail + i) == ZIP_END_OF_CENTRAL_DIRECTORY) {
            return archive_size - (long) tail_size + (long) i;
        }
        if (i == 0) {
            break;
        }
        i--;
    }
    fail("end of central directory not found");
    return -1;
}

static CentralList parse_central_directory(FILE *file) {
    size_t tail_size = 0;
    long archive_size = 0;
    unsigned char *tail = read_archive_tail(file, &tail_size, &archive_size);
    long eocd_offset = find_eocd_offset(tail, tail_size, archive_size);
    size_t eocd_in_tail = (size_t) (eocd_offset - (archive_size - (long) tail_size));

    uint16_t entry_count = read_u16(tail + eocd_in_tail + 10);
    uint32_t central_size = read_u32(tail + eocd_in_tail + 12);
    uint32_t central_offset = read_u32(tail + eocd_in_tail + 16);
    free(tail);

    if (fseek(file, central_offset, SEEK_SET) != 0) {
        fail_errno("failed to seek central directory");
    }

    unsigned char *buffer = malloc(central_size);
    if (!buffer && central_size > 0) {
        fail("out of memory");
    }
    if (central_size > 0 && fread(buffer, 1, central_size, file) != central_size) {
        free(buffer);
        fail_errno("failed to read central directory");
    }

    CentralList list = {0};
    size_t cursor = 0;
    while (cursor < central_size && list.count < entry_count) {
        if (read_u32(buffer + cursor) != ZIP_CENTRAL_DIRECTORY_HEADER) {
            free(buffer);
            free_central_list(&list);
            fail("invalid central directory header");
        }
        uint16_t method = read_u16(buffer + cursor + 10);
        uint16_t mod_time = read_u16(buffer + cursor + 12);
        uint16_t mod_date = read_u16(buffer + cursor + 14);
        uint32_t crc = read_u32(buffer + cursor + 16);
        uint32_t compressed_size = read_u32(buffer + cursor + 20);
        uint32_t uncompressed_size = read_u32(buffer + cursor + 24);
        uint16_t name_length = read_u16(buffer + cursor + 28);
        uint16_t extra_length = read_u16(buffer + cursor + 30);
        uint16_t comment_length = read_u16(buffer + cursor + 32);
        uint32_t local_offset = read_u32(buffer + cursor + 42);

        char *name = malloc(name_length + 1);
        if (!name) {
            free(buffer);
            free_central_list(&list);
            fail("out of memory");
        }
        memcpy(name, buffer + cursor + 46, name_length);
        name[name_length] = '\0';

        CentralEntry entry;
        entry.name = name;
        entry.crc32 = crc;
        entry.compressed_size = compressed_size;
        entry.uncompressed_size = uncompressed_size;
        entry.local_header_offset = local_offset;
        entry.method = method;
        entry.mod_time = mod_time;
        entry.mod_date = mod_date;
        central_list_push(&list, entry);

        cursor += 46 + name_length + extra_length + comment_length;
    }
    free(buffer);
    return list;
}

static void copy_stored_bytes(FILE *archive, FILE *output, uint32_t size) {
    unsigned char buffer[CHUNK_SIZE];
    uint32_t remaining = size;
    while (remaining > 0) {
        size_t chunk = remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining;
        if (fread(buffer, 1, chunk, archive) != chunk) {
            fail_errno("failed to read stored bytes");
        }
        if (fwrite(buffer, 1, chunk, output) != chunk) {
            fail_errno("failed to write extracted file");
        }
        remaining -= (uint32_t) chunk;
    }
}

static void inflate_stream_to_file(FILE *archive, FILE *output, uint32_t compressed_size) {
    unsigned char in_buffer[CHUNK_SIZE];
    unsigned char out_buffer[CHUNK_SIZE];
    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        fail("failed to initialize inflate");
    }

    uint32_t remaining = compressed_size;
    int finished = 0;
    while (!finished && remaining > 0) {
        size_t chunk = remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining;
        if (fread(in_buffer, 1, chunk, archive) != chunk) {
            inflateEnd(&stream);
            fail_errno("failed to read compressed bytes");
        }
        remaining -= (uint32_t) chunk;
        stream.next_in = in_buffer;
        stream.avail_in = (uInt) chunk;

        while (stream.avail_in > 0 || (remaining == 0 && !finished)) {
            stream.next_out = out_buffer;
            stream.avail_out = CHUNK_SIZE;
            int result = inflate(&stream, Z_NO_FLUSH);
            if (result == Z_STREAM_END) {
                finished = 1;
            } else if (result != Z_OK) {
                inflateEnd(&stream);
                fail("failed to inflate archive entry");
            }
            size_t have = CHUNK_SIZE - stream.avail_out;
            if (have > 0 && fwrite(out_buffer, 1, have, output) != have) {
                inflateEnd(&stream);
                fail_errno("failed to write extracted file");
            }
            if (result == Z_STREAM_END || stream.avail_in == 0) {
                break;
            }
        }
    }

    inflateEnd(&stream);
}

static void extract_entry(FILE *archive, const CentralEntry *entry, const char *destination) {
    if (fseek(archive, entry->local_header_offset, SEEK_SET) != 0) {
        fail_errno("failed to seek local header");
    }
    unsigned char header[30];
    if (fread(header, 1, sizeof(header), archive) != sizeof(header)) {
        fail_errno("failed to read local header");
    }
    if (read_u32(header) != ZIP_LOCAL_FILE_HEADER) {
        fail("invalid local header");
    }
    uint16_t name_length = read_u16(header + 26);
    uint16_t extra_length = read_u16(header + 28);
    if (fseek(archive, name_length + extra_length, SEEK_CUR) != 0) {
        fail_errno("failed to seek local payload");
    }

    char *target_path = join_path(destination, entry->name);
    ensure_parent_directories(target_path);
    FILE *output = fopen(target_path, "wb");
    if (!output) {
        free(target_path);
        fail_errno(target_path);
    }

    if (entry->method == ZIP_METHOD_STORE) {
        copy_stored_bytes(archive, output, entry->compressed_size);
    } else if (entry->method == ZIP_METHOD_DEFLATE) {
        inflate_stream_to_file(archive, output, entry->compressed_size);
    } else {
        fclose(output);
        free(target_path);
        fail("unsupported compression method");
    }

    fclose(output);
    free(target_path);
}

static size_t next_extract_index(ExtractQueue *queue) {
    size_t index = SIZE_MAX;
    pthread_mutex_lock(&queue->mutex);
    if (queue->next_index < queue->entries->count) {
        index = queue->next_index;
        queue->next_index += 1;
    }
    pthread_mutex_unlock(&queue->mutex);
    return index;
}

static void *extract_worker_main(void *raw_queue) {
    ExtractQueue *queue = raw_queue;
    FILE *archive = fopen(queue->archive_path, "rb");
    if (!archive) {
        fail_errno(queue->archive_path);
    }

    while (1) {
        size_t index = next_extract_index(queue);
        if (index == SIZE_MAX) {
            break;
        }
        extract_entry(archive, &queue->entries->items[index], queue->destination);
        progress_step(queue->progress, "extract", queue->entries->items[index].name);
    }

    fclose(archive);
    return NULL;
}

void command_list(const char *archive_path) {
    FILE *file = fopen(archive_path, "rb");
    if (!file) {
        fail_errno(archive_path);
    }

    CentralList entries = parse_central_directory(file);
    for (size_t i = 0; i < entries.count; i++) {
        printf("%s\t%u\t%u\n",
               entries.items[i].name,
               entries.items[i].uncompressed_size,
               entries.items[i].compressed_size);
    }

    free_central_list(&entries);
    fclose(file);
}

void command_extract(const char *archive_path, const char *destination, const RuntimeOptions *options) {
    FILE *file = fopen(archive_path, "rb");
    if (!file) {
        fail_errno(archive_path);
    }
    if (mkdir(destination, 0755) != 0 && errno != EEXIST) {
        fclose(file);
        fail_errno(destination);
    }

    CentralList entries = parse_central_directory(file);
    fclose(file);

    ExtractQueue queue;
    queue.archive_path = archive_path;
    queue.destination = destination;
    queue.entries = &entries;
    queue.next_index = 0;
    ProgressState progress;
    progress_state_init(&progress, entries.count);
    queue.progress = &progress;
    pthread_mutex_init(&queue.mutex, NULL);

    int worker_count = options->thread_count;
    if ((size_t) worker_count > entries.count) {
        worker_count = (int) entries.count;
    }
    if (worker_count < 1) {
        worker_count = 1;
    }

    pthread_t *threads = calloc((size_t) worker_count, sizeof(pthread_t));
    if (!threads) {
        pthread_mutex_destroy(&queue.mutex);
        free_central_list(&entries);
        fail("메모리가 부족합니다");
    }

    for (int i = 0; i < worker_count; i++) {
        if (pthread_create(&threads[i], NULL, extract_worker_main, &queue) != 0) {
            pthread_mutex_destroy(&queue.mutex);
            free(threads);
            free_central_list(&entries);
            fail("해제 작업 스레드를 만들지 못했습니다");
        }
    }

    for (int i = 0; i < worker_count; i++) {
        pthread_join(threads[i], NULL);
    }
    pthread_mutex_destroy(&queue.mutex);
    progress_state_destroy(&progress);
    free(threads);

    printf("해제 완료: %zu개 항목 -> %s (%d개 스레드)\n", entries.count, destination, worker_count);

    free_central_list(&entries);
}
