#include "common.h"
#include "benchmark.h"
#include "zip_reader.h"
#include "zip_writer.h"

static void print_usage(void) {
    fprintf(stderr, "사용법:\n");
    fprintf(stderr, "  newnzip-engine list archive.zip\n");
    fprintf(stderr, "  newnzip-engine create output.zip <파일-또는-폴더>...\n");
    fprintf(stderr, "  newnzip-engine extract archive.zip destination\n");
    fprintf(stderr, "  newnzip-engine benchmark output.zip <파일-또는-폴더>...\n");
    fprintf(stderr, "선택 옵션:\n");
    fprintf(stderr, "  --threads=N  병렬 처리 스레드 수 지정\n");
}

int main(int argc, char **argv) {
    if (argc < 3) {
        print_usage();
        return 1;
    }

    RuntimeOptions options = default_runtime_options();
    int command_index = 1;
    if (strncmp(argv[1], "--threads=", 10) == 0) {
        options.thread_count = parse_thread_argument(argv[1] + 10, &options);
        command_index = 2;
    }
    if (argc <= command_index + 1) {
        print_usage();
        return 1;
    }

    if (strcmp(argv[command_index], "list") == 0) {
        command_list(argv[command_index + 1]);
        return 0;
    }
    if (strcmp(argv[command_index], "create") == 0) {
        command_create(argc - command_index + 1, argv + command_index - 1, &options);
        return 0;
    }
    if (strcmp(argv[command_index], "extract") == 0) {
        if (argc - command_index < 3) {
            fail("사용법: newnzip-engine extract archive.zip destination");
        }
        command_extract(argv[command_index + 1], argv[command_index + 2], &options);
        return 0;
    }
    if (strcmp(argv[command_index], "benchmark") == 0) {
        command_benchmark(argc - command_index + 1, argv + command_index - 1, &options);
        return 0;
    }

    fail("알 수 없는 명령입니다");
    return 1;
}
