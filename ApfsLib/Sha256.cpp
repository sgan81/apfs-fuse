#include <cstddef>
#include <cstdint>

#include "Sha256.h"

#ifdef _MSC_VER
#include <intrin.h>
#else
static inline uint32_t _rotr(uint32_t v, int sh)
{
	return (v >> sh) | (v << (32 - sh));
}

#endif

static inline uint32_t Ch(uint32_t x, uint32_t y, uint32_t z)
{
	return (x & y) ^ (~x & z);
}

static inline uint32_t Maj(uint32_t x, uint32_t y, uint32_t z)
{
	return (x & y) ^ (x & z) ^ (y & z);
}

static inline uint32_t ROTR(uint32_t x, uint32_t y)
{
	return _rotr(x, y);
}

static inline uint32_t SHR(uint32_t x, uint32_t y)
{
	return x >> y;
}

static inline uint32_t S0(uint32_t x)
{
	return _rotr(x, 2) ^ _rotr(x, 13) ^ _rotr(x, 22);
}

static inline uint32_t S1(uint32_t x)
{
	return _rotr(x, 6) ^ _rotr(x, 11) ^ _rotr(x, 25);
}

static inline uint32_t s0(uint32_t x)
{
	return _rotr(x, 7) ^ _rotr(x, 18) ^ (x >> 3);
}

static inline uint32_t s1(uint32_t x)
{
	return _rotr(x, 17) ^ _rotr(x, 19) ^ (x >> 10);
}

const uint32_t SHA256::_k[64] = {
	0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, 0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
	0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3, 0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
	0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC, 0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
	0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7, 0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
	0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13, 0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
	0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3, 0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
	0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5, 0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
	0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208, 0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2
};

SHA256::SHA256()
{
	Init();
}

SHA256::~SHA256()
{
}

void SHA256::Init()
{
	int i;

	_hash[0] = 0x6A09E667;
	_hash[1] = 0xBB67AE85;
	_hash[2] = 0x3C6EF372;
	_hash[3] = 0xA54FF53A;
	_hash[4] = 0x510E527F;
	_hash[5] = 0x9B05688C;
	_hash[6] = 0x1F83D9AB;
	_hash[7] = 0x5BE0CD19;
	_bufferPtr = 0;
	_byteCnt = 0;

	for (i = 0; i < 64; i++)
		_buffer[i] = 0;
}

void SHA256::Round()
{
	uint32_t a, b, c, d, e, f, g, h;
	uint32_t w[64];
	int t;
	uint32_t t1, t2;

	for (t = 0; t < 16; t++)
		w[t] = (_buffer[4*t] << 24) | (_buffer[4*t+1] << 16) | (_buffer[4*t+2] << 8) | _buffer[4*t+3];
	for (t = 16; t < 64; t++)
		w[t] = s1(w[t-2]) + w[t-7] + s0(w[t-15]) + w[t-16];

	a = _hash[0];
	b = _hash[1];
	c = _hash[2];
	d = _hash[3];
	e = _hash[4];
	f = _hash[5];
	g = _hash[6];
	h = _hash[7];

	for (t = 0; t < 64; t++) {
		t1 = h + S1(e) + Ch(e, f, g) + _k[t] + w[t];
		t2 = S0(a) + Maj(a, b, c);
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}

	_hash[0] += a;
	_hash[1] += b;
	_hash[2] += c;
	_hash[3] += d;
	_hash[4] += e;
	_hash[5] += f;
	_hash[6] += g;
	_hash[7] += h;

	for (t = 0; t < 64; t++)
		_buffer[t] = 0;

	_bufferPtr = 0;
}

void SHA256::Update(const void *data, size_t cnt)
{
	size_t i;
	const uint8_t *bdata = reinterpret_cast<const uint8_t *>(data);

	for (i = 0; i < cnt; i++) {
		_buffer[_bufferPtr++] = bdata[i];
		if (_bufferPtr == 64)
			Round();
	}

	_byteCnt += cnt;
}

void SHA256::Final(uint8_t *hash)
{
	uint32_t len_h, len_l;
	int i;

	_buffer[_bufferPtr] = 0x80;
	if (_bufferPtr >= 55)
		Round();

	len_h = static_cast<uint32_t>(_byteCnt >> 29);
	len_l = static_cast<uint32_t>(_byteCnt << 3);

	_buffer[56] = (len_h >> 24) & 0xFF;
	_buffer[57] = (len_h >> 16) & 0xFF;
	_buffer[58] = (len_h >> 8) & 0xFF;
	_buffer[59] = len_h & 0xFF;
	_buffer[60] = (len_l >> 24) & 0xFF;
	_buffer[61] = (len_l >> 16) & 0xFF;
	_buffer[62] = (len_l >> 8) & 0xFF;
	_buffer[63] = len_l & 0xFF;

	Round();

	for (i = 0; i < 8; i++) {
		hash[4*i] = (_hash[i] >> 24) & 0xFF;
		hash[4*i+1] = (_hash[i] >> 16) & 0xFF;
		hash[4*i+2] = (_hash[i] >> 8) & 0xFF;
		hash[4*i+3] = _hash[i] & 0xFF;
	}

	Init();
}
