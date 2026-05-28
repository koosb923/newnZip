#include "benchmark.h"
#include "zip_reader.h"
#include "zip_writer.h"

static char *create_temp_directory(void) {
    const char *base = getenv("TMPDIR");
    if (!base || !*base) {
        base = "/tmp";
    }

    size_t length = strlen(base) + 24;
    char *pattern = malloc(length);
    if (!pattern) {
        fail("메모리가 부족합니다");
    }

    snprintf(pattern, length, "%s/%s", base, "newnzip-benchmark-XXXXXX");
    if (!mkdtemp(pattern)) {
        free(pattern);
        fail_errno("임시 폴더를 만들지 못했습니다");
    }
    return pattern;
}

static uint64_t total_source_size(SourceEntry *sources, size_t count) {
    uint64_t total = 0;
    for (size_t i = 0; i < count; i++) {
        struct stat item_stat;
        if (stat(sources[i].path, &item_stat) != 0) {
            fail_errno(sources[i].path);
        }
        total += (uint64_t) item_stat.st_size;
    }
    return total;
}

void command_benchmark(int argc, char **argv, const RuntimeOptions *options) {
    if (argc < 4) {
        fail("사용법: newnzip-engine benchmark output.zip <파일-또는-폴더>...");
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
        free_sources(sources, source_count);
        fail("벤치마크 대상 파일이 없습니다");
    }

    uint64_t input_bytes = total_source_size(sources, source_count);
    free_sources(sources, source_count);

    double create_start = monotonic_seconds();
    command_create(argc, argv, options);
    double create_elapsed = monotonic_seconds() - create_start;

    char *extract_dir = create_temp_directory();
    double extract_start = monotonic_seconds();
    command_extract(argv[2], extract_dir, options);
    double extract_elapsed = monotonic_seconds() - extract_start;

    double input_mb = (double) input_bytes / (1024.0 * 1024.0);
    double create_speed = create_elapsed > 0.0 ? input_mb / create_elapsed : 0.0;
    double extract_speed = extract_elapsed > 0.0 ? input_mb / extract_elapsed : 0.0;

    printf("벤치마크 요약\n");
    printf("입력 크기: %.2f MiB\n", input_mb);
    printf("압축 시간: %.3f초, 처리량: %.2f MiB/s\n", create_elapsed, create_speed);
    printf("해제 시간: %.3f초, 처리량: %.2f MiB/s\n", extract_elapsed, extract_speed);
    printf("사용 스레드: %d\n", options->thread_count);

    remove_tree(extract_dir);
    free(extract_dir);
}
