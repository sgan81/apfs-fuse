/*
 *	This file is part of apfs-fuse, a read-only implementation of APFS
 *	(Apple File System) for FUSE.
 *	Copyright (C) 2017 Simon Gander
 *
 *	Apfs-fuse is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Apfs-fuse is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with apfs-fuse.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <cstdio>
#include <cstdint>
#include <cstring>

#include "Des.h"

#define rol28(v,n) ((v << n) & 0xFFFFFFF) | ((v >> (28-n)) & 0xFFFFFFF)
#define ror28(v,n) ((v >> n) & 0xFFFFFFF) | ((v << (28-n)) & 0xFFFFFFF)

DES::DES()
{
	memset(m_keySchedule, 0, sizeof(m_keySchedule));
	m_initVector = 0;
}

DES::~DES()
{
	memset(m_keySchedule, 0, sizeof(m_keySchedule));
	m_initVector = 0;
}

void DES::Encrypt(uint8_t *cipher, const uint8_t *plain, size_t size)
{
	size_t off;
	uint64_t r;

	for (off = 0; off < size; off += 8)
	{
		r = BytesToU64(plain + off);
		r = InitialPermutation(r);
		r = EncryptInternal(r, m_keySchedule);
		r = FinalPermutation(r);
		U64ToBytes(cipher + off, r);
	}
}

void DES::Decrypt(uint8_t *plain, const uint8_t *cipher, size_t size)
{
	size_t off;
	uint64_t r;

	for (off = 0; off < size; off += 8)
	{
		r = BytesToU64(cipher + off);
		r = InitialPermutation(r);
		r = DecryptInternal(r, m_keySchedule);
		r = FinalPermutation(r);
		U64ToBytes(plain + off, r);
	}
}

void DES::EncryptCBC(uint8_t *cipher, const uint8_t *plain, size_t size)
{
	size_t off;
	uint64_t r;

	for (off = 0; off < size; off += 8)
	{
		r = BytesToU64(plain + off);
		r = r ^ m_initVector;
		r = InitialPermutation(r);
		r = EncryptInternal(r, m_keySchedule);
		r = FinalPermutation(r);
		m_initVector = r;
		U64ToBytes(cipher + off, r);
	}
}

void DES::DecryptCBC(uint8_t *plain, const uint8_t *cipher, size_t size)
{
	size_t off;
	uint64_t r;
	uint64_t iv;

	for (off = 0; off < size; off += 8)
	{
		r = BytesToU64(cipher + off);
		iv = r;
		r = InitialPermutation(r);
		r = DecryptInternal(r, m_keySchedule);
		r = FinalPermutation(r);
		r = r ^ m_initVector;
		m_initVector = iv;
		U64ToBytes(plain + off, r);
	}
}

void DES::SetKey(const uint8_t *key)
{
	memset(m_keySchedule, 0, sizeof(m_keySchedule));
	m_initVector = 0;
	uint64_t k = BytesToU64(key);
	KeySchedule(k, m_keySchedule);
}

void DES::SetIV(const uint8_t *iv)
{
	if (iv)
		m_initVector = BytesToU64(iv);
	else
		m_initVector = 0;
}

void DES::PrintBits(uint64_t v, int bits)
{
	int i;

	for (i = bits - 1; i >= 0; i--)
	{
		if ((v >> i) & 1)
			printf("1");
		else
			printf("0");
		if ((i & 7) == 0)
			printf(" ");
	}
	printf("\n");
}

uint64_t DES::Permute(const uint8_t *box, uint64_t v)
{
	int i;
	uint64_t r;

	r = 0;
	for (i = 0; i < 64; i++) {
		r = (r << 1) | ((v >> (64 - box[i])) & 1);
	}

	return r;
}

uint64_t DES::InitialPermutation(uint64_t v)
{
	return Permute(m_ip_box, v);
}

uint64_t DES::FinalPermutation(uint64_t v)
{
	return Permute(m_fp_box, v);
}

uint32_t DES::Function(uint32_t r, uint64_t ks)
{
	int i;
	uint64_t e;
	uint32_t v;
	uint32_t f;
	uint8_t s[8];

	e = 0;
	for (i = 0; i < 48; i++)
		e = (e << 1) | ((r >> (32 - m_e_box[i])) & 1);

	e = e ^ ks;

	for (i = 0; i < 8; i++)
		s[i] = (e >> (42 - 6 * i)) & 0x3F;

	v = (m_s1[s[0]] << 28) | (m_s2[s[1]] << 24) | (m_s3[s[2]] << 20) | (m_s4[s[3]] << 16) | (m_s5[s[4]] << 12) | (m_s6[s[5]] << 8) | (m_s7[s[6]] << 4) | m_s8[s[7]];

	f = 0;
	for (i = 0; i < 32; i++)
		f = (f << 1) | ((v >> (32 - m_p_box[i])) & 1);

	return f;
}

void DES::KeySchedule(uint64_t key, uint64_t *ks)
{
	uint32_t c, d;
	uint64_t t;
	uint64_t k;
	int i;
	int j;

	t = 0;

	for (i = 0; i < 56; i++)
		t = (t << 1) | ((key >> (64 - m_pc1_box[i])) & 1);

	c = (t >> 28) & 0xFFFFFFF;
	d = t & 0xFFFFFFF;

	for (i = 0; i < 16; i++) {
		c = rol28(c, m_shifts[i]);
		d = rol28(d, m_shifts[i]);

		t = ((uint64_t)c << 28) | d;

		k = 0;
		for (j = 0; j < 48; j++)
			k = (k << 1) | ((t >> (56 - m_pc2_box[j])) & 1);

		ks[i] = k;
	}
}

uint64_t DES::EncryptInternal(uint64_t v, const uint64_t *ks)
{
	uint32_t l, r;
	uint32_t t;
	int i;

	l = v >> 32;
	r = v & 0xFFFFFFFF;

	for (i = 0; i < 16; i++) {
		t = r;
		r = l ^ Function(r, ks[i]);
		l = t;
	}

	return ((uint64_t)r << 32) | l;
}

uint64_t DES::DecryptInternal(uint64_t v, const uint64_t *ks)
{
	uint32_t l, r;
	uint32_t t;
	int i;

	r = v >> 32;
	l = v & 0xFFFFFFFF;

	for (i = 15; i >= 0; i--) {
		t = l;
		l = r ^ Function(l, ks[i]);
		r = t;
	}

	return ((uint64_t)l << 32) | r;
}

uint64_t DES::BytesToU64(const uint8_t *data)
{
	uint64_t d = 0;

	for (int n = 0; n < 8; n++)
		d = (d << 8) | data[n];

	return d;
}

void DES::U64ToBytes(uint8_t *data, uint64_t val)
{
	for (int n = 7; n >= 0; n--)
	{
		data[n] = val & 0xFF;
		val >>= 8;
	}
}


const uint8_t DES::m_ip_box[64] = {
	58, 50, 42, 34, 26, 18, 10,  2,
	60, 52, 44, 36, 28, 20, 12,  4,
	62, 54, 46, 38, 30, 22, 14,  6,
	64, 56, 48, 40, 32, 24, 16,  8,
	57, 49, 41, 33, 25, 17,  9,  1,
	59, 51, 43, 35, 27, 19, 11,  3,
	61, 53, 45, 37, 29, 21, 13,  5,
	63, 55, 47, 39, 31, 23, 15,  7
};

const uint8_t DES::m_fp_box[64] = {
	40,  8, 48, 16, 56, 24, 64, 32,
	39,  7, 47, 15, 55, 23, 63, 31,
	38,  6, 46, 14, 54, 22, 62, 30,
	37,  5, 45, 13, 53, 21, 61, 29,
	36,  4, 44, 12, 52, 20, 60, 28,
	35,  3, 43, 11, 51, 19, 59, 27,
	34,  2, 42, 10, 50, 18, 58, 26,
	33,  1, 41,  9, 49, 17, 57, 25
};

const uint8_t DES::m_e_box[48] = {
	32,  1,  2,  3,  4,  5,
	 4,  5,  6,  7,  8,  9,
	 8,  9, 10, 11, 12, 13,
	12, 13, 14, 15, 16, 17,
	16, 17, 18, 19, 20, 21,
	20, 21, 22, 23, 24, 25,
	24, 25, 26, 27, 28, 29,
	28, 29, 30, 31, 32,  1
};

const uint8_t DES::m_s1[64] = {
	14,  0,  4, 15, 13,  7,  1,  4,  2, 14, 15,  2, 11, 13,  8,  1,  3, 10, 10,  6,  6, 12, 12, 11,  5,  9,  9,  5,  0,  3,  7,  8,
	 4, 15,  1, 12, 14,  8,  8,  2, 13,  4,  6,  9,  2,  1, 11,  7, 15,  5, 12, 11,  9,  3,  7, 14,  3, 10, 10,  0,  5,  6,  0, 13
};
const uint8_t DES::m_s2[64] = {
	15,  3,  1, 13,  8,  4, 14,  7,  6, 15, 11,  2,  3,  8,  4, 14,  9, 12,  7,  0,  2,  1, 13, 10, 12,  6,  0,  9,  5, 11, 10,  5,
	 0, 13, 14,  8,  7, 10, 11,  1, 10,  3,  4, 15, 13,  4,  1,  2,  5, 11,  8,  6, 12,  7,  6, 12,  9,  0,  3,  5,  2, 14, 15,  9
};
const uint8_t DES::m_s3[64] = {
	10, 13,  0,  7,  9,  0, 14,  9,  6,  3,  3,  4, 15,  6,  5, 10,  1,  2, 13,  8, 12,  5,  7, 14, 11, 12,  4, 11,  2, 15,  8,  1,
	13,  1,  6, 10,  4, 13,  9,  0,  8,  6, 15,  9,  3,  8,  0,  7, 11,  4,  1, 15,  2, 14, 12,  3,  5, 11, 10,  5, 14,  2,  7, 12
};
const uint8_t DES::m_s4[64] = {
	 7, 13, 13,  8, 14, 11,  3,  5,  0,  6,  6, 15,  9,  0, 10,  3,  1,  4,  2,  7,  8,  2,  5, 12, 11,  1, 12, 10,  4, 14, 15,  9,
	10,  3,  6, 15,  9,  0,  0,  6, 12, 10, 11,  1,  7, 13, 13,  8, 15,  9,  1,  4,  3,  5, 14, 11,  5, 12,  2,  7,  8,  2,  4, 14
};
const uint8_t DES::m_s5[64] = {
	2, 14, 12, 11,  4,  2,  1, 12,  7,  4, 10,  7, 11, 13,  6,  1,  8,  5,  5,  0,  3, 15, 15, 10, 13,  3,  0,  9, 14,  8,  9,  6,
	4, 11,  2,  8,  1, 12, 11,  7, 10,  1, 13, 14,  7,  2,  8, 13, 15,  6,  9, 15, 12,  0,  5,  9,  6, 10,  3,  4,  0,  5, 14,  3
};
const uint8_t DES::m_s6[64] = {
	12, 10,  1, 15, 10,  4, 15,  2,  9,  7,  2, 12,  6,  9,  8,  5,  0,  6, 13,  1,  3, 13,  4, 14, 14,  0,  7, 11,  5,  3, 11,  8,
	 9,  4, 14,  3, 15,  2,  5, 12,  2,  9,  8,  5, 12, 15,  3, 10,  7, 11,  0, 14,  4,  1, 10,  7,  1,  6, 13,  0, 11,  8,  6, 13
};
const uint8_t DES::m_s7[64] = {
	4, 13, 11,  0,  2, 11, 14,  7, 15,  4,  0,  9,  8,  1, 13, 10,  3, 14, 12,  3,  9,  5,  7, 12,  5,  2, 10, 15,  6,  8,  1,  6,
	1,  6,  4, 11, 11, 13, 13,  8, 12,  1,  3,  4,  7, 10, 14,  7, 10,  9, 15,  5,  6,  0,  8, 15,  0, 14,  5,  2,  9,  3,  2, 12
};
const uint8_t DES::m_s8[64] = {
	13,  1,  2, 15,  8, 13,  4,  8,  6, 10, 15,  3, 11,  7,  1,  4, 10, 12,  9,  5,  3,  6, 14, 11,  5,  0,  0, 14, 12,  9,  7,  2,
	 7,  2, 11,  1,  4, 14,  1,  7,  9,  4, 12, 10, 14,  8,  2, 13,  0, 15,  6, 12, 10,  9, 13,  0, 15,  3,  3,  5,  5,  6,  8, 11
};

const uint8_t DES::m_p_box[32] = {
	16,  7, 20, 21,
	29, 12, 28, 17,
	 1, 15, 23, 26,
	 5, 18, 31, 10,
	 2,  8, 24, 14,
	32, 27,  3,  9,
	19, 13, 30,  6,
	22, 11,  4, 25
};

const uint8_t DES::m_pc1_box[56] = {
	57, 49, 41, 33, 25, 17,  9,
	 1, 58, 50, 42, 34, 26, 18,
	10,  2, 59, 51, 43, 35, 27,
	19, 11,  3, 60, 52, 44, 36,

	63, 55, 47, 39, 31, 23, 15,
	 7, 62, 54, 46, 38, 30, 22,
	14,  6, 61, 53, 45, 37, 29,
	21, 13,  5, 28, 20, 12,  4
};

const uint8_t DES::m_pc2_box[48] = {
	14, 17, 11, 24,  1,  5,
	 3, 28, 15,  6, 21, 10,
	23, 19, 12,  4, 26,  8,
	16,  7, 27, 20, 13,  2,
	41, 52, 31, 37, 47, 55,
	30, 40, 51, 45, 33, 48,
	44, 49, 39, 56, 34, 53,
	46, 42, 50, 36, 29, 32
};

const unsigned int DES::m_shifts[16] = {
	1, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1
};
