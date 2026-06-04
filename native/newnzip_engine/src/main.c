#include "common.h"
#include "archive_adapter.h"
#include "benchmark.h"
#include "capabilities.h"
#include "sevenzip_reader.h"
#include "tar_reader.h"
#include "tar_writer.h"
#include "zip_reader.h"
#include "zip_writer.h"

static bool suffix_equals_ignore_case(const char *value, const char *suffix) {
    size_t value_length = strlen(value);
    size_t suffix_length = strlen(suffix);
    if (suffix_length > value_length) {
        return false;
    }
    return strcasecmp(value + value_length - suffix_length, suffix) == 0;
}

static bool is_native_tar_create_format(const char *format) {
    return strcasecmp(format, "tar") == 0 ||
           strcasecmp(format, "tar.gz") == 0 ||
           strcasecmp(format, "tgz") == 0 ||
           strcasecmp(format, "tar.bz2") == 0 ||
           strcasecmp(format, "tbz2") == 0 ||
           strcasecmp(format, "tar.xz") == 0 ||
           strcasecmp(format, "txz") == 0 ||
           strcasecmp(format, "tar.zstd") == 0 ||
           strcasecmp(format, "tar.zst") == 0 ||
           strcasecmp(format, "tzst") == 0;
}

static bool is_native_zip_alias_format(const char *format) {
    return strcasecmp(format, "zip") == 0 || strcasecmp(format, "jar") == 0;
}

static bool is_native_tar_extract_path(const char *archive_path) {
    return suffix_equals_ignore_case(archive_path, ".tar") ||
           suffix_equals_ignore_case(archive_path, ".tar.gz") ||
           suffix_equals_ignore_case(archive_path, ".tgz") ||
           suffix_equals_ignore_case(archive_path, ".tar.bz2") ||
           suffix_equals_ignore_case(archive_path, ".tbz2") ||
           suffix_equals_ignore_case(archive_path, ".tar.xz") ||
           suffix_equals_ignore_case(archive_path, ".txz") ||
           suffix_equals_ignore_case(archive_path, ".tar.zstd") ||
           suffix_equals_ignore_case(archive_path, ".tar.zst") ||
           suffix_equals_ignore_case(archive_path, ".tzst");
}

static void print_usage(void) {
    fprintf(stderr, "사용법:\n");
    fprintf(stderr, "  newnzip-engine list archive.zip\n");
    fprintf(stderr, "  newnzip-engine create output.zip <파일-또는-폴더>...\n");
    fprintf(stderr, "  newnzip-engine extract archive.zip destination\n");
    fprintf(stderr, "  newnzip-engine benchmark output.zip <파일-또는-폴더>...\n");
    fprintf(stderr, "  newnzip-engine capabilities\n");
    fprintf(stderr, "선택 옵션:\n");
    fprintf(stderr, "  --format=zip|7z|tar|tar.gz|tar.bz2|tar.xz|tar.zstd|zstd|lz4|brotli|wim  압축 형식 지정\n");
    fprintf(stderr, "  --method=deflate|store|auto  ZIP 압축 방식 지정\n");
    fprintf(stderr, "  --split=100m  ZIP 분할 크기 지정\n");
    fprintf(stderr, "  --threads=N  병렬 처리 스레드 수 지정\n");
    fprintf(stderr, "  --mode=auto|balanced|max|low-memory  성능 모드 지정\n");
    fprintf(stderr, "  --password=암호  ZIP/7Z 암호 지정 (ZIP은 네이티브, 7Z는 backend 필요)\n");
}

static bool parse_option(const char *argument, RuntimeOptions *options) {
    if (strncmp(argument, "--threads=", 10) == 0) {
        options->thread_count = parse_thread_argument(argument + 10, options);
        return true;
    }
    if (strncmp(argument, "--mode=", 7) == 0) {
        apply_performance_mode(options, argument + 7);
        return true;
    }
    if (strncmp(argument, "--format=", 9) == 0) {
        options->archive_format = argument + 9;
        return true;
    }
    if (strncmp(argument, "--method=", 9) == 0) {
        options->zip_method = argument + 9;
        return true;
    }
    if (strncmp(argument, "--split=", 8) == 0) {
        options->split_size = parse_size_argument(argument + 8);
        return true;
    }
    if (strncmp(argument, "--password=", 11) == 0) {
        options->password = argument + 11;
        return true;
    }
    if (strncmp(argument, "--zip-encryption=", 17) == 0) {
        options->zip_encryption_mode = argument + 17;
        return true;
    }
    return false;
}

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "capabilities") == 0) {
        command_capabilities();
        return 0;
    }

    if (argc < 3) {
        print_usage();
        return 1;
    }

    RuntimeOptions options = default_runtime_options();
    const char *command = NULL;
    char **positionals = calloc((size_t) argc, sizeof(char *));
    if (!positionals) {
        fail("메모리가 부족합니다");
    }
    int positional_count = 0;

    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--", 2) == 0) {
            if (!parse_option(argv[i], &options)) {
                fail("지원하지 않는 옵션입니다");
            }
            continue;
        }
        if (!command) {
            command = argv[i];
        } else {
            positionals[positional_count++] = argv[i];
        }
    }

    if (!command) {
        print_usage();
        free(positionals);
        return 1;
    }

    char **command_argv = calloc((size_t) positional_count + 3, sizeof(char *));
    if (!command_argv) {
        fail("메모리가 부족합니다");
    }
    command_argv[0] = argv[0];
    command_argv[1] = (char *) command;
    for (int i = 0; i < positional_count; i++) {
        command_argv[i + 2] = positionals[i];
    }
    int command_argc = positional_count + 2;
    free(positionals);

    if (strcmp(command, "list") == 0) {
        if (positional_count < 1) {
            fail("사용법: newnzip-engine list archive.zip");
        }
        if (sevenzip_has_signature(command_argv[2])) {
            command_list_7z_native(command_argv[2]);
        } else {
            command_list(command_argv[2]);
        }
        free(command_argv);
        return 0;
    }
    if (strcmp(command, "create") == 0) {
        if (options.password && *options.password &&
            !is_native_zip_alias_format(options.archive_format) &&
            strcmp(options.archive_format, "7z") != 0) {
            fail("암호 압축은 현재 zip 또는 7z 형식만 지원합니다");
        }
        if (is_native_zip_alias_format(options.archive_format)) {
            command_create(command_argc, command_argv, &options);
            free(command_argv);
            return 0;
        }
        if (is_native_tar_create_format(options.archive_format)) {
            if (strcasecmp(options.archive_format, "tar") == 0) {
                command_create_tar(command_argc, command_argv, &options);
            } else if (strcasecmp(options.archive_format, "tar.gz") == 0 || strcasecmp(options.archive_format, "tgz") == 0) {
                command_create_targz(command_argc, command_argv, &options);
            } else if (strcasecmp(options.archive_format, "tar.bz2") == 0 || strcasecmp(options.archive_format, "tbz2") == 0) {
                command_create_tarbz2(command_argc, command_argv, &options);
            } else if (strcasecmp(options.archive_format, "tar.xz") == 0 || strcasecmp(options.archive_format, "txz") == 0) {
                command_create_tarxz(command_argc, command_argv, &options);
            } else {
                command_create_tarzstd(command_argc, command_argv, &options);
            }
            free(command_argv);
            return 0;
        }
        {
            adapter_create(command_argc, command_argv, &options);
            free(command_argv);
            return 0;
        }
    }
    if (strcmp(command, "extract") == 0) {
        if (positional_count < 2) {
            fail("사용법: newnzip-engine extract archive.zip destination");
        }
        const char *archive_path = command_argv[2];
        size_t archive_length = strlen(archive_path);
        if (archive_length > 4 && strcmp(archive_path + archive_length - 4, ".001") == 0) {
            command_extract_split(archive_path, command_argv[3], &options);
        } else if (!options.password && sevenzip_has_signature(archive_path)) {
            command_extract_7z_native(command_argv[2], command_argv[3], &options);
        } else if (options.password && *options.password &&
                   strcmp(strrchr(archive_path, '.') ? strrchr(archive_path, '.') + 1 : "", "zip") != 0) {
            adapter_extract(command_argv[2], command_argv[3], &options);
        } else if (is_native_tar_extract_path(archive_path)) {
            if (suffix_equals_ignore_case(archive_path, ".tar")) {
                command_extract_tar(command_argv[2], command_argv[3], &options);
            } else if (suffix_equals_ignore_case(archive_path, ".tar.gz") || suffix_equals_ignore_case(archive_path, ".tgz")) {
                command_extract_targz(command_argv[2], command_argv[3], &options);
            } else if (suffix_equals_ignore_case(archive_path, ".tar.bz2") || suffix_equals_ignore_case(archive_path, ".tbz2")) {
                command_extract_tarbz2(command_argv[2], command_argv[3], &options);
            } else if (suffix_equals_ignore_case(archive_path, ".tar.xz") || suffix_equals_ignore_case(archive_path, ".txz")) {
                command_extract_tarxz(command_argv[2], command_argv[3], &options);
            } else {
                command_extract_tarzstd(command_argv[2], command_argv[3], &options);
            }
        } else if (adapter_can_extract(archive_path)) {
            adapter_extract(command_argv[2], command_argv[3], &options);
        } else {
            command_extract(command_argv[2], command_argv[3], &options);
        }
        free(command_argv);
        return 0;
    }
    if (strcmp(command, "benchmark") == 0) {
        command_benchmark(command_argc, command_argv, &options);
        free(command_argv);
        return 0;
    }

    free(command_argv);
    fail("알 수 없는 명령입니다");
    return 1;
}
