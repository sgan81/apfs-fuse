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
