#pragma once

#include <cstdint>
#include <fstream>

#include "Aes.h"
#include "Device.h"

class DiskImageFile
{
public:
	DiskImageFile();
	~DiskImageFile();

	bool Open(const char *name);
	void Close();
	void Reset();

	void Read(uint64_t off, void *data, size_t size);

	size_t GetContentSize() const { return m_crypt_size; }

	bool CheckSetupEncryption();

private:
	bool SetupEncryptionV1();
	bool SetupEncryptionV2();
	size_t PkcsUnpad(const uint8_t *data, size_t size);

	std::ifstream m_image;

	bool m_is_encrypted;
	uint64_t m_crypt_offset;
	uint64_t m_crypt_size;
	uint32_t m_crypt_blocksize;
	uint8_t m_hmac_key[0x14];

	AES m_aes;
};
