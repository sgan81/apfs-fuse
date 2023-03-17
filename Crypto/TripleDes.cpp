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

#include "Des.h"
#include "TripleDes.h"

#include <cstring>

TripleDES::TripleDES()
{
	memset(m_keySchedule, 0, sizeof(m_keySchedule));
	m_iv = 0;
}

TripleDES::~TripleDES()
{
	memset(m_keySchedule, 0, sizeof(m_keySchedule));
	m_iv = 0;
}

void TripleDES::Encrypt(uint8_t * cipher, const uint8_t * plain, size_t size)
{
	size_t off;
	uint64_t r;

	for (off = 0; off < size; off += 8)
	{
		r = DES::BytesToU64(plain + off);
		r = DES::InitialPermutation(r);
		r = DES::EncryptInternal(r, m_keySchedule[0]);
		r = DES::DecryptInternal(r, m_keySchedule[1]);
		r = DES::EncryptInternal(r, m_keySchedule[2]);
		r = DES::FinalPermutation(r);
		DES::U64ToBytes(cipher + off, r);
	}
}

void TripleDES::Decrypt(uint8_t * plain, const uint8_t * cipher, size_t size)
{
	size_t off;
	uint64_t r;

	for (off = 0; off < size; off += 8)
	{
		r = DES::BytesToU64(cipher + off);
		r = DES::InitialPermutation(r);
		r = DES::DecryptInternal(r, m_keySchedule[2]);
		r = DES::EncryptInternal(r, m_keySchedule[1]);
		r = DES::DecryptInternal(r, m_keySchedule[0]);
		r = DES::FinalPermutation(r);
		DES::U64ToBytes(plain + off, r);
	}
}

void TripleDES::EncryptCBC(uint8_t * cipher, const uint8_t * plain, size_t size)
{
	size_t off;
	uint64_t r;

	for (off = 0; off < size; off += 8)
	{
		r = DES::BytesToU64(plain + off);
		r = r ^ m_iv;
		r = DES::InitialPermutation(r);
		r = DES::EncryptInternal(r, m_keySchedule[0]);
		r = DES::DecryptInternal(r, m_keySchedule[1]);
		r = DES::EncryptInternal(r, m_keySchedule[2]);
		r = DES::FinalPermutation(r);
		m_iv = r;
		DES::U64ToBytes(cipher + off, r);
	}
}

void TripleDES::DecryptCBC(uint8_t * plain, const uint8_t * cipher, size_t size)
{
	size_t off;
	uint64_t r;
	uint64_t iv;

	for (off = 0; off < size; off += 8)
	{
		r = DES::BytesToU64(cipher + off);
		iv = r;
		r = DES::InitialPermutation(r);
		r = DES::DecryptInternal(r, m_keySchedule[2]);
		r = DES::EncryptInternal(r, m_keySchedule[1]);
		r = DES::DecryptInternal(r, m_keySchedule[0]);
		r = DES::FinalPermutation(r);
		r = r ^ m_iv;
		m_iv = iv;
		DES::U64ToBytes(plain + off, r);
	}
}

void TripleDES::SetKey(const uint8_t * key)
{
	uint64_t k[3];
	memset(m_keySchedule, 0, sizeof(m_keySchedule));
	m_iv = 0;
	k[0] = DES::BytesToU64(key + 0);
	k[1] = DES::BytesToU64(key + 8);
	k[2] = DES::BytesToU64(key + 16);
	DES::KeySchedule(k[0], m_keySchedule[0]);
	DES::KeySchedule(k[1], m_keySchedule[1]);
	DES::KeySchedule(k[2], m_keySchedule[2]);
}

void TripleDES::SetIV(const uint8_t * iv)
{
	if (iv)
		m_iv = DES::BytesToU64(iv);
	else
		m_iv = 0;
}
