#ifndef NEWNZIP_ZIP_CRYPTO_H
#define NEWNZIP_ZIP_CRYPTO_H

#include "common.h"

typedef struct {
    uint32_t key0;
    uint32_t key1;
    uint32_t key2;
} ZipCryptoState;

void zip_crypto_init(ZipCryptoState *state, const char *password);
unsigned char zip_crypto_encrypt_byte(ZipCryptoState *state, unsigned char plain);
unsigned char zip_crypto_decrypt_byte(ZipCryptoState *state, unsigned char cipher);
void zip_crypto_generate_header_with_state(ZipCryptoState *state, uint32_t crc32_value, unsigned char header[12]);
bool zip_crypto_validate_header(const char *password, uint32_t crc32_value, const unsigned char encrypted_header[12]);

#endif
