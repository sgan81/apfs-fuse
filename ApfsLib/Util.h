/*
	This file is part of apfs-fuse, a read-only implementation of APFS
	(Apple File System) for FUSE.
	Copyright (C) 2017 Simon Gander

	Apfs-fuse is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	Apfs-fuse is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with apfs-fuse.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <cstdint>
#include <string>
#include <ostream>
#include <vector>

#include "Global.h"

uint64_t Fletcher64(const uint32_t *data, size_t cnt, uint64_t init);
bool VerifyBlock(const void *block, size_t size);
bool IsZero(const byte_t *data, size_t size);
bool IsEmptyBlock(const void *data, size_t blksize);
void DumpHex(std::ostream &os, const byte_t *data, size_t size, size_t line_size = 16);
void DumpBuffer(const uint8_t *data, size_t len, const char *label);

std::string uuidstr(const apfs_uuid_t &uuid);
void dump_utf8(std::ostream &st, const char *str);
void dump_utf32(std::ostream &st, const char32_t *str, size_t size);

uint32_t HashFilename(const char *utf8str, uint16_t name_len, bool case_fold);

int StrCmpUtf8NormalizedFolded(const char *s1, const char *s2, bool case_fold);

bool Utf8toUtf32(std::vector<char32_t> &str32, const char * str);

size_t DecompressZLib(uint8_t *dst, unsigned int dst_size, const uint8_t *src, unsigned int src_size); // zstream can't take more than unsigned int for src and dst size
size_t DecompressADC(uint8_t *dst, size_t dst_size, const uint8_t *src, size_t src_size);
size_t DecompressLZVN(uint8_t *dst, size_t dst_size, const uint8_t *src, size_t src_size);
size_t DecompressBZ2(uint8_t *dst, unsigned int dst_size, const uint8_t *src, unsigned int src_size); // bz_stream can't take more than unsigned int for src and dst size
size_t DecompressLZFSE(uint8_t *dst, size_t dst_size, const uint8_t *src, size_t src_size);

bool GetPassword(std::string &pw);
