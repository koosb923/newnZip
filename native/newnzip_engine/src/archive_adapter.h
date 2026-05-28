#ifndef NEWNZIP_ARCHIVE_ADAPTER_H
#define NEWNZIP_ARCHIVE_ADAPTER_H

bool adapter_can_create(const char *format);
bool adapter_can_extract(const char *archive_path);
void adapter_create(int argc, char **argv, const RuntimeOptions *options);
void adapter_extract(const char *archive_path, const char *destination);

#endif
