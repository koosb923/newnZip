#ifndef NEWNZIP_TAR_READER_H
#define NEWNZIP_TAR_READER_H

#include "common.h"

void command_extract_tar(const char *archive_path, const char *destination, const RuntimeOptions *options);
void command_extract_targz(const char *archive_path, const char *destination, const RuntimeOptions *options);
void command_extract_tarbz2(const char *archive_path, const char *destination, const RuntimeOptions *options);
void command_extract_tarxz(const char *archive_path, const char *destination, const RuntimeOptions *options);
void command_extract_tarzstd(const char *archive_path, const char *destination, const RuntimeOptions *options);

#endif
