#include "common.h"
#include "archive_adapter.h"
#include "benchmark.h"
#include "capabilities.h"
#include "zip_reader.h"
#include "zip_writer.h"

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
        command_list(command_argv[2]);
        free(command_argv);
        return 0;
    }
    if (strcmp(command, "create") == 0) {
        if (options.password && *options.password &&
            strcmp(options.archive_format, "zip") != 0 &&
            strcmp(options.archive_format, "7z") != 0) {
            fail("암호 압축은 현재 zip 또는 7z 형식만 지원합니다");
        }
        if (strcmp(options.archive_format, "zip") != 0) {
            adapter_create(command_argc, command_argv, &options);
            free(command_argv);
            return 0;
        }
        command_create(command_argc, command_argv, &options);
        free(command_argv);
        return 0;
    }
    if (strcmp(command, "extract") == 0) {
        if (positional_count < 2) {
            fail("사용법: newnzip-engine extract archive.zip destination");
        }
        const char *archive_path = command_argv[2];
        size_t archive_length = strlen(archive_path);
        if (archive_length > 4 && strcmp(archive_path + archive_length - 4, ".001") == 0) {
            command_extract_split(archive_path, command_argv[3], &options);
        } else if (options.password && *options.password &&
                   strcmp(strrchr(archive_path, '.') ? strrchr(archive_path, '.') + 1 : "", "zip") != 0) {
            adapter_extract(command_argv[2], command_argv[3], &options);
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
