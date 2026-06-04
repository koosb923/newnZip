#ifndef NEWNZIP_TAR_WRITER_H
#define NEWNZIP_TAR_WRITER_H

#include "common.h"

void command_create_tar(int argc, char **argv, const RuntimeOptions *options);
void command_create_targz(int argc, char **argv, const RuntimeOptions *options);
void command_create_tarbz2(int argc, char **argv, const RuntimeOptions *options);
void command_create_tarxz(int argc, char **argv, const RuntimeOptions *options);
void command_create_tarzstd(int argc, char **argv, const RuntimeOptions *options);

#endif
