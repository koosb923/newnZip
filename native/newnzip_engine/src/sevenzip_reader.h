#ifndef NEWNZIP_SEVENZIP_READER_H
#define NEWNZIP_SEVENZIP_READER_H

#include "common.h"

typedef struct {
    uint8_t major_version;
    uint8_t minor_version;
    uint64_t next_header_offset;
    uint64_t next_header_size;
    uint32_t next_header_crc;
} SevenZipStartHeader;

bool sevenzip_has_signature(const char *archive_path);
SevenZipStartHeader sevenzip_read_start_header(const char *archive_path);
void command_list_7z_native(const char *archive_path);
void command_extract_7z_native(const char *archive_path, const char *destination, const RuntimeOptions *options);

#endif
