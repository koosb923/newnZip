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
    options.detected_cpu_count = detect_cpu_count();
    options.thread_count = options.detected_cpu_count;
    options.compression_level = 3;
    options.chunk_size = CHUNK_SIZE;
    options.split_size = 0;
    options.archive_format = "zip";
    options.zip_method = "deflate";
#if defined(__APPLE__)
    options.small_file_threshold = 512 * 1024u;
#else
    options.small_file_threshold = 256 * 1024u;
#endif
    options.performance_mode = "auto";
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

uint64_t parse_size_argument(const char *value) {
    if (!value || !*value) {
        return 0;
    }

    char *end = NULL;
    double parsed = strtod(value, &end);
    if (!end || parsed <= 0.0) {
        fail("크기는 0보다 큰 숫자여야 합니다");
    }

    uint64_t multiplier = 1;
    if (*end != '\0') {
        if (strcasecmp(end, "k") == 0 || strcasecmp(end, "kb") == 0) {
            multiplier = 1024ull;
        } else if (strcasecmp(end, "m") == 0 || strcasecmp(end, "mb") == 0) {
            multiplier = 1024ull * 1024ull;
        } else if (strcasecmp(end, "g") == 0 || strcasecmp(end, "gb") == 0) {
            multiplier = 1024ull * 1024ull * 1024ull;
        } else {
            fail("크기 단위는 k, m, g 중 하나여야 합니다");
        }
    }

    return (uint64_t) (parsed * (double) multiplier);
}

void apply_performance_mode(RuntimeOptions *options, const char *mode_name) {
    if (!mode_name || strcmp(mode_name, "auto") == 0) {
        options->performance_mode = "auto";
        options->thread_count = options->detected_cpu_count;
        options->compression_level = 3;
        options->small_file_threshold = 512 * 1024u;
        return;
    }
    if (strcmp(mode_name, "balanced") == 0) {
        options->performance_mode = "balanced";
        options->thread_count = options->detected_cpu_count > 8 ? 8 : options->detected_cpu_count;
        options->compression_level = 4;
        options->small_file_threshold = 512 * 1024u;
        return;
    }
    if (strcmp(mode_name, "max") == 0) {
        options->performance_mode = "max";
        options->thread_count = options->detected_cpu_count;
        options->compression_level = 6;
        options->small_file_threshold = 1024 * 1024u;
        return;
    }
    if (strcmp(mode_name, "low-memory") == 0) {
        options->performance_mode = "low-memory";
        options->thread_count = options->detected_cpu_count > 4 ? 4 : options->detected_cpu_count;
        if (options->thread_count < 1) {
            options->thread_count = 1;
        }
        options->compression_level = 1;
        options->small_file_threshold = 128 * 1024u;
        return;
    }
    fail("지원하지 않는 성능 모드입니다. auto, balanced, max, low-memory 중 하나를 사용하세요");
}

void tune_runtime_for_source_count(RuntimeOptions *options, size_t source_count, uint64_t total_size) {
    if (source_count == 0) {
        return;
    }

    uint64_t average_size = total_size / source_count;
    if (strcmp(options->performance_mode, "auto") == 0) {
        if (source_count > 4000 || average_size < 64 * 1024u) {
            options->thread_count = options->detected_cpu_count > 6 ? 6 : options->detected_cpu_count;
            options->compression_level = 1;
            options->small_file_threshold = 1024 * 1024u;
        } else if (total_size > 2ull * 1024ull * 1024ull * 1024ull) {
            options->thread_count = options->detected_cpu_count;
            options->compression_level = 4;
            options->small_file_threshold = 256 * 1024u;
        }
    }

    if ((size_t) options->thread_count > source_count) {
        options->thread_count = (int) source_count;
    }
    if (options->thread_count < 1) {
        options->thread_count = 1;
    }
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
