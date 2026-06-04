#include "zip_crypto.h"

#include <sys/time.h>

static uint32_t zip_crypto_crc_table[256];
static bool zip_crypto_crc_table_ready = false;

static void zip_crypto_initialize_crc_table(void) {
    if (zip_crypto_crc_table_ready) {
        return;
    }
    for (uint32_t index = 0; index < 256u; index++) {
        uint32_t value = index;
        for (int bit = 0; bit < 8; bit++) {
            value = (value & 1u) ? (0xedb88320u ^ (value >> 1)) : (value >> 1);
        }
        zip_crypto_crc_table[index] = value;
    }
    zip_crypto_crc_table_ready = true;
}

static uint32_t zip_crypto_crc32_update(uint32_t crc, unsigned char plain) {
    zip_crypto_initialize_crc_table();
    return zip_crypto_crc_table[(crc ^ plain) & 0xffu] ^ (crc >> 8);
}

static void zip_crypto_update_keys(ZipCryptoState *state, unsigned char plain) {
    state->key0 = zip_crypto_crc32_update(state->key0, plain);
    state->key1 = state->key1 + (state->key0 & 0xffu);
    state->key1 = state->key1 * 134775813u + 1u;
    unsigned char key1_byte = (unsigned char) ((state->key1 >> 24) & 0xffu);
    state->key2 = zip_crypto_crc32_update(state->key2, key1_byte);
}

static unsigned char zip_crypto_mask(const ZipCryptoState *state) {
    uint16_t temp = (uint16_t) ((state->key2 & 0xffffu) | 2u);
    return (unsigned char) (((temp * (temp ^ 1u)) >> 8) & 0xffu);
}

static unsigned int zip_crypto_random_seed(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned int) (tv.tv_sec ^ tv.tv_usec ^ getpid());
}

static unsigned char zip_crypto_random_byte(void) {
    static bool seeded = false;
    if (!seeded) {
        srand(zip_crypto_random_seed());
        seeded = true;
    }
    return (unsigned char) (rand() & 0xff);
}

void zip_crypto_init(ZipCryptoState *state, const char *password) {
    state->key0 = 305419896u;
    state->key1 = 591751049u;
    state->key2 = 878082192u;

    const unsigned char *cursor = (const unsigned char *) (password ? password : "");
    while (*cursor) {
        zip_crypto_update_keys(state, *cursor);
        cursor += 1;
    }
}

unsigned char zip_crypto_encrypt_byte(ZipCryptoState *state, unsigned char plain) {
    unsigned char cipher = (unsigned char) (plain ^ zip_crypto_mask(state));
    zip_crypto_update_keys(state, plain);
    return cipher;
}

unsigned char zip_crypto_decrypt_byte(ZipCryptoState *state, unsigned char cipher) {
    unsigned char plain = (unsigned char) (cipher ^ zip_crypto_mask(state));
    zip_crypto_update_keys(state, plain);
    return plain;
}

void zip_crypto_generate_header_with_state(ZipCryptoState *state, uint32_t crc32_value, unsigned char header[12]) {
    for (size_t index = 0; index < 11; index++) {
        header[index] = zip_crypto_random_byte();
    }
    header[11] = (unsigned char) ((crc32_value >> 24) & 0xffu);

    for (size_t index = 0; index < 12; index++) {
        header[index] = zip_crypto_encrypt_byte(state, header[index]);
    }
}

bool zip_crypto_validate_header(const char *password, uint32_t crc32_value, const unsigned char encrypted_header[12]) {
    ZipCryptoState state;
    zip_crypto_init(&state, password);

    unsigned char header[12];
    for (size_t index = 0; index < 12; index++) {
        header[index] = zip_crypto_decrypt_byte(&state, encrypted_header[index]);
    }

    return header[11] == (unsigned char) ((crc32_value >> 24) & 0xffu);
}
