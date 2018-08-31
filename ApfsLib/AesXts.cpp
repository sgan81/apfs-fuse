#include "AesXts.h"

#include <algorithm>
#include <iterator>

AesXts::AesXts()
{
	CleanUp();
}

AesXts::~AesXts()
{
}

void AesXts::CleanUp()
{
	m_aes_1.CleanUp();
	m_aes_2.CleanUp();
}

void AesXts::SetKey(const uint8_t* key1, const uint8_t* key2)
{
	m_aes_1.SetKey(key1, AES::AES_128);
	m_aes_2.SetKey(key2, AES::AES_128);
}

void AesXts::Encrypt(uint8_t* cipher, const uint8_t* plain, std::size_t size, uint64_t unit_no)
{
	uint8_t pp[0x10];
	uint8_t cc[0x10];
	uint8_t tweak[0x10];
	size_t k = 0;

	for (k = 0; k < 0x10; k++)
	{
		tweak[k] = unit_no & 0xFF;
		unit_no >>= 8;
	}

	m_aes_2.Encrypt(tweak, tweak);

	for (k = 0; k < size; k += 0x10)
	{
		Xor(pp, plain + k, tweak);
		m_aes_1.Encrypt(pp, cc);
		Xor(cipher + k, cc, tweak);
		MultiplyTweak(tweak);
	}
}

void AesXts::Decrypt(uint8_t* plain, const uint8_t* cipher, std::size_t size, uint64_t unit_no)
{
	uint8_t pp[0x10];
	uint8_t cc[0x10];
	uint8_t tweak[0x10];
	size_t k = 0;

	for (k = 0; k < 0x10; k++)
	{
		tweak[k] = unit_no & 0xFF;
		unit_no >>= 8;
	}

	m_aes_2.Encrypt(tweak, tweak);

	for (k = 0; k < size; k += 0x10)
	{
		Xor(cc, cipher + k, tweak);
		m_aes_1.Decrypt(cc, pp);
		Xor(plain + k, pp, tweak);
		MultiplyTweak(tweak);
	}
}

void AesXts::Xor(uint8_t *out, const uint8_t *op1, const uint8_t *op2)
{
	uint64_t *val64 = reinterpret_cast<uint64_t *>(out);
	const uint64_t *op1_64 = reinterpret_cast<const uint64_t *>(op1);
	const uint64_t *op2_64 = reinterpret_cast<const uint64_t *>(op2);

	val64[0] = op1_64[0] ^ op2_64[0];
	val64[1] = op1_64[1] ^ op2_64[1];
}

#ifdef __clang__
#pragma clang diagnostic ignored "-Wconditional-uninitialized"
#endif

void AesXts::MultiplyTweak(uint8_t* tweak)
{
	uint8_t cin;
	uint8_t cout;
	int k;

	cin = 0;
	for (k = 0; k < 0x10; k++)
	{
		cout = tweak[k] >> 7;
		tweak[k] = (tweak[k] << 1) | cin;
		cin = cout;
	}
	if (cout) // 2018-08 : wrongly reported uninitialized by clang
		tweak[0] ^= 0x87;
}
