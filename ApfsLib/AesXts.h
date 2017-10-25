#pragma once

#include <cstdint>

#include "Aes.h"

class AesXts
{
public:
	AesXts();
	~AesXts();

	void CleanUp();

	void SetKey(const uint8_t *key1, const uint8_t *key2);

	void Encrypt(uint8_t *cipher, const uint8_t *plain, size_t size, uint64_t unit_no);
	void Decrypt(uint8_t *plain, const uint8_t *cipher, size_t size, uint64_t unit_no);

private:
	void Xor(uint8_t *out, const uint8_t *op1, const uint8_t *op2);
	void MultiplyTweak(uint8_t *tweak);

	AES m_aes_1;
	AES m_aes_2;
};
