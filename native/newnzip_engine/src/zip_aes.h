#ifndef NEWNZIP_ZIP_AES_H
#define NEWNZIP_ZIP_AES_H

#include "common.h"

typedef struct {
    const char *password;
    uint8_t strength;
    uint16_t actual_method;
} ZipAesOptions;

bool zip_aes_mode_enabled(const RuntimeOptions *options);
void zip_aes_not_implemented(void);

#endif
