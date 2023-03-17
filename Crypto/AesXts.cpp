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

#include <ApfsLib/Endian.h>
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
	uint64_t tweak[2];
	size_t k = 0;

	tweak[0] = htole64(unit_no);
	tweak[1] = 0;

	m_aes_2.Encrypt(reinterpret_cast<const uint8_t *>(tweak), reinterpret_cast<uint8_t *>(tweak));

	for (k = 0; k < size; k += 0x10)
	{
		Xor128(pp, plain + k, tweak);
		m_aes_1.Encrypt(pp, cc);
		Xor128(cipher + k, cc, tweak);
		MultiplyTweak(tweak);
	}
}

void AesXts::Decrypt(uint8_t* plain, const uint8_t* cipher, std::size_t size, uint64_t unit_no)
{
	uint8_t pp[0x10];
	uint8_t cc[0x10];
	uint64_t tweak[2];
	size_t k = 0;

	tweak[0] = htole64(unit_no);
	tweak[1] = 0;

	m_aes_2.Encrypt(reinterpret_cast<const uint8_t *>(tweak), reinterpret_cast<uint8_t *>(tweak));

	for (k = 0; k < size; k += 0x10)
	{
		Xor128(cc, cipher + k, tweak);
		m_aes_1.Decrypt(cc, pp);
		Xor128(plain + k, pp, tweak);
		MultiplyTweak(tweak);
	}
}

void AesXts::Xor128(void *out, const void *op1, const void *op2)
{
	reinterpret_cast<uint64_t*>(out)[0] = reinterpret_cast<const uint64_t*>(op1)[0] ^ reinterpret_cast<const uint64_t*>(op2)[0];
	reinterpret_cast<uint64_t*>(out)[1] = reinterpret_cast<const uint64_t*>(op1)[1] ^ reinterpret_cast<const uint64_t*>(op2)[1];
}

void AesXts::MultiplyTweak(uint64_t* tweak)
{
	uint8_t c1;
	uint8_t c2;

#if (__BYTE_ORDER == __LITTLE_ENDIAN)
	c1 = (tweak[0] & 0x8000000000000000ULL) ? 1 : 0;
	c2 = (tweak[1] & 0x8000000000000000ULL) ? 0x87 : 0;

	tweak[0] = (tweak[0] << 1) ^ c2;
	tweak[1] = (tweak[1] << 1) | c1;
#else
	uint64_t t0;
	uint64_t t1;

	t0 = le64toh(tweak[0]);
	t1 = le64toh(tweak[1]);

	c1 = (t0 & 0x8000000000000000ULL) ? 1 : 0;
	c2 = (t1 & 0x8000000000000000ULL) ? 0x87 : 0;
	t0 = (t0 << 1) ^ c2;
	t1 = (t1 << 1) | c1;

	tweak[0] = htole64(t0);
	tweak[1] = htole64(t1);
#endif
}
