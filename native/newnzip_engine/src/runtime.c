#include "common.h"

double monotonic_seconds(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        fail_errno("단조 시계를 읽지 못했습니다");
    }
    return (double) ts.tv_sec + ((double) ts.tv_nsec / 1000000000.0);
}

int detect_cpu_count(void) {
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    if (count < 1) {
        return 1;
    }
    if (count > 64) {
        return 64;
    }
    return (int) count;
}

RuntimeOptions default_runtime_options(void) {
    RuntimeOptions options;
    options.thread_count = detect_cpu_count();
    options.compression_level = Z_DEFAULT_COMPRESSION;
    return options;
}

int parse_thread_argument(const char *value, const RuntimeOptions *defaults) {
    if (!value || !*value) {
        return defaults->thread_count;
    }

    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (!end || *end != '\0' || parsed < 1 || parsed > 64) {
        fail("스레드 수는 1부터 64 사이의 정수여야 합니다");
    }
    return (int) parsed;
}

char *create_temp_path(const char *prefix) {
    const char *base = getenv("TMPDIR");
    if (!base || !*base) {
        base = "/tmp";
    }

    size_t length = strlen(base) + strlen(prefix) + 16;
    char *pattern = malloc(length);
    if (!pattern) {
        fail("메모리가 부족합니다");
    }

    snprintf(pattern, length, "%s/%sXXXXXX", base, prefix);
    int fd = mkstemp(pattern);
    if (fd < 0) {
        free(pattern);
        fail_errno("임시 파일을 만들지 못했습니다");
    }
    close(fd);
    return pattern;
}

void remove_file_if_exists(const char *path) {
    if (!path) {
        return;
    }
    if (unlink(path) != 0 && errno != ENOENT) {
        fail_errno(path);
    }
}

void remove_tree(const char *path) {
    struct stat item_stat;
    if (lstat(path, &item_stat) != 0) {
        if (errno == ENOENT) {
            return;
        }
        fail_errno(path);
    }

    if (S_ISDIR(item_stat.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) {
            fail_errno(path);
        }

        struct dirent *entry = NULL;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            char *child = join_path(path, entry->d_name);
            remove_tree(child);
            free(child);
        }
        closedir(dir);

        if (rmdir(path) != 0) {
            fail_errno(path);
        }
        return;
    }

    remove_file_if_exists(path);
}
