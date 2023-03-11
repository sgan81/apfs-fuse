#include "Sha1.h"

inline uint32_t Ch(uint32_t x, uint32_t y, uint32_t z)
{
	return (x & y) ^ ((~x) & z);
}

inline uint32_t Parity(uint32_t x, uint32_t y, uint32_t z)
{
	return x ^ y ^ z;
}

inline uint32_t Maj(uint32_t x, uint32_t y, uint32_t z)
{
	return (x & y) ^ (x & z) ^ (y & z);
}

inline uint32_t Rotl(int sh, uint32_t v)
{
	return (v << sh) | (v >> (32 - sh));
}

Sha1::Sha1()
{
	Init();
}

Sha1::~Sha1()
{
}

void Sha1::Init()
{
	m_hash[0] = 0x67452301;
	m_hash[1] = 0xEFCDAB89;
	m_hash[2] = 0x98BADCFE;
	m_hash[3] = 0x10325476;
	m_hash[4] = 0xC3D2E1F0;

	m_bit_cnt = 0;
	m_buf_idx = 0;

	for (size_t n = 0; n < 64; n++)
		m_buffer[n] = 0;
}

void Sha1::Update(const void * ptr, size_t size)
{
	size_t n;
	const uint8_t *data = reinterpret_cast<const uint8_t *>(ptr);

	for (n = 0; n < size; n++)
	{
		m_buffer[m_buf_idx] = data[n];
		m_buf_idx++;
		if (m_buf_idx == 64)
		{
			Round();
			m_buf_idx = 0;
		}
	}

	m_bit_cnt += (8 * size);
}

void Sha1::Final(uint8_t * hash)
{
	size_t n;

	m_buffer[m_buf_idx++] = 0x80;

	if (m_buf_idx > 56)
	{
		for (; m_buf_idx < 64; m_buf_idx++)
			m_buffer[m_buf_idx] = 0;
		Round();
		m_buf_idx = 0;
	}

	for (; m_buf_idx < 56; m_buf_idx++)
		m_buffer[m_buf_idx] = 0;
	m_buffer[56] = (m_bit_cnt >> 56) & 0xFF;
	m_buffer[57] = (m_bit_cnt >> 48) & 0xFF;
	m_buffer[58] = (m_bit_cnt >> 40) & 0xFF;
	m_buffer[59] = (m_bit_cnt >> 32) & 0xFF;
	m_buffer[60] = (m_bit_cnt >> 24) & 0xFF;
	m_buffer[61] = (m_bit_cnt >> 16) & 0xFF;
	m_buffer[62] = (m_bit_cnt >> 8) & 0xFF;
	m_buffer[63] = (m_bit_cnt >> 0) & 0xFF;
	Round();

	for (n = 0; n < 5; n++)
	{
		hash[4 * n + 0] = (m_hash[n] >> 24) & 0xFF;
		hash[4 * n + 1] = (m_hash[n] >> 16) & 0xFF;
		hash[4 * n + 2] = (m_hash[n] >> 8) & 0xFF;
		hash[4 * n + 3] = (m_hash[n] >> 0) & 0xFF;
	}
}

void Sha1::Round()
{
	uint32_t w[80];
	uint32_t a;
	uint32_t b;
	uint32_t c;
	uint32_t d;
	uint32_t e;
	uint32_t T;
	int k;

	for (k = 0; k < 16; k++)
		w[k] = (m_buffer[4 * k] << 24) | (m_buffer[4 * k + 1] << 16) | (m_buffer[4 * k + 2] << 8) | m_buffer[4 * k + 3];
	for (k = 16; k < 80; k++)
		w[k] = Rotl(1, w[k - 3] ^ w[k - 8] ^ w[k - 14] ^ w[k - 16]);

	a = m_hash[0];
	b = m_hash[1];
	c = m_hash[2];
	d = m_hash[3];
	e = m_hash[4];

	for (k = 0; k < 20; k++)
	{
		T = Rotl(5, a) + Ch(b, c, d) + e + m_K[0] + w[k];
		e = d;
		d = c;
		c = Rotl(30, b);
		b = a;
		a = T;
	}

	for (k = 20; k < 40; k++)
	{
		T = Rotl(5, a) + Parity(b, c, d) + e + m_K[1] + w[k];
		e = d;
		d = c;
		c = Rotl(30, b);
		b = a;
		a = T;
	}

	for (k = 40; k < 60; k++)
	{
		T = Rotl(5, a) + Maj(b, c, d) + e + m_K[2] + w[k];
		e = d;
		d = c;
		c = Rotl(30, b);
		b = a;
		a = T;
	}

	for (k = 60; k < 80; k++)
	{
		T = Rotl(5, a) + Parity(b, c, d) + e + m_K[3] + w[k];
		e = d;
		d = c;
		c = Rotl(30, b);
		b = a;
		a = T;
	}

	m_hash[0] = a + m_hash[0];
	m_hash[1] = b + m_hash[1];
	m_hash[2] = c + m_hash[2];
	m_hash[3] = d + m_hash[3];
	m_hash[4] = e + m_hash[4];
}


const uint32_t Sha1::m_K[4] =
{
	0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6
};
