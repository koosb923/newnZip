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
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <zlib.h>

#define ZIP_LOCAL_FILE_HEADER 0x04034b50u
#define ZIP_CENTRAL_DIRECTORY_HEADER 0x02014b50u
#define ZIP_END_OF_CENTRAL_DIRECTORY 0x06054b50u
#define ZIP_VERSION 20u
#define ZIP_METHOD_STORE 0u
#define ZIP_METHOD_DEFLATE 8u
#define CHUNK_SIZE 65536u

typedef struct {
    char *path;
    char *archive_name;
    uint64_t size;
} SourceEntry;

typedef struct {
    char *name;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint32_t local_header_offset;
    uint16_t method;
    uint16_t mod_time;
    uint16_t mod_date;
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
    const char *performance_mode;
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
uint16_t read_u16(const unsigned char *bytes);
uint32_t read_u32(const unsigned char *bytes);
char *duplicate_string(const char *value);
char *join_path(const char *left, const char *right);
char *normalize_archive_name(const char *name);
void ensure_parent_directories(const char *path);
void central_list_push(CentralList *list, CentralEntry entry);
void free_central_list(CentralList *list);
void collect_sources(const char *path, const char *archive_root, SourceEntry **items, size_t *count, size_t *capacity);
void free_sources(SourceEntry *items, size_t count);
double monotonic_seconds(void);
int detect_cpu_count(void);
RuntimeOptions default_runtime_options(void);
int parse_thread_argument(const char *value, const RuntimeOptions *defaults);
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
