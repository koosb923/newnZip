#ifndef NEWNZIP_UUE_READER_H
#define NEWNZIP_UUE_READER_H

#include "common.h"

bool uue_can_extract(const char *archive_path);
void command_extract_uue(const char *archive_path, const char *destination, const RuntimeOptions *options);

#endif
