#include "zip_aes.h"

bool zip_aes_mode_enabled(const RuntimeOptions *options) {
    return options &&
           options->password && *options->password &&
           options->zip_encryption_mode &&
           strcasecmp(options->zip_encryption_mode, "aes256") == 0;
}

void zip_aes_not_implemented(void) {
    fail("AES ZIP 암호화는 아직 구현 중입니다. 현재는 zipcrypto를 사용하세요");
}
