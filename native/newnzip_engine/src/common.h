#ifndef NEWNZIP_COMMON_H
#define NEWNZIP_COMMON_H

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <zlib.h>

#define ZIP_GENERAL_PURPOSE_UTF8 0x0800u
#define ZIP_GENERAL_PURPOSE_ENCRYPTED 0x0001u
#define ZIP_LOCAL_FILE_HEADER 0x04034b50u
#define ZIP_CENTRAL_DIRECTORY_HEADER 0x02014b50u
#define ZIP_END_OF_CENTRAL_DIRECTORY 0x06054b50u
#define ZIP64_END_OF_CENTRAL_DIRECTORY 0x06064b50u
#define ZIP64_END_OF_CENTRAL_DIRECTORY_LOCATOR 0x07064b50u
#define ZIP64_EXTRA_FIELD_ID 0x0001u
#define ZIP_AES_EXTRA_FIELD_ID 0x9901u
#define ZIP_VERSION 20u
#define ZIP64_VERSION 45u
#define ZIP_AES_VERSION 51u
#define ZIP_METHOD_STORE 0u
#define ZIP_METHOD_DEFLATE 8u
#define ZIP_METHOD_AES 99u
#define CHUNK_SIZE 65536u

typedef enum {
    ZIP_ENCRYPTION_NONE = 0,
    ZIP_ENCRYPTION_ZIPCRYPTO = 1,
    ZIP_ENCRYPTION_AES256 = 2
} ZipEncryptionMode;

typedef struct {
    char *path;
    char *archive_name;
    char *link_target;
    uint64_t size;
    mode_t mode;
    bool is_symlink;
} SourceEntry;

typedef struct {
    char *name;
    uint32_t crc32;
    uint64_t compressed_size;
    uint64_t payload_size;
    uint64_t uncompressed_size;
    uint64_t local_header_offset;
    uint32_t external_attributes;
    uint16_t method;
    uint16_t actual_method;
    uint16_t mod_time;
    uint16_t mod_date;
    uint16_t general_purpose_flag;
    bool encrypted;
    ZipEncryptionMode encryption_mode;
    uint8_t aes_strength;
} CentralEntry;

typedef struct {
    CentralEntry *items;
    size_t count;
    size_t capacity;
} CentralList;

typedef struct {
    int detected_cpu_count;
    int thread_count;
    int compression_level;
    size_t chunk_size;
    size_t small_file_threshold;
    uint64_t split_size;
    const char *archive_format;
    const char *zip_method;
    const char *zip_encryption_mode;
    const char *performance_mode;
    const char *password;
} RuntimeOptions;

typedef struct {
    size_t total;
    size_t completed;
    pthread_mutex_t mutex;
} ProgressState;

void fail(const char *message);
void fail_errno(const char *message);
uint16_t dos_time_value(time_t raw_time);
uint16_t dos_date_value(time_t raw_time);
void write_u16(FILE *file, uint16_t value);
void write_u32(FILE *file, uint32_t value);
void write_u64(FILE *file, uint64_t value);
uint16_t read_u16(const unsigned char *bytes);
uint32_t read_u32(const unsigned char *bytes);
uint64_t read_u64(const unsigned char *bytes);
char *duplicate_string(const char *value);
char *join_path(const char *left, const char *right);
char *normalize_archive_name(const char *name);
bool should_exclude_archive_path(const char *path, const char *archive_name);
void ensure_parent_directories(const char *path);
void central_list_push(CentralList *list, CentralEntry entry);
void free_central_list(CentralList *list);
void collect_sources(const char *path, const char *archive_root, SourceEntry **items, size_t *count, size_t *capacity);
void free_sources(SourceEntry *items, size_t count);
double monotonic_seconds(void);
int detect_cpu_count(void);
RuntimeOptions default_runtime_options(void);
int parse_thread_argument(const char *value, const RuntimeOptions *defaults);
uint64_t parse_size_argument(const char *value);
void apply_performance_mode(RuntimeOptions *options, const char *mode_name);
void tune_runtime_for_source_count(RuntimeOptions *options, size_t source_count, uint64_t total_size);
char *create_temp_path(const char *prefix);
void remove_file_if_exists(const char *path);
void remove_tree(const char *path);
void progress_state_init(ProgressState *state, size_t total);
void progress_state_destroy(ProgressState *state);
void progress_step(ProgressState *state, const char *stage, const char *name);
uint64_t total_source_size(const SourceEntry *sources, size_t count);
void sort_sources_by_size_desc(SourceEntry *items, size_t count);

#endif
