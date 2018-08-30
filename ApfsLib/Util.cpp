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

#include "Unicode.h"

#include <cassert>

#include <iostream>
#include <iomanip>
#include <vector>
#include <sstream>
#if defined(__linux__) || defined(__APPLE__)
#include <termios.h>
#endif
#include <stdio.h>

#include "Util.h"
#include "Crc32.h"

#include <zlib.h>
#include <bzlib.h>

extern "C" {
#include <lzfse.h>
#include <lzvn_decode_base.h>
}

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

void dump_utf8(std::ostream &st, const char* str)
{
	size_t ptr = 0;
	uint8_t ch;
	char32_t uch;
	int cnt;

	st << std::hex << std::uppercase;

	while (str[ptr] != 0)
	{
		ch = str[ptr++];

		if (ch < 0x80) {
			uch = ch;
			cnt = 0;
		} else if (ch < 0xC0) {
			break;
		} else if (ch < 0xE0) {
			uch = ch & 0x1F;
			cnt = 1;
		} else if (ch < 0xF0) {
			uch = ch & 0x0F;
			cnt = 2;
		} else if (ch < 0xF8) {
			uch = ch & 0x07;
			cnt = 3;
		} else
			break;

		for (; cnt > 0; --cnt)
		{
			ch = str[ptr++];
			if ((ch & 0xC0) == 0x80)
				uch = (uch << 6) | (ch & 0x3F);
			else
				break;
		}

		st << uch << ' ';
	}

	st << str << std::endl;
}

void dump_utf32(std::ostream &st, const char32_t *str, size_t size)
{
	size_t k;

	st << std::hex << std::uppercase;

	for (k = 0; k < size; k++)
		st << str[k] << ' ';
	for (k = 0; k < size; k++)
		if (str[k] >= 0x20 && str[k] < 0x7F)
			st << static_cast<char>(str[k]);
		else
			st << '.';
	st << std::endl;
}

uint32_t HashFilename(const char* utf8str, uint16_t name_len, bool case_fold)
{
	std::vector<char32_t> utf32;
	std::vector<char32_t> utf32_nfd;
	uint32_t hash;

	Utf8toUtf32(utf32, utf8str);

	NormalizeFoldString(utf32_nfd, utf32, case_fold);

#if 0
	if (g_debug & Dbg_Dir)
	{
		dump_utf32(utf32.data(), utf32.size());
		dump_utf32(utf32_nfd.data(), utf32_nfd.size());
	}
#endif

	g_crc.SetCRC(0xFFFFFFFF);
	g_crc.Calc(reinterpret_cast<const byte_t *>(utf32_nfd.data()), utf32_nfd.size() * sizeof(char32_t));

	hash = g_crc.GetCRC();

	hash = ((hash & 0x3FFFFF) << 10) | (name_len & 0x3FF);

	return hash;
}

int StrCmpUtf8NormalizedFolded(const char* s1, const char* s2, bool case_fold)
{
	std::vector<char32_t> s1_u32;
	std::vector<char32_t> s2_u32;
	std::vector<char32_t> s1_u32_nfd;
	std::vector<char32_t> s2_u32_nfd;
	size_t k;

	Utf8toUtf32(s1_u32, s1);
	Utf8toUtf32(s2_u32, s2);

	NormalizeFoldString(s1_u32_nfd, s1_u32, case_fold);
	NormalizeFoldString(s2_u32_nfd, s2_u32, case_fold);

	s1_u32_nfd.push_back(0);
	s2_u32_nfd.push_back(0);

	k = 0;
	while (true)
	{
		if (s1_u32_nfd[k] < s2_u32_nfd[k])
			return -1;
		if (s1_u32_nfd[k] > s2_u32_nfd[k])
			return 1;
		if (s1_u32_nfd[k] == 0 || s2_u32_nfd[k] == 0)
			break;
		++k;
	}

	return 0;
}

bool Utf8toUtf32(std::vector<char32_t> &str32, const char* str)
{
	size_t ip = 0;
	int cnt = 0;

	char32_t ch;
	uint8_t c;

	bool ok = true;

	while (str[ip] != 0)
	{
		c = str[ip++];

		if (c < 0x80)
		{
			ch = c;
			cnt = 0;
		}
		else if (c < 0xC0)
		{
			ok = false;
			break;
		}
		else if (c < 0xE0)
		{
			ch = c & 0x1F;
			cnt = 1;
		}
		else if (c < 0xF0)
		{
			ch = c & 0x0F;
			cnt = 2;
		}
		else if (c < 0xF8)
		{
			ch = c & 0x07;
			cnt = 3;
		}
		else
		{
			ok = false;
			break;
		}

		for (; cnt > 0; --cnt)
		{
			c = str[ip++];
			if ((c & 0xC0) == 0x80)
				ch = (ch << 6) | (c & 0x3F);
			else
			{
				ok = false;
				break;
			}
		}

		if (!ok)
			break;

		str32.push_back(ch);

		if (ch == 0)
			break;
	}

	return ok;
}

bool GetPassword(std::string &pw)
{
#if defined(__linux__) || defined(__APPLE__)
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

size_t DecompressZLib(uint8_t *dst, size_t dst_size, const uint8_t *src, size_t src_size)
{
	// size_t nwr = 0;
	int ret;

	z_stream strm;

	memset(&strm, 0, sizeof(strm));
	strm.zalloc = Z_NULL;
	strm.zfree = Z_NULL;
	strm.avail_in = src_size;
	strm.avail_out = dst_size;
	strm.next_in = const_cast<uint8_t *>(src);
	strm.next_out = dst;

	ret = inflateInit2(&strm, 15);
	if (ret != Z_OK)
	{
		std::cerr << "DecompressZLib: inflateInit failed." << std::endl;
		return 0;
	}

	do {
		ret = inflate(&strm, Z_NO_FLUSH);
		if (ret == Z_NEED_DICT || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR)
		{
			strm.avail_out = 0;
			break;
		}
	} while (ret != Z_STREAM_END);

	inflateEnd(&strm);

	return dst_size - strm.avail_out;
}

size_t DecompressADC(uint8_t * dst, size_t dst_size, const uint8_t * src, size_t src_size)
{
	size_t in_idx = 0;
	size_t out_idx = 0;
	uint8_t ctl;
	int len;
	int dist;
	int cnt;

	for (;;)
	{
		if (in_idx >= src_size)
			break;
		if (out_idx >= dst_size)
			break;

		ctl = src[in_idx++];

		if (ctl & 0x80)
		{
			len = (ctl & 0x7F) + 1;

			for (cnt = 0; cnt < len; cnt++)
				dst[out_idx++] = src[in_idx++];
		}
		else
		{
			if (ctl & 0x40)
			{
				len = ctl - 0x3C;
				dist = src[in_idx++];
				dist = (dist << 8) | src[in_idx++];
				dist += 1;
			}
			else
			{
				len = ((ctl >> 2) & 0xF) + 3;
				dist = (((ctl & 3) << 8) | src[in_idx++]) + 1;
			}

			for (cnt = 0; cnt < len; cnt++)
			{
				dst[out_idx] = dst[out_idx - dist];
				out_idx++;
			}
		}
	}

	assert(in_idx == src_size);
	assert(out_idx == dst_size);

	return out_idx;
}

size_t DecompressLZVN(uint8_t * dst, size_t dst_size, const uint8_t * src, size_t src_size)
{
	lzvn_decoder_state state;

	memset(&state, 0, sizeof(state));

	state.src = src;
	state.src_end = src + src_size;
	state.dst = dst;
	state.dst_begin = dst;
	state.dst_end = dst + dst_size;
	state.dst_current = dst;

	lzvn_decode(&state);

	return static_cast<size_t>(state.dst - dst);
}

size_t DecompressBZ2(uint8_t * dst, size_t dst_size, const uint8_t * src, size_t src_size)
{
	bz_stream strm;

	memset(&strm, 0, sizeof(strm));

	BZ2_bzDecompressInit(&strm, 0, 0);

	strm.next_in = const_cast<char *>(reinterpret_cast<const char *>(src));
	strm.avail_in = src_size;
	strm.next_out = reinterpret_cast<char *>(dst);
	strm.avail_out = dst_size;

	BZ2_bzDecompress(&strm);

	BZ2_bzDecompressEnd(&strm);

	return strm.total_out_lo32;
}

size_t DecompressLZFSE(uint8_t * dst, size_t dst_size, const uint8_t * src, size_t src_size)
{
	return lzfse_decode_buffer(dst, dst_size, src, src_size, nullptr);
}

