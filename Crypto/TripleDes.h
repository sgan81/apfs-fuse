#pragma once

#include <cstddef>
#include <cstdint>

class TripleDES
{
public:
	TripleDES();
	~TripleDES();

	void Encrypt(uint8_t *cipher, const uint8_t *plain, size_t size);
	void Decrypt(uint8_t *plain, const uint8_t *cipher, size_t size);
	void EncryptCBC(uint8_t *cipher, const uint8_t *plain, size_t size);
	void DecryptCBC(uint8_t *plain, const uint8_t *cipher, size_t size);

	void SetKey(const uint8_t *key);
	void SetIV(const uint8_t *iv);

private:
	uint64_t m_keySchedule[3][16];
	uint64_t m_iv;
};
