#include "common.h"
#include "zip_reader.h"

typedef struct {
    const char *archive_path;
    const char *destination;
    CentralList *entries;
    size_t next_index;
    pthread_mutex_t mutex;
    ProgressState *progress;
    size_t chunk_size;
} ExtractQueue;

typedef struct {
    uint64_t entry_count;
    uint64_t central_size;
    uint64_t central_offset;
} CentralDirectoryLocation;

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

static CentralDirectoryLocation read_zip64_central_directory_location(FILE *file, long eocd_offset) {
    if (eocd_offset < 20) {
        fail("zip64 locator not found");
    }
    if (fseek(file, eocd_offset - 20, SEEK_SET) != 0) {
        fail_errno("failed to seek zip64 locator");
    }
    unsigned char locator[20];
    if (fread(locator, 1, sizeof(locator), file) != sizeof(locator)) {
        fail_errno("failed to read zip64 locator");
    }
    if (read_u32(locator) != ZIP64_END_OF_CENTRAL_DIRECTORY_LOCATOR) {
        fail("zip64 locator not found");
    }

    uint64_t zip64_eocd_offset = read_u64(locator + 8);
    if (fseek(file, (long) zip64_eocd_offset, SEEK_SET) != 0) {
        fail_errno("failed to seek zip64 eocd");
    }

    unsigned char fixed[56];
    if (fread(fixed, 1, sizeof(fixed), file) != sizeof(fixed)) {
        fail_errno("failed to read zip64 eocd");
    }
    if (read_u32(fixed) != ZIP64_END_OF_CENTRAL_DIRECTORY) {
        fail("invalid zip64 eocd");
    }

    CentralDirectoryLocation location;
    location.entry_count = read_u64(fixed + 32);
    location.central_size = read_u64(fixed + 40);
    location.central_offset = read_u64(fixed + 48);
    return location;
}

static void apply_zip64_extra(CentralEntry *entry, const unsigned char *extra, uint16_t extra_length) {
    size_t cursor = 0;
    while (cursor + 4 <= extra_length) {
        uint16_t header_id = read_u16(extra + cursor);
        uint16_t data_size = read_u16(extra + cursor + 2);
        cursor += 4;
        if (cursor + data_size > extra_length) {
            break;
        }
        if (header_id == ZIP64_EXTRA_FIELD_ID) {
            size_t data_cursor = cursor;
            if (entry->uncompressed_size == UINT32_MAX && data_cursor + 8 <= cursor + data_size) {
                entry->uncompressed_size = read_u64(extra + data_cursor);
                data_cursor += 8;
            }
            if (entry->compressed_size == UINT32_MAX && data_cursor + 8 <= cursor + data_size) {
                entry->compressed_size = read_u64(extra + data_cursor);
                data_cursor += 8;
            }
            if (entry->local_header_offset == UINT32_MAX && data_cursor + 8 <= cursor + data_size) {
                entry->local_header_offset = read_u64(extra + data_cursor);
            }
            return;
        }
        cursor += data_size;
    }
}

static CentralList parse_central_directory(FILE *file) {
    size_t tail_size = 0;
    long archive_size = 0;
    unsigned char *tail = read_archive_tail(file, &tail_size, &archive_size);
    long eocd_offset = find_eocd_offset(tail, tail_size, archive_size);
    size_t eocd_in_tail = (size_t) (eocd_offset - (archive_size - (long) tail_size));

    CentralDirectoryLocation location;
    location.entry_count = read_u16(tail + eocd_in_tail + 10);
    location.central_size = read_u32(tail + eocd_in_tail + 12);
    location.central_offset = read_u32(tail + eocd_in_tail + 16);
    bool uses_zip64 = location.entry_count == UINT16_MAX ||
                      location.central_size == UINT32_MAX ||
                      location.central_offset == UINT32_MAX;
    free(tail);

    if (uses_zip64) {
        location = read_zip64_central_directory_location(file, eocd_offset);
    }

    if (location.central_size > SIZE_MAX) {
        fail("central directory is too large");
    }
    if (fseek(file, (long) location.central_offset, SEEK_SET) != 0) {
        fail_errno("failed to seek central directory");
    }

    size_t central_size = (size_t) location.central_size;
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
    while (cursor < central_size && list.count < location.entry_count) {
        if (read_u32(buffer + cursor) != ZIP_CENTRAL_DIRECTORY_HEADER) {
            free(buffer);
            free_central_list(&list);
            fail("invalid central directory header");
        }
        uint16_t method = read_u16(buffer + cursor + 10);
        uint16_t mod_time = read_u16(buffer + cursor + 12);
        uint16_t mod_date = read_u16(buffer + cursor + 14);
        uint32_t crc = read_u32(buffer + cursor + 16);
        uint64_t compressed_size = read_u32(buffer + cursor + 20);
        uint64_t uncompressed_size = read_u32(buffer + cursor + 24);
        uint16_t name_length = read_u16(buffer + cursor + 28);
        uint16_t extra_length = read_u16(buffer + cursor + 30);
        uint16_t comment_length = read_u16(buffer + cursor + 32);
        uint32_t external_attributes = read_u32(buffer + cursor + 38);
        uint64_t local_offset = read_u32(buffer + cursor + 42);

        char *raw_name = malloc(name_length + 1);
        if (!raw_name) {
            free(buffer);
            free_central_list(&list);
            fail("out of memory");
        }
        memcpy(raw_name, buffer + cursor + 46, name_length);
        raw_name[name_length] = '\0';
        char *name = normalize_archive_name(raw_name);
        free(raw_name);

        CentralEntry entry;
        entry.name = name;
        entry.crc32 = crc;
        entry.compressed_size = compressed_size;
        entry.uncompressed_size = uncompressed_size;
        entry.local_header_offset = local_offset;
        entry.external_attributes = external_attributes;
        entry.method = method;
        entry.mod_time = mod_time;
        entry.mod_date = mod_date;
        apply_zip64_extra(&entry, buffer + cursor + 46 + name_length, extra_length);
        central_list_push(&list, entry);

        cursor += 46 + name_length + extra_length + comment_length;
    }
    free(buffer);
    return list;
}

static bool entry_is_symlink(const CentralEntry *entry) {
    mode_t mode = (mode_t) ((entry->external_attributes >> 16) & 0xffffu);
    return (mode & S_IFMT) == S_IFLNK;
}

static char *read_stored_payload(FILE *archive, uint64_t size) {
    if (size > SIZE_MAX - 1) {
        fail("stored payload is too large");
    }
    char *payload = malloc((size_t) size + 1);
    if (!payload) {
        fail("out of memory");
    }
    if (size > 0 && fread(payload, 1, (size_t) size, archive) != (size_t) size) {
        free(payload);
        fail_errno("failed to read stored bytes");
    }
    payload[size] = '\0';
    return payload;
}

static void copy_stored_bytes(FILE *archive, FILE *output, uint64_t size, unsigned char *buffer, size_t chunk_size) {
    uint64_t remaining = size;
    while (remaining > 0) {
        size_t chunk = remaining > chunk_size ? chunk_size : (size_t) remaining;
        if (fread(buffer, 1, chunk, archive) != chunk) {
            fail_errno("failed to read stored bytes");
        }
        if (fwrite(buffer, 1, chunk, output) != chunk) {
            fail_errno("failed to write extracted file");
        }
        remaining -= (uint64_t) chunk;
    }
}

static void inflate_stream_to_file(FILE *archive, FILE *output, uint64_t compressed_size, unsigned char *in_buffer, unsigned char *out_buffer, size_t chunk_size) {
    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        fail("failed to initialize inflate");
    }

    uint64_t remaining = compressed_size;
    int finished = 0;
    while (!finished && remaining > 0) {
        size_t chunk = remaining > chunk_size ? chunk_size : (size_t) remaining;
        if (fread(in_buffer, 1, chunk, archive) != chunk) {
            inflateEnd(&stream);
            fail_errno("failed to read compressed bytes");
        }
        remaining -= (uint64_t) chunk;
        stream.next_in = in_buffer;
        stream.avail_in = (uInt) chunk;

        while (stream.avail_in > 0 || (remaining == 0 && !finished)) {
            stream.next_out = out_buffer;
            stream.avail_out = (uInt) chunk_size;
            int result = inflate(&stream, Z_NO_FLUSH);
            if (result == Z_STREAM_END) {
                finished = 1;
            } else if (result != Z_OK) {
                inflateEnd(&stream);
                fail("failed to inflate archive entry");
            }
            size_t have = chunk_size - stream.avail_out;
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

static void extract_entry(
    FILE *archive,
    const CentralEntry *entry,
    const char *destination,
    unsigned char *input_buffer,
    unsigned char *output_buffer,
    size_t chunk_size
) {
    if (fseek(archive, (long) entry->local_header_offset, SEEK_SET) != 0) {
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

    if (entry_is_symlink(entry)) {
        if (entry->method != ZIP_METHOD_STORE) {
            free(target_path);
            fail("unsupported symlink compression method");
        }
        char *link_target = read_stored_payload(archive, entry->compressed_size);
        if (unlink(target_path) != 0 && errno != ENOENT) {
            free(link_target);
            free(target_path);
            fail_errno(target_path);
        }
        if (symlink(link_target, target_path) != 0) {
            free(link_target);
            free(target_path);
            fail_errno(target_path);
        }
        free(link_target);
        free(target_path);
        return;
    }

    FILE *output = fopen(target_path, "wb");
    if (!output) {
        free(target_path);
        fail_errno(target_path);
    }

    if (entry->method == ZIP_METHOD_STORE) {
        copy_stored_bytes(archive, output, entry->compressed_size, input_buffer, chunk_size);
    } else if (entry->method == ZIP_METHOD_DEFLATE) {
        inflate_stream_to_file(archive, output, entry->compressed_size, input_buffer, output_buffer, chunk_size);
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

    unsigned char *input_buffer = malloc(queue->chunk_size);
    unsigned char *output_buffer = malloc(queue->chunk_size);
    if (!input_buffer || !output_buffer) {
        fail("메모리가 부족합니다");
    }

    while (1) {
        size_t index = next_extract_index(queue);
        if (index == SIZE_MAX) {
            break;
        }
        extract_entry(
            archive,
            &queue->entries->items[index],
            queue->destination,
            input_buffer,
            output_buffer,
            queue->chunk_size
        );
        progress_step(queue->progress, "extract", queue->entries->items[index].name);
    }

    free(input_buffer);
    free(output_buffer);
    fclose(archive);
    return NULL;
}

static int compare_entry_size_desc(const void *left, const void *right) {
    const CentralEntry *a = left;
    const CentralEntry *b = right;
    if (a->compressed_size < b->compressed_size) {
        return 1;
    }
    if (a->compressed_size > b->compressed_size) {
        return -1;
    }
    return strcmp(a->name, b->name);
}

void command_list(const char *archive_path) {
    FILE *file = fopen(archive_path, "rb");
    if (!file) {
        fail_errno(archive_path);
    }

    CentralList entries = parse_central_directory(file);
    for (size_t i = 0; i < entries.count; i++) {
        printf("%s\t%llu\t%llu\n",
               entries.items[i].name,
               (unsigned long long) entries.items[i].uncompressed_size,
               (unsigned long long) entries.items[i].compressed_size);
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

    qsort(entries.items, entries.count, sizeof(CentralEntry), compare_entry_size_desc);
    RuntimeOptions tuned_options = *options;
    uint64_t total_size = 0;
    for (size_t i = 0; i < entries.count; i++) {
        total_size += entries.items[i].compressed_size;
    }
    tune_runtime_for_source_count(&tuned_options, entries.count, total_size);

    ExtractQueue queue;
    queue.archive_path = archive_path;
    queue.destination = destination;
    queue.entries = &entries;
    queue.next_index = 0;
    queue.chunk_size = tuned_options.chunk_size;
    ProgressState progress;
    progress_state_init(&progress, entries.count);
    queue.progress = &progress;
    pthread_mutex_init(&queue.mutex, NULL);

    int worker_count = tuned_options.thread_count;
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

    printf("해제 완료: %zu개 항목 -> %s (%d개 스레드, %s 모드)\n",
           entries.count,
           destination,
           worker_count,
           tuned_options.performance_mode);

    free_central_list(&entries);
}

static void join_split_files(const char *start_path, const char *destination_path, size_t chunk_size) {
    size_t start_length = strlen(start_path);
    if (start_length <= 4 || strcmp(start_path + start_length - 4, ".001") != 0) {
        fail("분할 ZIP의 첫 번째 파일(.001)을 지정하세요");
    }

    char base_path[PATH_MAX];
    if (start_length - 4 >= sizeof(base_path)) {
        fail("path too long");
    }
    memcpy(base_path, start_path, start_length - 4);
    base_path[start_length - 4] = '\0';

    FILE *output = fopen(destination_path, "wb");
    if (!output) {
        fail_errno(destination_path);
    }

    unsigned char *buffer = malloc(chunk_size);
    if (!buffer) {
        fclose(output);
        fail("메모리가 부족합니다");
    }

    for (int part = 1; part < 10000; part++) {
        char part_path[PATH_MAX];
        snprintf(part_path, sizeof(part_path), "%s.%03d", base_path, part);
        FILE *input = fopen(part_path, "rb");
        if (!input) {
            if (part == 1) {
                free(buffer);
                fclose(output);
                fail_errno(part_path);
            }
            break;
        }

        while (1) {
            size_t read_size = fread(buffer, 1, chunk_size, input);
            if (read_size > 0 && fwrite(buffer, 1, read_size, output) != read_size) {
                fclose(input);
                free(buffer);
                fclose(output);
                fail_errno(destination_path);
            }
            if (read_size < chunk_size) {
                if (ferror(input)) {
                    fclose(input);
                    free(buffer);
                    fclose(output);
                    fail_errno(part_path);
                }
                break;
            }
        }
        fclose(input);
    }

    free(buffer);
    fclose(output);
}

void command_extract_split(const char *archive_path, const char *destination, const RuntimeOptions *options) {
    char *joined_path = create_temp_path("newnzip-joined-");
    join_split_files(archive_path, joined_path, options->chunk_size);
    command_extract(joined_path, destination, options);
    remove_file_if_exists(joined_path);
    free(joined_path);
}
