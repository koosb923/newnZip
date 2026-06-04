#ifndef NEWNZIP_ZIP_AES_H
#define NEWNZIP_ZIP_AES_H

#include "common.h"

typedef struct {
    const char *password;
    uint8_t strength;
    uint16_t actual_method;
} ZipAesOptions;

typedef struct ZipAesContext ZipAesContext;

bool zip_aes_mode_enabled(const RuntimeOptions *options);
void zip_aes_not_implemented(void);
uint16_t zip_aes_extra_field_length(void);
void zip_aes_write_extra_field(FILE *output, uint16_t actual_method);
bool zip_aes_apply_extra_field(CentralEntry *entry, const unsigned char *extra, uint16_t data_size);
size_t zip_aes_salt_length(uint8_t strength);
size_t zip_aes_auth_code_length(void);
ZipAesContext *zip_aes_create_encryptor(const char *password, uint8_t strength, unsigned char *salt_out, unsigned char password_verifier[2]);
ZipAesContext *zip_aes_create_decryptor(const char *password, uint8_t strength, const unsigned char *salt, const unsigned char password_verifier[2]);
void zip_aes_crypt(ZipAesContext *context, unsigned char *buffer, size_t size);
void zip_aes_auth_update(ZipAesContext *context, const unsigned char *encrypted, size_t size);
void zip_aes_finalize_auth(ZipAesContext *context, unsigned char auth_code[10]);
void zip_aes_destroy(ZipAesContext *context);

#endif
