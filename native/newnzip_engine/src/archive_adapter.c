#include "common.h"
#include "archive_adapter.h"
#include "uue_reader.h"

#include <fcntl.h>
#include <sys/wait.h>

typedef enum {
    STREAM_NONE,
    STREAM_GZIP,
    STREAM_BZIP2,
    STREAM_XZ,
    STREAM_ZSTD,
    STREAM_LZ4,
    STREAM_BROTLI
} StreamKind;

static bool equals_ignore_case(const char *left, const char *right) {
    return strcasecmp(left, right) == 0;
}

static bool has_suffix_ignore_case(const char *value, const char *suffix) {
    size_t value_length = strlen(value);
    size_t suffix_length = strlen(suffix);
    if (suffix_length > value_length) {
        return false;
    }
    return strcasecmp(value + value_length - suffix_length, suffix) == 0;
}

static bool is_executable_file(const char *path) {
    return path && access(path, X_OK) == 0;
}

static char *join_executable_path(const char *directory, const char *name) {
    size_t length = strlen(directory) + strlen(name) + 2;
    char *path = malloc(length);
    if (!path) {
        fail("메모리가 부족합니다");
    }
    snprintf(path, length, "%s/%s", directory, name);
    return path;
}

static char *find_tool(const char *primary, const char *fallback) {
    const char *backend_dir = getenv("NEWNZIP_BACKEND_DIR");
    if (backend_dir && *backend_dir) {
        char *candidate = join_executable_path(backend_dir, primary);
        if (is_executable_file(candidate)) {
            return candidate;
        }
        free(candidate);
        if (fallback) {
            candidate = join_executable_path(backend_dir, fallback);
            if (is_executable_file(candidate)) {
                return candidate;
            }
            free(candidate);
        }
    }

    const char *path_value = getenv("PATH");
    if (!path_value || !*path_value) {
        return NULL;
    }
    char *path_copy = duplicate_string(path_value);
    char *cursor = path_copy;
    while (cursor) {
        char *next = strchr(cursor, ':');
        if (next) {
            *next = '\0';
        }
        if (*cursor) {
            char *candidate = join_executable_path(cursor, primary);
            if (is_executable_file(candidate)) {
                free(path_copy);
                return candidate;
            }
            free(candidate);
            if (fallback) {
                candidate = join_executable_path(cursor, fallback);
                if (is_executable_file(candidate)) {
                    free(path_copy);
                    return candidate;
                }
                free(candidate);
            }
        }
        cursor = next ? next + 1 : NULL;
    }
    free(path_copy);
    return NULL;
}

static bool is_tar_format(const char *format) {
    return equals_ignore_case(format, "tar") ||
           equals_ignore_case(format, "tar.gz") ||
           equals_ignore_case(format, "tgz") ||
           equals_ignore_case(format, "tar.bz2") ||
           equals_ignore_case(format, "tbz2") ||
           equals_ignore_case(format, "tar.xz") ||
           equals_ignore_case(format, "txz") ||
           equals_ignore_case(format, "tar.zstd") ||
           equals_ignore_case(format, "tar.zst") ||
           equals_ignore_case(format, "tzst");
}

static bool is_external_archive_format(const char *format) {
    return equals_ignore_case(format, "zip") ||
           equals_ignore_case(format, "jar") ||
           equals_ignore_case(format, "7z") ||
           equals_ignore_case(format, "wim");
}

static bool is_stream_create_format(const char *format) {
    return equals_ignore_case(format, "gz") ||
           equals_ignore_case(format, "gzip") ||
           equals_ignore_case(format, "bz2") ||
           equals_ignore_case(format, "bzip2") ||
           equals_ignore_case(format, "z") ||
           equals_ignore_case(format, "zstd") ||
           equals_ignore_case(format, "zst") ||
           equals_ignore_case(format, "lz4") ||
           equals_ignore_case(format, "brotli") ||
           equals_ignore_case(format, "br");
}

static bool is_standalone_gzip(const char *archive_path) {
    return has_suffix_ignore_case(archive_path, ".gz") &&
           !has_suffix_ignore_case(archive_path, ".tar.gz") &&
           !has_suffix_ignore_case(archive_path, ".tgz");
}

static bool is_standalone_bzip2(const char *archive_path) {
    return has_suffix_ignore_case(archive_path, ".bz2") &&
           !has_suffix_ignore_case(archive_path, ".tar.bz2") &&
           !has_suffix_ignore_case(archive_path, ".tbz2");
}

static StreamKind stream_kind_for_path(const char *archive_path) {
    if (is_standalone_gzip(archive_path)) {
        return STREAM_GZIP;
    }
    if (is_standalone_bzip2(archive_path)) {
        return STREAM_BZIP2;
    }
    if (has_suffix_ignore_case(archive_path, ".xz")) {
        return STREAM_XZ;
    }
    if (has_suffix_ignore_case(archive_path, ".zst") ||
        has_suffix_ignore_case(archive_path, ".zstd")) {
        return STREAM_ZSTD;
    }
    if (has_suffix_ignore_case(archive_path, ".lz4")) {
        return STREAM_LZ4;
    }
    if (has_suffix_ignore_case(archive_path, ".br") ||
        has_suffix_ignore_case(archive_path, ".brotli")) {
        return STREAM_BROTLI;
    }
    if (has_suffix_ignore_case(archive_path, ".z")) {
        return STREAM_GZIP;
    }
    return STREAM_NONE;
}

static char *stream_decompressor_for_kind(StreamKind kind) {
    switch (kind) {
        case STREAM_GZIP:
            return find_tool("gzip", NULL);
        case STREAM_BZIP2:
            return find_tool("bzip2", NULL);
        case STREAM_XZ:
            return find_tool("xz", NULL);
        case STREAM_ZSTD:
            return find_tool("zstd", NULL);
        case STREAM_LZ4:
            return find_tool("lz4", NULL);
        case STREAM_BROTLI:
            return find_tool("brotli", NULL);
        case STREAM_NONE:
            return NULL;
    }
    return NULL;
}

static char *stream_compressor_for_format(const char *format) {
    if (equals_ignore_case(format, "gz") || equals_ignore_case(format, "gzip") ||
        equals_ignore_case(format, "z")) {
        return find_tool("gzip", NULL);
    }
    if (equals_ignore_case(format, "bz2") || equals_ignore_case(format, "bzip2")) {
        return find_tool("bzip2", NULL);
    }
    if (equals_ignore_case(format, "zstd") || equals_ignore_case(format, "zst")) {
        return find_tool("zstd", NULL);
    }
    if (equals_ignore_case(format, "lz4")) {
        return find_tool("lz4", NULL);
    }
    if (equals_ignore_case(format, "brotli") || equals_ignore_case(format, "br")) {
        return find_tool("brotli", NULL);
    }
    return NULL;
}

static char *stream_output_name(const char *archive_path) {
    const char *name = strrchr(archive_path, '/');
    name = name ? name + 1 : archive_path;

    size_t length = strlen(name);
    if (is_standalone_gzip(archive_path)) {
        length -= 3;
    } else if (is_standalone_bzip2(archive_path)) {
        length -= 4;
    } else if (has_suffix_ignore_case(archive_path, ".xz")) {
        length -= 3;
    } else if (has_suffix_ignore_case(archive_path, ".zst")) {
        length -= 4;
    } else if (has_suffix_ignore_case(archive_path, ".zstd")) {
        length -= 5;
    } else if (has_suffix_ignore_case(archive_path, ".lz4")) {
        length -= 4;
    } else if (has_suffix_ignore_case(archive_path, ".br")) {
        length -= 3;
    } else if (has_suffix_ignore_case(archive_path, ".brotli")) {
        length -= 7;
    } else if (has_suffix_ignore_case(archive_path, ".z")) {
        length -= 2;
    }

    if (length == 0) {
        return duplicate_string("decompressed");
    }

    char *output = malloc(length + 1);
    if (!output) {
        fail("메모리가 부족합니다");
    }
    memcpy(output, name, length);
    output[length] = '\0';
    return output;
}

static const char *tar_compression_flag(const char *format) {
    if (equals_ignore_case(format, "tar.gz") || equals_ignore_case(format, "tgz")) {
        return "-czf";
    }
    if (equals_ignore_case(format, "tar.bz2") || equals_ignore_case(format, "tbz2")) {
        return "-cjf";
    }
    if (equals_ignore_case(format, "tar.xz") || equals_ignore_case(format, "txz")) {
        return "-cJf";
    }
    if (equals_ignore_case(format, "tar.zstd") ||
        equals_ignore_case(format, "tar.zst") ||
        equals_ignore_case(format, "tzst")) {
        return "--zstd";
    }
    return "-cf";
}

static bool is_libarchive_extract_path(const char *archive_path) {
    return has_suffix_ignore_case(archive_path, ".tar") ||
           has_suffix_ignore_case(archive_path, ".tar.gz") ||
           has_suffix_ignore_case(archive_path, ".tgz") ||
           has_suffix_ignore_case(archive_path, ".tar.bz2") ||
           has_suffix_ignore_case(archive_path, ".tbz2") ||
           has_suffix_ignore_case(archive_path, ".tar.xz") ||
           has_suffix_ignore_case(archive_path, ".txz") ||
           has_suffix_ignore_case(archive_path, ".tar.zst") ||
           has_suffix_ignore_case(archive_path, ".tar.zstd") ||
           has_suffix_ignore_case(archive_path, ".tzst") ||
           has_suffix_ignore_case(archive_path, ".cab") ||
           has_suffix_ignore_case(archive_path, ".iso") ||
           has_suffix_ignore_case(archive_path, ".wim") ||
           has_suffix_ignore_case(archive_path, ".cpio") ||
           has_suffix_ignore_case(archive_path, ".rpm") ||
           has_suffix_ignore_case(archive_path, ".deb") ||
           has_suffix_ignore_case(archive_path, ".udf") ||
           has_suffix_ignore_case(archive_path, ".img");
}

static bool is_sevenzip_extract_path(const char *archive_path) {
    return has_suffix_ignore_case(archive_path, ".7z") ||
           has_suffix_ignore_case(archive_path, ".rar") ||
           has_suffix_ignore_case(archive_path, ".ace") ||
           has_suffix_ignore_case(archive_path, ".arj") ||
           has_suffix_ignore_case(archive_path, ".lzh") ||
           has_suffix_ignore_case(archive_path, ".lha") ||
           has_suffix_ignore_case(archive_path, ".zpaq");
}

static bool is_rar_extract_path(const char *archive_path) {
    return has_suffix_ignore_case(archive_path, ".rar");
}

static bool is_dmg_extract_path(const char *archive_path) {
    return has_suffix_ignore_case(archive_path, ".dmg");
}

static int run_process(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) {
        fail_errno("프로세스를 시작하지 못했습니다");
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        fail_errno("프로세스 종료를 기다리지 못했습니다");
    }
    if (!WIFEXITED(status)) {
        return 1;
    }
    return WEXITSTATUS(status);
}

static void run_sevenzip_create_adapter(int input_count, char **argv, const RuntimeOptions *options) {
    char *sevenzip = find_tool("7zz", "7z");
    if (!sevenzip) {
        fail("ZIP/7Z 생성 backend가 없습니다. 7zz/7z를 설치하거나 NEWNZIP_BACKEND_DIR에 번들하세요");
    }
    size_t extra_args = (options->password && *options->password) ? 2u : 0u;
    char **command = calloc((size_t) input_count + 6u + extra_args, sizeof(char *));
    if (!command) {
        free(sevenzip);
        fail("메모리가 부족합니다");
    }
    command[0] = sevenzip;
    command[1] = "a";
    command[2] = equals_ignore_case(options->archive_format, "zip") ? "-tzip" : "-t7z";
    int next_index = 3;
    if (options->password && *options->password) {
        size_t password_arg_length = strlen(options->password) + 3;
        char *password_arg = malloc(password_arg_length);
        if (!password_arg) {
            free(command);
            free(sevenzip);
            fail("메모리가 부족합니다");
        }
        snprintf(password_arg, password_arg_length, "-p%s", options->password);
        command[next_index++] = password_arg;
        if (equals_ignore_case(options->archive_format, "7z")) {
            command[next_index++] = "-mhe=on";
        }
    }
    command[next_index++] = argv[2];
    for (int i = 0; i < input_count; i++) {
        command[next_index + i] = argv[3 + i];
    }
    command[next_index + input_count] = NULL;
    int exit_code = run_process(command);
    if (options->password && *options->password) {
        free(command[3]);
    }
    free(command);
    free(sevenzip);
    if (exit_code != 0) {
        fail("ZIP/7Z adapter 생성에 실패했습니다");
    }
    printf("생성 완료: %s (%s adapter)\n", argv[2], options->archive_format);
}

static void run_sevenzip_extract_adapter(const char *archive_path, const char *destination, const RuntimeOptions *options) {
    char *sevenzip = find_tool("7zz", "7z");
    if (!sevenzip) {
        fail("7Z/RAR 계열 해제 backend가 없습니다. 7zz/7z를 설치하거나 NEWNZIP_BACKEND_DIR에 번들하세요");
    }

    size_t output_arg_length = strlen(destination) + 4;
    char *output_arg = malloc(output_arg_length);
    if (!output_arg) {
        free(sevenzip);
        fail("메모리가 부족합니다");
    }
    snprintf(output_arg, output_arg_length, "-o%s", destination);

    char *command[7] = { sevenzip, "x", "-y", output_arg, (char *) archive_path, NULL, NULL };
    if (options && options->password && *options->password) {
        size_t password_arg_length = strlen(options->password) + 3;
        char *password_arg = malloc(password_arg_length);
        if (!password_arg) {
            free(output_arg);
            free(sevenzip);
            fail("메모리가 부족합니다");
        }
        snprintf(password_arg, password_arg_length, "-p%s", options->password);
        command[2] = password_arg;
        command[3] = "-y";
        command[4] = output_arg;
        command[5] = (char *) archive_path;
        command[6] = NULL;
    }

    int exit_code = run_process(command);
    if (options && options->password && *options->password) {
        free(command[2]);
    }
    free(output_arg);
    free(sevenzip);
    if (exit_code != 0) {
        if (is_rar_extract_path(archive_path)) {
            fail("RAR 해제에 실패했습니다. 비밀번호 오류, 손상된 파일, 또는 지원하지 않는 RAR 버전일 수 있습니다");
        }
        if (options && options->password && *options->password) {
            fail("압축 해제에 실패했습니다. 비밀번호가 올바르지 않거나 지원하지 않는 방식일 수 있습니다");
        }
        fail("7Z 계열 해제에 실패했습니다. 손상된 파일이거나 지원하지 않는 방식일 수 있습니다");
    }
    printf("해제 완료: %s -> %s (7z adapter)\n", archive_path, destination);
}

static int run_process_to_file(char *const argv[], const char *output_path) {
    int output_fd = open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (output_fd < 0) {
        fail_errno(output_path);
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(output_fd);
        fail_errno("프로세스를 시작하지 못했습니다");
    }
    if (pid == 0) {
        if (dup2(output_fd, STDOUT_FILENO) < 0) {
            fprintf(stderr, "stdout: %s\n", strerror(errno));
            _exit(127);
        }
        close(output_fd);
        execvp(argv[0], argv);
        fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    close(output_fd);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        fail_errno("프로세스 종료를 기다리지 못했습니다");
    }
    if (!WIFEXITED(status)) {
        return 1;
    }
    return WEXITSTATUS(status);
}

static void run_dmg_extract_adapter(const char *archive_path, const char *destination) {
#if defined(__APPLE__)
    char *mount_path = create_temp_path("newnzip-dmg-mount-");
    remove_file_if_exists(mount_path);
    if (mkdir(mount_path, 0755) != 0 && errno != EEXIST) {
        free(mount_path);
        fail_errno(mount_path);
    }

    char *const attach_command[] = {
        "hdiutil",
        "attach",
        "-readonly",
        "-nobrowse",
        "-mountpoint",
        mount_path,
        (char *) archive_path,
        NULL
    };
    int attach_code = run_process(attach_command);
    if (attach_code != 0) {
        remove_tree(mount_path);
        free(mount_path);
        fail("DMG 마운트에 실패했습니다");
    }

    char *const copy_command[] = {
        "ditto",
        mount_path,
        (char *) destination,
        NULL
    };
    int copy_code = run_process(copy_command);

    char *const detach_command[] = {
        "hdiutil",
        "detach",
        mount_path,
        NULL
    };
    int detach_code = run_process(detach_command);
    remove_tree(mount_path);
    free(mount_path);

    if (copy_code != 0) {
        fail("DMG 내용 복사에 실패했습니다");
    }
    if (detach_code != 0) {
        fail("DMG 마운트 해제에 실패했습니다");
    }
    printf("해제 완료: %s -> %s (dmg adapter)\n", archive_path, destination);
#else
    (void) archive_path;
    (void) destination;
    fail("DMG 해제는 현재 macOS에서만 지원합니다");
#endif
}

bool adapter_can_create(const char *format) {
    return is_tar_format(format) ||
           is_external_archive_format(format) ||
           is_stream_create_format(format);
}

bool adapter_can_extract(const char *archive_path) {
    return is_libarchive_extract_path(archive_path) ||
           is_dmg_extract_path(archive_path) ||
           is_sevenzip_extract_path(archive_path) ||
           uue_can_extract(archive_path) ||
           stream_kind_for_path(archive_path) != STREAM_NONE;
}

void adapter_create(int argc, char **argv, const RuntimeOptions *options) {
    if (argc < 4) {
        fail("사용법: newnzip-engine --format=tar.gz create output.tar.gz <파일-또는-폴더>...");
    }
    if (!adapter_can_create(options->archive_format)) {
        fail("이 형식은 아직 생성 backend가 없습니다");
    }

    int input_count = argc - 3;
    if (is_external_archive_format(options->archive_format)) {
        if (equals_ignore_case(options->archive_format, "zip") ||
            equals_ignore_case(options->archive_format, "7z")) {
            run_sevenzip_create_adapter(input_count, argv, options);
            return;
        }
        if (equals_ignore_case(options->archive_format, "wim")) {
            char *wimlib = find_tool("wimlib-imagex", NULL);
            if (!wimlib) {
                fail("WIM 생성 backend가 없습니다. wimlib-imagex를 설치하거나 NEWNZIP_BACKEND_DIR에 번들하세요");
            }
            if (input_count != 1) {
                free(wimlib);
                fail("WIM 생성은 현재 입력 폴더 1개만 지원합니다");
            }
            char *const command[] = {
                wimlib,
                "capture",
                argv[3],
                argv[2],
                NULL
            };
            int exit_code = run_process(command);
            free(wimlib);
            if (exit_code != 0) {
                fail("WIM adapter 생성에 실패했습니다");
            }
            printf("생성 완료: %s (wim adapter)\n", argv[2]);
            return;
        }
    }

    if (is_stream_create_format(options->archive_format)) {
        if (input_count != 1) {
            fail("스트림 압축은 현재 단일 파일 입력만 지원합니다");
        }
        char *compressor = stream_compressor_for_format(options->archive_format);
        if (!compressor) {
            fail("stream 생성 backend가 없습니다. 필요한 코덱을 설치하거나 NEWNZIP_BACKEND_DIR에 번들하세요");
        }

        char *const command[] = {
            compressor,
            "-c",
            argv[3],
            NULL
        };
        int exit_code = run_process_to_file(command, argv[2]);
        free(compressor);
        if (exit_code != 0) {
            fail("stream adapter 생성에 실패했습니다");
        }
        printf("생성 완료: %s (%s stream adapter)\n", argv[2], options->archive_format);
        return;
    }

    char **command = calloc((size_t) input_count + 5, sizeof(char *));
    if (!command) {
        fail("메모리가 부족합니다");
    }

    command[0] = "bsdtar";
    if (equals_ignore_case(tar_compression_flag(options->archive_format), "--zstd")) {
        command[1] = "-cf";
        command[2] = argv[2];
        command[3] = "--zstd";
        for (int i = 0; i < input_count; i++) {
            command[4 + i] = argv[3 + i];
        }
        command[4 + input_count] = NULL;
    } else {
        command[1] = (char *) tar_compression_flag(options->archive_format);
        command[2] = argv[2];
        for (int i = 0; i < input_count; i++) {
            command[3 + i] = argv[3 + i];
        }
        command[3 + input_count] = NULL;
    }

    int exit_code = run_process(command);
    free(command);
    if (exit_code != 0) {
        fail("archive adapter 생성에 실패했습니다");
    }
    printf("생성 완료: %s (%s adapter)\n", argv[2], options->archive_format);
}

void adapter_extract(const char *archive_path, const char *destination, const RuntimeOptions *options) {
    if (mkdir(destination, 0755) != 0 && errno != EEXIST) {
        fail_errno(destination);
    }

    StreamKind stream_kind = stream_kind_for_path(archive_path);
    char *stream_decompressor = stream_decompressor_for_kind(stream_kind);
    if (stream_decompressor) {
        char *output_name = stream_output_name(archive_path);
        char *output_path = join_path(destination, output_name);
        free(output_name);

        char *const command[] = {
            (char *) stream_decompressor,
            "-dc",
            (char *) archive_path,
            NULL
        };

        int exit_code = run_process_to_file(command, output_path);
        free(stream_decompressor);
        if (exit_code != 0) {
            free(output_path);
            fail("stream adapter 해제에 실패했습니다");
        }
        printf("해제 완료: %s -> %s (stream adapter)\n", archive_path, output_path);
        free(output_path);
        return;
    }
    if (stream_kind != STREAM_NONE) {
        fail("stream 해제 backend가 없습니다. 필요한 코덱을 설치하거나 NEWNZIP_BACKEND_DIR에 번들하세요");
    }

    if ((options && options->password && *options->password && has_suffix_ignore_case(archive_path, ".zip")) ||
        is_sevenzip_extract_path(archive_path)) {
        run_sevenzip_extract_adapter(archive_path, destination, options);
        return;
    }

    if (uue_can_extract(archive_path)) {
        command_extract_uue(archive_path, destination, options);
        return;
    }

    if (is_dmg_extract_path(archive_path)) {
        run_dmg_extract_adapter(archive_path, destination);
        return;
    }

    char *const command[] = {
        "bsdtar",
        "-xf",
        (char *) archive_path,
        "-C",
        (char *) destination,
        NULL
    };

    int exit_code = run_process(command);
    if (exit_code != 0) {
        fail("archive adapter 해제에 실패했습니다");
    }
    printf("해제 완료: %s -> %s (adapter)\n", archive_path, destination);
}
