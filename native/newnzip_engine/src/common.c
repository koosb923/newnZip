#include "common.h"

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

void fail(const char *message) {
    fprintf(stderr, "%s\n", message);
    exit(1);
}

void fail_errno(const char *message) {
    fprintf(stderr, "%s: %s\n", message, strerror(errno));
    exit(1);
}

uint16_t dos_time_value(time_t raw_time) {
    struct tm *local_time = localtime(&raw_time);
    if (!local_time) {
        return 0;
    }
    return (uint16_t) (((local_time->tm_hour & 0x1f) << 11) |
                       ((local_time->tm_min & 0x3f) << 5) |
                       ((local_time->tm_sec / 2) & 0x1f));
}

uint16_t dos_date_value(time_t raw_time) {
    struct tm *local_time = localtime(&raw_time);
    if (!local_time) {
        return 0;
    }
    int year = local_time->tm_year + 1900;
    if (year < 1980) {
        year = 1980;
    }
    return (uint16_t) ((((year - 1980) & 0x7f) << 9) |
                       (((local_time->tm_mon + 1) & 0x0f) << 5) |
                       (local_time->tm_mday & 0x1f));
}

void write_u16(FILE *file, uint16_t value) {
    unsigned char bytes[2];
    bytes[0] = (unsigned char) (value & 0xffu);
    bytes[1] = (unsigned char) ((value >> 8) & 0xffu);
    if (fwrite(bytes, 1, 2, file) != 2) {
        fail_errno("failed to write u16");
    }
}

void write_u32(FILE *file, uint32_t value) {
    unsigned char bytes[4];
    bytes[0] = (unsigned char) (value & 0xffu);
    bytes[1] = (unsigned char) ((value >> 8) & 0xffu);
    bytes[2] = (unsigned char) ((value >> 16) & 0xffu);
    bytes[3] = (unsigned char) ((value >> 24) & 0xffu);
    if (fwrite(bytes, 1, 4, file) != 4) {
        fail_errno("failed to write u32");
    }
}

void write_u64(FILE *file, uint64_t value) {
    unsigned char bytes[8];
    bytes[0] = (unsigned char) (value & 0xffu);
    bytes[1] = (unsigned char) ((value >> 8) & 0xffu);
    bytes[2] = (unsigned char) ((value >> 16) & 0xffu);
    bytes[3] = (unsigned char) ((value >> 24) & 0xffu);
    bytes[4] = (unsigned char) ((value >> 32) & 0xffu);
    bytes[5] = (unsigned char) ((value >> 40) & 0xffu);
    bytes[6] = (unsigned char) ((value >> 48) & 0xffu);
    bytes[7] = (unsigned char) ((value >> 56) & 0xffu);
    if (fwrite(bytes, 1, 8, file) != 8) {
        fail_errno("failed to write u64");
    }
}

uint16_t read_u16(const unsigned char *bytes) {
    return (uint16_t) bytes[0] | ((uint16_t) bytes[1] << 8);
}

uint32_t read_u32(const unsigned char *bytes) {
    return (uint32_t) bytes[0] |
           ((uint32_t) bytes[1] << 8) |
           ((uint32_t) bytes[2] << 16) |
           ((uint32_t) bytes[3] << 24);
}

uint64_t read_u64(const unsigned char *bytes) {
    return (uint64_t) bytes[0] |
           ((uint64_t) bytes[1] << 8) |
           ((uint64_t) bytes[2] << 16) |
           ((uint64_t) bytes[3] << 24) |
           ((uint64_t) bytes[4] << 32) |
           ((uint64_t) bytes[5] << 40) |
           ((uint64_t) bytes[6] << 48) |
           ((uint64_t) bytes[7] << 56);
}

char *duplicate_string(const char *value) {
    size_t length = strlen(value);
    char *copy = malloc(length + 1);
    if (!copy) {
        fail("out of memory");
    }
    memcpy(copy, value, length + 1);
    return copy;
}

char *join_path(const char *left, const char *right) {
    size_t left_len = strlen(left);
    size_t right_len = strlen(right);
    size_t needs_sep = (left_len > 0 && left[left_len - 1] != '/');
    char *result = malloc(left_len + right_len + needs_sep + 1);
    if (!result) {
        fail("out of memory");
    }
    memcpy(result, left, left_len);
    if (needs_sep) {
        result[left_len] = '/';
        left_len += 1;
    }
    memcpy(result + left_len, right, right_len);
    result[left_len + right_len] = '\0';
    return result;
}

char *normalize_archive_name(const char *name) {
    char *copy = NULL;
#if defined(__APPLE__)
    CFStringRef source = CFStringCreateWithCString(kCFAllocatorDefault, name, kCFStringEncodingUTF8);
    if (source) {
        CFMutableStringRef normalized = CFStringCreateMutableCopy(kCFAllocatorDefault, 0, source);
        CFRelease(source);
        if (normalized) {
            CFStringNormalize(normalized, kCFStringNormalizationFormC);
            CFIndex length = CFStringGetLength(normalized);
            CFIndex max_size = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
            copy = malloc((size_t) max_size);
            if (!copy) {
                CFRelease(normalized);
                fail("out of memory");
            }
            if (!CFStringGetCString(normalized, copy, max_size, kCFStringEncodingUTF8)) {
                free(copy);
                copy = NULL;
            }
            CFRelease(normalized);
        }
    }
#endif
    if (!copy) {
        copy = duplicate_string(name);
    }
    for (size_t i = 0; copy[i] != '\0'; i++) {
        if (copy[i] == '\\') {
            copy[i] = '/';
        }
    }
    return copy;
}

static const char *last_path_component(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static bool is_excluded_archive_component(const char *name) {
    return strcmp(name, ".DS_Store") == 0 ||
           strcmp(name, "__MACOSX") == 0 ||
           strcmp(name, ".Spotlight-V100") == 0 ||
           strcmp(name, ".Trashes") == 0 ||
           strcmp(name, ".fseventsd") == 0 ||
           strcmp(name, ".TemporaryItems") == 0 ||
           strcmp(name, ".DocumentRevisions-V100") == 0 ||
           strcmp(name, ".VolumeIcon.icns") == 0 ||
           strcmp(name, "Icon\r") == 0 ||
           strncmp(name, "._", 2) == 0;
}

bool should_exclude_archive_path(const char *path, const char *archive_name) {
    const char *leaf = last_path_component(path);
    if (is_excluded_archive_component(leaf)) {
        return true;
    }

    char *name_copy = duplicate_string(archive_name);
    char *component = strtok(name_copy, "/");
    while (component) {
        if (is_excluded_archive_component(component)) {
            free(name_copy);
            return true;
        }
        component = strtok(NULL, "/");
    }
    free(name_copy);
    return false;
}

void ensure_parent_directories(const char *path) {
    char buffer[PATH_MAX];
    size_t length = strlen(path);
    if (length >= sizeof(buffer)) {
        fail("path too long");
    }
    memcpy(buffer, path, length + 1);

    for (size_t i = 1; i < length; i++) {
        if (buffer[i] == '/') {
            buffer[i] = '\0';
            if (mkdir(buffer, 0755) != 0 && errno != EEXIST) {
                fail_errno("failed to create parent directory");
            }
            buffer[i] = '/';
        }
    }
}

void central_list_push(CentralList *list, CentralEntry entry) {
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity == 0 ? 16 : list->capacity * 2;
        CentralEntry *new_items = realloc(list->items, new_capacity * sizeof(CentralEntry));
        if (!new_items) {
            fail("out of memory");
        }
        list->items = new_items;
        list->capacity = new_capacity;
    }
    list->items[list->count++] = entry;
}

void free_central_list(CentralList *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i].name);
    }
    free(list->items);
}

static void source_list_push(
    SourceEntry **items,
    size_t *count,
    size_t *capacity,
    const char *path,
    const char *archive_name,
    const char *link_target,
    uint64_t size,
    mode_t mode,
    bool is_symlink
) {
    if (*count == *capacity) {
        size_t new_capacity = *capacity == 0 ? 16 : (*capacity * 2);
        SourceEntry *new_items = realloc(*items, new_capacity * sizeof(SourceEntry));
        if (!new_items) {
            fail("out of memory");
        }
        *items = new_items;
        *capacity = new_capacity;
    }
    (*items)[*count].path = duplicate_string(path);
    (*items)[*count].archive_name = duplicate_string(archive_name);
    (*items)[*count].link_target = link_target ? duplicate_string(link_target) : NULL;
    (*items)[*count].size = size;
    (*items)[*count].mode = mode;
    (*items)[*count].is_symlink = is_symlink;
    *count += 1;
}

void collect_sources(const char *path, const char *archive_root, SourceEntry **items, size_t *count, size_t *capacity) {
    if (should_exclude_archive_path(path, archive_root)) {
        return;
    }

    struct stat item_stat;
    if (lstat(path, &item_stat) != 0) {
        fail_errno(path);
    }

    if (S_ISLNK(item_stat.st_mode)) {
        size_t buffer_size = item_stat.st_size > 0 ? (size_t) item_stat.st_size + 1 : PATH_MAX;
        char *target = malloc(buffer_size);
        if (!target) {
            fail("out of memory");
        }
        ssize_t target_length = readlink(path, target, buffer_size - 1);
        if (target_length < 0) {
            free(target);
            fail_errno(path);
        }
        target[target_length] = '\0';
        source_list_push(items, count, capacity, path, archive_root, target, (uint64_t) target_length, item_stat.st_mode, true);
        free(target);
        return;
    }

    if (S_ISREG(item_stat.st_mode)) {
        source_list_push(items, count, capacity, path, archive_root, NULL, (uint64_t) item_stat.st_size, item_stat.st_mode, false);
        return;
    }

    if (!S_ISDIR(item_stat.st_mode)) {
        return;
    }

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
        char *child_name = join_path(archive_root, entry->d_name);
        collect_sources(child_path, child_name, items, count, capacity);
        free(child_path);
        free(child_name);
    }
    closedir(dir);
}

void free_sources(SourceEntry *items, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(items[i].path);
        free(items[i].archive_name);
        free(items[i].link_target);
    }
    free(items);
}

uint64_t total_source_size(const SourceEntry *sources, size_t count) {
    uint64_t total = 0;
    for (size_t i = 0; i < count; i++) {
        total += sources[i].size;
    }
    return total;
}

static int compare_source_size_desc(const void *left, const void *right) {
    const SourceEntry *a = left;
    const SourceEntry *b = right;
    if (a->size < b->size) {
        return 1;
    }
    if (a->size > b->size) {
        return -1;
    }
    return strcmp(a->archive_name, b->archive_name);
}

void sort_sources_by_size_desc(SourceEntry *items, size_t count) {
    qsort(items, count, sizeof(SourceEntry), compare_source_size_desc);
}
