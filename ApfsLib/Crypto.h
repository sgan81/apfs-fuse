#pragma once

#include <cstdint>

#include "Aes.h"

void Rfc3394_KeyWrap(uint8_t *crypto, const uint8_t *plain, size_t size, const uint8_t *key, AES::Mode aes_mode, uint64_t iv);
void Rfc3394_KeyUnwrap(uint8_t *plain, const uint8_t *crypto, size_t size, const uint8_t *key, AES::Mode aes_mode, uint64_t *iv);
void HMAC_SHA256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t *mac);
void PBKDF2_HMAC_SHA256(const uint8_t* pw, size_t pw_len, const uint8_t* salt, size_t salt_len, int iterations, uint8_t* derived_key, size_t dk_len);
