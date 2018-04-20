#pragma once

#include <cstddef>
#include <cstdint>

class DES
{
	friend class TripleDES;

public:
	DES();
	~DES();

	void Encrypt(uint8_t *cipher, const uint8_t *plain, size_t size);
	void Decrypt(uint8_t *plain, const uint8_t *cipher, size_t size);
	void EncryptCBC(uint8_t *cipher, const uint8_t *plain, size_t size);
	void DecryptCBC(uint8_t *plain, const uint8_t *cipher, size_t size);
	void SetKey(const uint8_t *key);
	void SetIV(const uint8_t *iv);

private:
	static uint64_t InitialPermutation(uint64_t v);
	static uint64_t FinalPermutation(uint64_t v);
	static void KeySchedule(uint64_t key, uint64_t *ks);
	static uint64_t EncryptInternal(uint64_t v, const uint64_t *ks);
	static uint64_t DecryptInternal(uint64_t v, const uint64_t *ks);

	static void PrintBits(uint64_t v, int bits);
	static uint64_t Permute(const uint8_t *box, uint64_t v);
	static uint32_t Function(uint32_t r, uint64_t ks);

	static uint64_t BytesToU64(const uint8_t *data);
	static void U64ToBytes(uint8_t *data, uint64_t val);

	uint64_t m_keySchedule[16];
	uint64_t m_initVector;

	static const uint8_t m_ip_box[64];
	static const uint8_t m_fp_box[64];
	static const uint8_t m_e_box[48];
	static const uint8_t m_s1[64];
	static const uint8_t m_s2[64];
	static const uint8_t m_s3[64];
	static const uint8_t m_s4[64];
	static const uint8_t m_s5[64];
	static const uint8_t m_s6[64];
	static const uint8_t m_s7[64];
	static const uint8_t m_s8[64];
	static const uint8_t m_p_box[32];
	static const uint8_t m_pc1_box[56];
	static const uint8_t m_pc2_box[48];
	static const unsigned int m_shifts[16];
};
