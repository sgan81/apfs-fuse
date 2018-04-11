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

#ifdef __linux__
#include <unicode/utypes.h>
#include <unicode/normalizer2.h>
#else
#include "Unicode.h"
#endif

#include <iostream>
#include <iomanip>
#include <vector>
#include <sstream>
#ifdef __linux__
#include <termios.h>
#endif
#include <stdio.h>

#include "Util.h"
#include "Crc32.h"

static Crc32 g_crc(true, 0x1EDC6F41);

uint64_t Fletcher64(const uint32_t *data, size_t cnt, uint64_t init)
{
	size_t k;

	uint64_t sum1 = init & 0xFFFFFFFFU;
	uint64_t sum2 = (init >> 32);

	for (k = 0; k < cnt; k++)
	{
		sum1 = (sum1 + data[k]);
		sum2 = (sum2 + sum1);
	}

	sum1 = sum1 % 0xFFFFFFFF;
	sum2 = sum2 % 0xFFFFFFFF;

	return (static_cast<uint64_t>(sum2) << 32) | static_cast<uint64_t>(sum1);
}

bool VerifyBlock(const void *block, size_t size)
{
	uint64_t cs;
	const uint32_t * const data = reinterpret_cast<const uint32_t *>(block);

	size /= sizeof(uint32_t);

	cs = *reinterpret_cast<const uint64_t *>(block);
	if (cs == 0)
		return false;
	if (cs == 0xFFFFFFFFFFFFFFFFULL)
		return false;

	cs = Fletcher64(data + 2, size - 2, 0);
	cs = Fletcher64(data, 2, cs);

	return cs == 0;
}

bool IsZero(const byte_t *data, size_t size)
{
	for (size_t k = 0; k < size; k++)
	{
		if (data[k] != 0)
			return false;
	}
	return true;
}

bool IsEmptyBlock(const void *data, size_t blksize)
{
	blksize /= sizeof(uint64_t);
	const uint64_t *qdata = reinterpret_cast<const uint64_t *>(data);

	for (size_t k = 0; k < blksize; k++)
	{
		if (qdata[k] != 0)
			return false;
	}
	return true;
}

void DumpHex(std::ostream &os, const byte_t *data, size_t size, size_t lineSize)
{
	using namespace std;

	size_t i, j;
	byte_t b;

	if (size == 0)
		return;

	os << hex << uppercase << setfill('0');

	for (i = 0; i < size; i += lineSize)
	{
		os << setw(4) << i << ": ";
		for (j = 0; (j < lineSize) && ((i + j) < size); j++)
			os << setw(2) << static_cast<unsigned int>(data[i + j]) << ' ';
		for (; j < lineSize; j++)
			os << "   ";

		os << "- ";

		for (j = 0; (j < lineSize) && ((i + j) < size); j++)
		{
			b = data[i + j];
			if (b >= 0x20 && b < 0x7F)
				os << b;
			else
				os << '.';
		}

		os << endl;
	}

	// os << dec;
}

// Like DumpHex, but prints a label.
void DumpBuffer(const uint8_t *data, size_t len, const char *label)
{
	std::cout << "dumping " << label << std::endl;
	DumpHex(std::cout, data, len);
}

std::string uuidstr(const apfs_uuid_t &uuid)
{
	using namespace std;

	stringstream st;
	int k;

	st << hex << uppercase << setfill('0');
	for (k = 0; k < 4; k++)
		st << setw(2) << static_cast<unsigned>(uuid[k]);
	st << '-';
	for (k = 4; k < 6; k++)
		st << setw(2) << static_cast<unsigned>(uuid[k]);
	st << '-';
	for (k = 6; k < 8; k++)
		st << setw(2) << static_cast<unsigned>(uuid[k]);
	st << '-';
	for (k = 8; k < 10; k++)
		st << setw(2) << static_cast<unsigned>(uuid[k]);
	st << '-';
	for (k = 10; k < 16; k++)
		st << setw(2) << static_cast<unsigned>(uuid[k]);

	return st.str();
}

#ifdef __linux__

uint32_t HashFilename(const char *utf8str, uint16_t name_len, bool case_insensitive)
{
	icu::StringPiece utf8(utf8str);
	icu::UnicodeString str = icu::UnicodeString::fromUTF8(utf8);
	icu::UnicodeString normalized;
	UErrorCode err = U_ZERO_ERROR;
	std::vector<UChar32> hashdata;
	uint32_t hash;

	const icu::Normalizer2 *norm = icu::Normalizer2::getNFDInstance(err);

	// Case folding must be done before normalization, otherwise some names are not hashed correctly.
	if (case_insensitive)
		str.foldCase();

	normalized = norm->normalize(str, err);

	hashdata.resize(normalized.countChar32() + 1);

	normalized.toUTF32(hashdata.data(), hashdata.size(), err);

	hashdata.pop_back();


	g_crc.SetCRC(0xFFFFFFFF);
	g_crc.Calc(reinterpret_cast<const byte_t *>(hashdata.data()), hashdata.size() * sizeof(UChar32));
	hash = g_crc.GetCRC();

	hash = ((hash & 0x3FFFFF) << 10) | (name_len & 0x3FF);

	return hash;
}

#else

uint32_t HashFilename(const char *utf8str, uint16_t name_len, bool case_insensitive)
{
	std::vector<char32_t> u32_name;
	std::vector<char32_t> u32_normalized;
	uint32_t hash;

	Utf8toU32(u32_name, reinterpret_cast<const uint8_t *>(utf8str));
	NormalizeFilename(u32_normalized, u32_name, case_insensitive);

	g_crc.SetCRC(0xFFFFFFFF);
	g_crc.Calc(reinterpret_cast<const byte_t *>(u32_normalized.data()), u32_normalized.size() * sizeof(char32_t));

	hash = g_crc.GetCRC();

	hash = ((hash & 0x3FFFFF) << 10) | (name_len & 0x3FF);

	return hash;
}

#endif

bool GetPassword(std::string &pw)
{
#ifdef __linux__
	struct termios told, tnew;
	FILE *stream = stdin;

	/* Turn echoing off and fail if we can’t. */
	if (tcgetattr (fileno (stream), &told) != 0)
		return false;
	tnew = told;
	tnew.c_lflag &= ~ECHO;
	if (tcsetattr (fileno (stream), TCSAFLUSH, &tnew) != 0)
		return false;

	/* Read the password. */
	std::getline(std::cin, pw);

	/* Restore terminal. */
	(void) tcsetattr (fileno (stream), TCSAFLUSH, &told);

	std::cout << std::endl;

	return true;
#else
	std::getline(std::cin, pw);
	std::cout << std::endl;

	return true;
#endif
}
