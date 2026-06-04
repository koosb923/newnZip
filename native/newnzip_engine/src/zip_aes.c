#include "zip_aes.h"

#include <sys/time.h>

#if defined(__APPLE__)
#include <CommonCrypto/CommonCryptor.h>
#include <CommonCrypto/CommonHMAC.h>
#include <CommonCrypto/CommonKeyDerivation.h>

struct ZipAesContext {
    unsigned char crypt_key[32];
    unsigned char auth_key[32];
    uint8_t strength;
    uint32_t counter;
    CCHmacContext hmac;
};

static unsigned int zip_aes_random_seed(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned int) (tv.tv_sec ^ tv.tv_usec ^ getpid());
}

static unsigned char zip_aes_random_byte(void) {
    static bool seeded = false;
    if (!seeded) {
        srand(zip_aes_random_seed());
        seeded = true;
    }
    return (unsigned char) (rand() & 0xff);
}

static size_t zip_aes_key_length(uint8_t strength) {
    switch (strength) {
        case 0x01: return 16u;
        case 0x02: return 24u;
        case 0x03: return 32u;
        default: fail("지원하지 않는 AES ZIP 강도입니다");
    }
    return 0;
}

static void zip_aes_derive(
    const char *password,
    uint8_t strength,
    const unsigned char *salt,
    unsigned char *crypt_key,
    unsigned char *auth_key,
    unsigned char password_verifier[2]
) {
    size_t key_length = zip_aes_key_length(strength);
    size_t derived_length = key_length * 2u + 2u;
    unsigned char derived[66];
    if (CCKeyDerivationPBKDF(
        kCCPBKDF2,
        password ? password : "",
        strlen(password ? password : ""),
        salt,
        zip_aes_salt_length(strength),
        kCCPRFHmacAlgSHA1,
        1000,
        derived,
        derived_length
    ) != kCCSuccess) {
        fail("AES ZIP 키 파생에 실패했습니다");
    }
    memcpy(crypt_key, derived, key_length);
    memcpy(auth_key, derived + key_length, key_length);
    memcpy(password_verifier, derived + key_length * 2u, 2u);
}

static void zip_aes_keystream_block(
    ZipAesContext *context,
    unsigned char block[16]
) {
    unsigned char counter_block[16] = {0};
    counter_block[0] = (unsigned char) (context->counter & 0xffu);
    counter_block[1] = (unsigned char) ((context->counter >> 8) & 0xffu);
    counter_block[2] = (unsigned char) ((context->counter >> 16) & 0xffu);
    counter_block[3] = (unsigned char) ((context->counter >> 24) & 0xffu);

    size_t out_moved = 0;
    CCCryptorStatus status = CCCrypt(
        kCCEncrypt,
        kCCAlgorithmAES,
        kCCOptionECBMode,
        context->crypt_key,
        zip_aes_key_length(context->strength),
        NULL,
        counter_block,
        sizeof(counter_block),
        block,
        16,
        &out_moved
    );
    if (status != kCCSuccess || out_moved != 16u) {
        fail("AES ZIP CTR 블록 생성에 실패했습니다");
    }
    context->counter += 1u;
}

bool zip_aes_mode_enabled(const RuntimeOptions *options) {
    return options &&
           options->password && *options->password &&
           options->zip_encryption_mode &&
           strcasecmp(options->zip_encryption_mode, "aes256") == 0;
}

void zip_aes_not_implemented(void) {
    fail("AES ZIP 암호화는 아직 구현 중입니다. 현재는 zipcrypto를 사용하세요");
}

uint16_t zip_aes_extra_field_length(void) {
    return 11u;
}

void zip_aes_write_extra_field(FILE *output, uint16_t actual_method) {
    write_u16(output, ZIP_AES_EXTRA_FIELD_ID);
    write_u16(output, 7u);
    write_u16(output, 0x0001u);
    if (fwrite("AE", 1, 2, output) != 2) {
        fail_errno("AES ZIP extra field vendor를 기록하지 못했습니다");
    }
    if (fputc(0x03, output) == EOF) {
        fail_errno("AES ZIP strength를 기록하지 못했습니다");
    }
    write_u16(output, actual_method);
}

bool zip_aes_apply_extra_field(CentralEntry *entry, const unsigned char *extra, uint16_t data_size) {
    if (data_size < 7u) {
        return false;
    }
    uint16_t version = read_u16(extra);
    if (version != 0x0001u && version != 0x0002u) {
        return false;
    }
    if (extra[2] != 'A' || extra[3] != 'E') {
        return false;
    }
    uint8_t strength = extra[4];
    if (strength != 0x03u) {
        return false;
    }
    entry->encryption_mode = ZIP_ENCRYPTION_AES256;
    entry->aes_strength = strength;
    entry->actual_method = read_u16(extra + 5);
    return true;
}

size_t zip_aes_salt_length(uint8_t strength) {
    switch (strength) {
        case 0x01: return 8u;
        case 0x02: return 12u;
        case 0x03: return 16u;
        default: fail("지원하지 않는 AES ZIP 강도입니다");
    }
    return 0;
}

size_t zip_aes_auth_code_length(void) {
    return 10u;
}

ZipAesContext *zip_aes_create_encryptor(const char *password, uint8_t strength, unsigned char *salt_out, unsigned char password_verifier[2]) {
    ZipAesContext *context = calloc(1, sizeof(ZipAesContext));
    if (!context) {
        fail("메모리가 부족합니다");
    }
    context->strength = strength;
    context->counter = 1u;
    for (size_t index = 0; index < zip_aes_salt_length(strength); index++) {
        salt_out[index] = zip_aes_random_byte();
    }
    zip_aes_derive(password, strength, salt_out, context->crypt_key, context->auth_key, password_verifier);
    CCHmacInit(&context->hmac, kCCHmacAlgSHA1, context->auth_key, zip_aes_key_length(strength));
    return context;
}

ZipAesContext *zip_aes_create_decryptor(const char *password, uint8_t strength, const unsigned char *salt, const unsigned char password_verifier[2]) {
    unsigned char expected_verifier[2];
    ZipAesContext *context = calloc(1, sizeof(ZipAesContext));
    if (!context) {
        fail("메모리가 부족합니다");
    }
    context->strength = strength;
    context->counter = 1u;
    zip_aes_derive(password, strength, salt, context->crypt_key, context->auth_key, expected_verifier);
    if (memcmp(expected_verifier, password_verifier, 2u) != 0) {
        free(context);
        fail("비밀번호가 올바르지 않습니다");
    }
    CCHmacInit(&context->hmac, kCCHmacAlgSHA1, context->auth_key, zip_aes_key_length(strength));
    return context;
}

void zip_aes_crypt(ZipAesContext *context, unsigned char *buffer, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        unsigned char block[16];
        zip_aes_keystream_block(context, block);
        size_t block_size = size - offset > 16u ? 16u : size - offset;
        for (size_t index = 0; index < block_size; index++) {
            buffer[offset + index] ^= block[index];
        }
        offset += block_size;
    }
}

void zip_aes_auth_update(ZipAesContext *context, const unsigned char *encrypted, size_t size) {
    CCHmacUpdate(&context->hmac, encrypted, size);
}

void zip_aes_finalize_auth(ZipAesContext *context, unsigned char auth_code[10]) {
    unsigned char digest[CC_SHA1_DIGEST_LENGTH];
    CCHmacFinal(&context->hmac, digest);
    memcpy(auth_code, digest, 10u);
}

void zip_aes_destroy(ZipAesContext *context) {
    if (!context) {
        return;
    }
    memset(context, 0, sizeof(*context));
    free(context);
}

#elif defined(_WIN32)

#include <windows.h>
#include <bcrypt.h>

#ifdef _MSC_VER
#pragma comment(lib, "bcrypt.lib")
#endif

struct ZipAesContext {
    unsigned char crypt_key[32];
    unsigned char auth_key[32];
    uint8_t strength;
    uint32_t counter;
    BCRYPT_ALG_HANDLE aes_algorithm;
    BCRYPT_KEY_HANDLE aes_key;
    PUCHAR aes_key_object;
    DWORD aes_key_object_length;
    BCRYPT_ALG_HANDLE hmac_algorithm;
    BCRYPT_HASH_HANDLE hmac;
    PUCHAR hmac_object;
    DWORD hmac_object_length;
};

static void zip_aes_check_ntstatus(NTSTATUS status, const char *message) {
    if (status < 0) {
        fail(message);
    }
}

static size_t zip_aes_key_length(uint8_t strength) {
    switch (strength) {
        case 0x01: return 16u;
        case 0x02: return 24u;
        case 0x03: return 32u;
        default: fail("지원하지 않는 AES ZIP 강도입니다");
    }
    return 0;
}

static void zip_aes_derive(
    const char *password,
    uint8_t strength,
    const unsigned char *salt,
    unsigned char *crypt_key,
    unsigned char *auth_key,
    unsigned char password_verifier[2]
) {
    BCRYPT_ALG_HANDLE sha1_algorithm = NULL;
    size_t key_length = zip_aes_key_length(strength);
    size_t derived_length = key_length * 2u + 2u;
    unsigned char derived[66];

    zip_aes_check_ntstatus(
        BCryptOpenAlgorithmProvider(&sha1_algorithm, BCRYPT_SHA1_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG),
        "AES ZIP SHA-1 알고리즘을 열지 못했습니다"
    );
    zip_aes_check_ntstatus(
        BCryptDeriveKeyPBKDF2(
            sha1_algorithm,
            (PUCHAR) (password ? password : ""),
            (ULONG) strlen(password ? password : ""),
            (PUCHAR) salt,
            (ULONG) zip_aes_salt_length(strength),
            1000,
            derived,
            (ULONG) derived_length,
            0
        ),
        "AES ZIP 키 파생에 실패했습니다"
    );
    BCryptCloseAlgorithmProvider(sha1_algorithm, 0);

    memcpy(crypt_key, derived, key_length);
    memcpy(auth_key, derived + key_length, key_length);
    memcpy(password_verifier, derived + key_length * 2u, 2u);
}

static void zip_aes_keystream_block(
    ZipAesContext *context,
    unsigned char block[16]
) {
    unsigned char counter_block[16] = {0};
    ULONG result_size = 0;

    counter_block[0] = (unsigned char) (context->counter & 0xffu);
    counter_block[1] = (unsigned char) ((context->counter >> 8) & 0xffu);
    counter_block[2] = (unsigned char) ((context->counter >> 16) & 0xffu);
    counter_block[3] = (unsigned char) ((context->counter >> 24) & 0xffu);

    zip_aes_check_ntstatus(
        BCryptEncrypt(
            context->aes_key,
            counter_block,
            sizeof(counter_block),
            NULL,
            NULL,
            0,
            block,
            16,
            &result_size,
            0
        ),
        "AES ZIP CTR 블록 생성에 실패했습니다"
    );
    if (result_size != 16u) {
        fail("AES ZIP CTR 블록 길이가 올바르지 않습니다");
    }
    context->counter += 1u;
}

static void zip_aes_initialize_crypto(ZipAesContext *context) {
    DWORD object_length = 0;
    DWORD bytes_copied = 0;
    size_t key_length = zip_aes_key_length(context->strength);

    zip_aes_check_ntstatus(
        BCryptOpenAlgorithmProvider(&context->aes_algorithm, BCRYPT_AES_ALGORITHM, NULL, 0),
        "AES ZIP AES 알고리즘을 열지 못했습니다"
    );
    zip_aes_check_ntstatus(
        BCryptSetProperty(
            context->aes_algorithm,
            BCRYPT_CHAINING_MODE,
            (PUCHAR) BCRYPT_CHAIN_MODE_ECB,
            (ULONG) sizeof(BCRYPT_CHAIN_MODE_ECB),
            0
        ),
        "AES ZIP AES 체이닝 모드를 설정하지 못했습니다"
    );
    zip_aes_check_ntstatus(
        BCryptGetProperty(
            context->aes_algorithm,
            BCRYPT_OBJECT_LENGTH,
            (PUCHAR) &object_length,
            sizeof(object_length),
            &bytes_copied,
            0
        ),
        "AES ZIP AES object length를 읽지 못했습니다"
    );
    context->aes_key_object_length = object_length;
    context->aes_key_object = calloc(1, object_length == 0 ? 1u : object_length);
    if (!context->aes_key_object) {
        fail("메모리가 부족합니다");
    }
    zip_aes_check_ntstatus(
        BCryptGenerateSymmetricKey(
            context->aes_algorithm,
            &context->aes_key,
            context->aes_key_object,
            context->aes_key_object_length,
            context->crypt_key,
            (ULONG) key_length,
            0
        ),
        "AES ZIP AES 키를 만들지 못했습니다"
    );

    zip_aes_check_ntstatus(
        BCryptOpenAlgorithmProvider(&context->hmac_algorithm, BCRYPT_SHA1_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG),
        "AES ZIP HMAC 알고리즘을 열지 못했습니다"
    );
    zip_aes_check_ntstatus(
        BCryptGetProperty(
            context->hmac_algorithm,
            BCRYPT_OBJECT_LENGTH,
            (PUCHAR) &object_length,
            sizeof(object_length),
            &bytes_copied,
            0
        ),
        "AES ZIP HMAC object length를 읽지 못했습니다"
    );
    context->hmac_object_length = object_length;
    context->hmac_object = calloc(1, object_length == 0 ? 1u : object_length);
    if (!context->hmac_object) {
        fail("메모리가 부족합니다");
    }
    zip_aes_check_ntstatus(
        BCryptCreateHash(
            context->hmac_algorithm,
            &context->hmac,
            context->hmac_object,
            context->hmac_object_length,
            context->auth_key,
            (ULONG) key_length,
            0
        ),
        "AES ZIP HMAC을 만들지 못했습니다"
    );
}

bool zip_aes_mode_enabled(const RuntimeOptions *options) {
    return options &&
           options->password && *options->password &&
           options->zip_encryption_mode &&
           strcasecmp(options->zip_encryption_mode, "aes256") == 0;
}

void zip_aes_not_implemented(void) {
    fail("AES ZIP 암호화는 아직 구현 중입니다. 현재는 zipcrypto를 사용하세요");
}

uint16_t zip_aes_extra_field_length(void) {
    return 11u;
}

void zip_aes_write_extra_field(FILE *output, uint16_t actual_method) {
    write_u16(output, ZIP_AES_EXTRA_FIELD_ID);
    write_u16(output, 7u);
    write_u16(output, 0x0001u);
    if (fwrite("AE", 1, 2, output) != 2) {
        fail_errno("AES ZIP extra field vendor를 기록하지 못했습니다");
    }
    if (fputc(0x03, output) == EOF) {
        fail_errno("AES ZIP strength를 기록하지 못했습니다");
    }
    write_u16(output, actual_method);
}

bool zip_aes_apply_extra_field(CentralEntry *entry, const unsigned char *extra, uint16_t data_size) {
    if (data_size < 7u) {
        return false;
    }
    uint16_t version = read_u16(extra);
    if (version != 0x0001u && version != 0x0002u) {
        return false;
    }
    if (extra[2] != 'A' || extra[3] != 'E') {
        return false;
    }
    uint8_t strength = extra[4];
    if (strength != 0x03u) {
        return false;
    }
    entry->encryption_mode = ZIP_ENCRYPTION_AES256;
    entry->aes_strength = strength;
    entry->actual_method = read_u16(extra + 5);
    return true;
}

size_t zip_aes_salt_length(uint8_t strength) {
    switch (strength) {
        case 0x01: return 8u;
        case 0x02: return 12u;
        case 0x03: return 16u;
        default: fail("지원하지 않는 AES ZIP 강도입니다");
    }
    return 0;
}

size_t zip_aes_auth_code_length(void) {
    return 10u;
}

ZipAesContext *zip_aes_create_encryptor(const char *password, uint8_t strength, unsigned char *salt_out, unsigned char password_verifier[2]) {
    ZipAesContext *context = calloc(1, sizeof(ZipAesContext));
    if (!context) {
        fail("메모리가 부족합니다");
    }
    context->strength = strength;
    context->counter = 1u;
    zip_aes_check_ntstatus(
        BCryptGenRandom(NULL, salt_out, (ULONG) zip_aes_salt_length(strength), BCRYPT_USE_SYSTEM_PREFERRED_RNG),
        "AES ZIP salt를 만들지 못했습니다"
    );
    zip_aes_derive(password, strength, salt_out, context->crypt_key, context->auth_key, password_verifier);
    zip_aes_initialize_crypto(context);
    return context;
}

ZipAesContext *zip_aes_create_decryptor(const char *password, uint8_t strength, const unsigned char *salt, const unsigned char password_verifier[2]) {
    unsigned char expected_verifier[2];
    ZipAesContext *context = calloc(1, sizeof(ZipAesContext));
    if (!context) {
        fail("메모리가 부족합니다");
    }
    context->strength = strength;
    context->counter = 1u;
    zip_aes_derive(password, strength, salt, context->crypt_key, context->auth_key, expected_verifier);
    if (memcmp(expected_verifier, password_verifier, 2u) != 0) {
        free(context);
        fail("비밀번호가 올바르지 않습니다");
    }
    zip_aes_initialize_crypto(context);
    return context;
}

void zip_aes_crypt(ZipAesContext *context, unsigned char *buffer, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        unsigned char block[16];
        zip_aes_keystream_block(context, block);
        size_t block_size = size - offset > 16u ? 16u : size - offset;
        for (size_t index = 0; index < block_size; index++) {
            buffer[offset + index] ^= block[index];
        }
        offset += block_size;
    }
}

void zip_aes_auth_update(ZipAesContext *context, const unsigned char *encrypted, size_t size) {
    zip_aes_check_ntstatus(
        BCryptHashData(context->hmac, (PUCHAR) encrypted, (ULONG) size, 0),
        "AES ZIP 인증 데이터를 갱신하지 못했습니다"
    );
}

void zip_aes_finalize_auth(ZipAesContext *context, unsigned char auth_code[10]) {
    unsigned char digest[20];
    zip_aes_check_ntstatus(
        BCryptFinishHash(context->hmac, digest, sizeof(digest), 0),
        "AES ZIP 인증 코드를 만들지 못했습니다"
    );
    memcpy(auth_code, digest, 10u);
}

void zip_aes_destroy(ZipAesContext *context) {
    if (!context) {
        return;
    }
    if (context->hmac) {
        BCryptDestroyHash(context->hmac);
    }
    if (context->hmac_algorithm) {
        BCryptCloseAlgorithmProvider(context->hmac_algorithm, 0);
    }
    if (context->aes_key) {
        BCryptDestroyKey(context->aes_key);
    }
    if (context->aes_algorithm) {
        BCryptCloseAlgorithmProvider(context->aes_algorithm, 0);
    }
    free(context->hmac_object);
    free(context->aes_key_object);
    memset(context, 0, sizeof(*context));
    free(context);
}

#else

struct ZipAesContext {
    int unused;
};

bool zip_aes_mode_enabled(const RuntimeOptions *options) {
    return options &&
           options->password && *options->password &&
           options->zip_encryption_mode &&
           strcasecmp(options->zip_encryption_mode, "aes256") == 0;
}

void zip_aes_not_implemented(void) {
    fail("AES ZIP 암호화는 현재 이 플랫폼에서 아직 구현되지 않았습니다");
}

uint16_t zip_aes_extra_field_length(void) { return 11u; }
void zip_aes_write_extra_field(FILE *output, uint16_t actual_method) { (void) output; (void) actual_method; zip_aes_not_implemented(); }
bool zip_aes_apply_extra_field(CentralEntry *entry, const unsigned char *extra, uint16_t data_size) { (void) entry; (void) extra; (void) data_size; return false; }
size_t zip_aes_salt_length(uint8_t strength) { (void) strength; zip_aes_not_implemented(); return 0; }
size_t zip_aes_auth_code_length(void) { return 10u; }
ZipAesContext *zip_aes_create_encryptor(const char *password, uint8_t strength, unsigned char *salt_out, unsigned char password_verifier[2]) { (void) password; (void) strength; (void) salt_out; (void) password_verifier; zip_aes_not_implemented(); return NULL; }
ZipAesContext *zip_aes_create_decryptor(const char *password, uint8_t strength, const unsigned char *salt, const unsigned char password_verifier[2]) { (void) password; (void) strength; (void) salt; (void) password_verifier; zip_aes_not_implemented(); return NULL; }
void zip_aes_crypt(ZipAesContext *context, unsigned char *buffer, size_t size) { (void) context; (void) buffer; (void) size; zip_aes_not_implemented(); }
void zip_aes_auth_update(ZipAesContext *context, const unsigned char *encrypted, size_t size) { (void) context; (void) encrypted; (void) size; zip_aes_not_implemented(); }
void zip_aes_finalize_auth(ZipAesContext *context, unsigned char auth_code[10]) { (void) context; (void) auth_code; zip_aes_not_implemented(); }
void zip_aes_destroy(ZipAesContext *context) { (void) context; }

#endif
