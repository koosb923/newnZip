#ifndef NEWNZIP_ZIP_READER_H
#define NEWNZIP_ZIP_READER_H

void command_list(const char *archive_path);
void command_extract(const char *archive_path, const char *destination, const RuntimeOptions *options);
void command_extract_split(const char *archive_path, const char *destination, const RuntimeOptions *options);

#endif
