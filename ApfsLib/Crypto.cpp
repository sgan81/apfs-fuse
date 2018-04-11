#include "Sha256.h"
#include "Crypto.h"
#include "Util.h"

#include <cstring>
#include <cassert>

#include <iostream>
#include <iomanip>

#ifdef _MSC_VER
#include <intrin.h>

#define bswap_64 _byteswap_uint64
#endif
#ifdef __linux__
#include <byteswap.h>
#endif
#ifdef __APPLE__
#define bswap_64 _OSSwapInt64
#endif

union Rfc3394_Unit {
	uint64_t u64[2];
	uint8_t u8[16];
};

constexpr uint64_t rfc_3394_default_iv = 0xA6A6A6A6A6A6A6A6ULL;

void Rfc3394_KeyWrap(uint8_t *crypto, const uint8_t *plain, size_t size, const uint8_t *key, AES::Mode aes_mode, uint64_t iv)
{
	Rfc3394_Unit u;
	uint64_t a;
	uint64_t r[6];
	int i;
	int j;
	int n = size / sizeof(uint64_t);
	uint64_t t;
	const uint64_t *p = reinterpret_cast<const uint64_t *>(plain);
	uint64_t *c = reinterpret_cast<uint64_t *>(crypto);
	AES aes;

	aes.SetKey(key, aes_mode);
	a = iv;
	t = 1;

	for (i = 0; i < n; i++)
		r[i] = p[i];

	for (j = 0; j < 6; j++)
	{
		for (i = 0; i < n; i++)
		{
			u.u64[1] = r[i];
			u.u64[0] = a;
			aes.Encrypt(u.u8, u.u8);
			a = u.u64[0] ^ bswap_64(t);
			r[i] = u.u64[1];
			t++;
		}
	}

	c[0] = a;
	for (i = 0; i < n; i++)
		c[i + 1] = r[i];
}

bool Rfc3394_KeyUnwrap(uint8_t *plain, const uint8_t *crypto, size_t size, const uint8_t *key, AES::Mode aes_mode, uint64_t *iv)
{
	Rfc3394_Unit u;
	uint64_t a;
	uint64_t r[6];
	int i;
	int j;
	int n = size / sizeof(uint64_t);
	uint64_t t;
	const uint64_t *c = reinterpret_cast<const uint64_t *>(crypto);
	uint64_t *p = reinterpret_cast<uint64_t *>(plain);
	AES aes;

	aes.SetKey(key, aes_mode);
	t = 6 * n;

	a = c[0];
	for (i = 0; i < n; i++)
		r[i] = c[i + 1];

	for (j = 5; j >= 0; j--)
	{
		for (i = n - 1; i >= 0; i--)
		{
			u.u64[0] = a ^ bswap_64(t);
			u.u64[1] = r[i];
			aes.Decrypt(u.u8, u.u8);
			a = u.u64[0];
			r[i] = u.u64[1];
			t--;
		}
	}

	for (i = 0; i < n; i++)
		p[i] = r[i];
	if (iv)
		*iv = a;

	return a == rfc_3394_default_iv;
}

void HMAC_SHA256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t *mac)
{
	uint8_t kdata[0x40];
	uint8_t digest[0x20];
	constexpr uint8_t ipad = 0x36;
	constexpr uint8_t opad = 0x5C;
	SHA256 sha;

	if (key_len > 0x40)
	{
		sha.Init();
		sha.Update(key, key_len);
		sha.Final(digest);

		memcpy(kdata, digest, 0x20);
		key_len = 0x20;
	}
	else
	{
		memcpy(kdata, key, key_len);
	}
	if (key_len < 0x40)
		memset(kdata + key_len, 0, 0x40 - key_len);

	for (size_t k = 0; k < 0x40; k++)
		kdata[k] ^= ipad;

	sha.Init();
	sha.Update(kdata, 0x40);
	sha.Update(data, data_len);
	sha.Final(digest);

	for (size_t k = 0; k < 0x40; k++)
		kdata[k] ^= (ipad ^ opad);

	sha.Init();
	sha.Update(kdata, 0x40);
	sha.Update(digest, 0x20);
	sha.Final(mac);

	memset(digest, 0, sizeof(digest));
}

void PBKDF2_HMAC_SHA256(const uint8_t* pw, size_t pw_len, const uint8_t* salt, size_t salt_len, int iterations, uint8_t* derived_key, size_t dk_len)
{
	// HMAC_SHA256(pw, key_len, salt, salt_len, mac)

	// l = ceil(dkLen / hLen); // No of blocks
	// r = dkLen - (l - 1) * hLen; // Octets in last block
	// T[k] = F(P, S, c, k + 1); // k = 1 .. iterations

	// F(P, S, c, k) = U_1 ^ U_2 ^ ... U_c
	// U_1 = PRF(P, S || INT(i))
	// U_2 = PRF(P, U_1)
	// U_c = PRF(P, U_{c-1})

	assert(salt_len <= 0x10);
	assert(dk_len <= 0x20);

	constexpr size_t h_len = 0x20;
	size_t r;
	size_t l;
	uint8_t t[h_len];
	uint8_t u[h_len];
	uint8_t s[0x14];
	size_t k;
	int j;
	uint32_t i;
	size_t n;

	r = dk_len % h_len;
	l = dk_len / h_len;
	if (r > 0) l++;

	for (i = 1, k = 0; k < dk_len; i++, k += h_len)
	{
		// F(P,S,c,i)

		memcpy(s, salt, salt_len);
		s[salt_len + 0] = (i >> 24) & 0xFF;
		s[salt_len + 1] = (i >> 16) & 0xFF;
		s[salt_len + 2] = (i >> 8) & 0xFF;
		s[salt_len + 3] = i & 0xFF;

		HMAC_SHA256(pw, pw_len, s, salt_len + 4, u);
		memcpy(t, u, sizeof(t));

		for (j = 1; j < iterations; j++)
		{
			HMAC_SHA256(pw, pw_len, u, sizeof(u), u);
			for (n = 0; n < h_len; n++)
				t[n] ^= u[n];
		}

		for (n = 0; n < h_len && (n + k) < dk_len; n++)
			derived_key[n + k] = t[n];
	}
}
