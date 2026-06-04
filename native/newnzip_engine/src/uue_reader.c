#include "uue_reader.h"

static bool uue_has_suffix_ignore_case(const char *value, const char *suffix) {
    size_t value_length = strlen(value);
    size_t suffix_length = strlen(suffix);
    if (suffix_length > value_length) {
        return false;
    }
    return strcasecmp(value + value_length - suffix_length, suffix) == 0;
}

static unsigned char uue_decode_char(unsigned char ch) {
    return ch == '`' ? 0u : (unsigned char) ((ch - 32u) & 0x3fu);
}

static bool uue_read_line(FILE *input, char *buffer, size_t size) {
    return fgets(buffer, (int) size, input) != NULL;
}

static void uue_trim_newline(char *line) {
    size_t length = strlen(line);
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        line[length - 1] = '\0';
        length -= 1;
    }
}

bool uue_can_extract(const char *archive_path) {
    return uue_has_suffix_ignore_case(archive_path, ".uue") ||
           uue_has_suffix_ignore_case(archive_path, ".uu");
}

void command_extract_uue(const char *archive_path, const char *destination, const RuntimeOptions *options) {
    (void) options;

    FILE *input = fopen(archive_path, "rb");
    if (!input) {
        fail_errno(archive_path);
    }

    char line[4096];
    bool found_begin = false;
    char file_name[PATH_MAX] = {0};

    while (uue_read_line(input, line, sizeof(line))) {
        uue_trim_newline(line);
        if (strncmp(line, "begin ", 6) == 0) {
            char mode[8] = {0};
            if (sscanf(line, "begin %7s %1023s", mode, file_name) == 2) {
                found_begin = true;
                break;
            }
        }
    }

    if (!found_begin) {
        fclose(input);
        fail("UUE 헤더(begin)를 찾지 못했습니다");
    }

    char *output_path = join_path(destination, file_name);
    ensure_parent_directories(output_path);
    FILE *output = fopen(output_path, "wb");
    if (!output) {
        free(output_path);
        fclose(input);
        fail_errno(output_path);
    }

    ProgressState progress;
    progress_state_init(&progress, 1);

    while (uue_read_line(input, line, sizeof(line))) {
        uue_trim_newline(line);
        if (strcmp(line, "end") == 0) {
            break;
        }
        if (line[0] == '\0') {
            continue;
        }

        size_t expected = (size_t) uue_decode_char((unsigned char) line[0]);
        if (expected == 0) {
            continue;
        }

        unsigned char decoded[3];
        size_t produced = 0;
        const unsigned char *cursor = (const unsigned char *) (line + 1);
        while (*cursor != '\0' && produced < expected) {
            unsigned char a = uue_decode_char(cursor[0]);
            unsigned char b = cursor[1] ? uue_decode_char(cursor[1]) : 0u;
            unsigned char c = cursor[2] ? uue_decode_char(cursor[2]) : 0u;
            unsigned char d = cursor[3] ? uue_decode_char(cursor[3]) : 0u;

            decoded[0] = (unsigned char) ((a << 2) | (b >> 4));
            decoded[1] = (unsigned char) ((b << 4) | (c >> 2));
            decoded[2] = (unsigned char) ((c << 6) | d);

            size_t chunk = expected - produced;
            if (chunk > 3u) {
                chunk = 3u;
            }
            if (fwrite(decoded, 1, chunk, output) != chunk) {
                free(output_path);
                fclose(output);
                fclose(input);
                fail_errno("UUE 출력 파일을 기록하지 못했습니다");
            }
            produced += chunk;
            cursor += 4;
        }
    }

    progress_step(&progress, "extract", file_name);
    progress_state_destroy(&progress);
    fclose(output);
    fclose(input);
    printf("해제 완료: %s -> %s (uue native)\n", archive_path, output_path);
    free(output_path);
}
