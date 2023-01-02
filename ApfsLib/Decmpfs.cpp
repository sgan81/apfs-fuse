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

#include <iomanip>
#include <iostream>
#include <cstring>
#include <cassert>

#include "Decmpfs.h"
#include "Endian.h"

#include "Global.h"
#include "Util.h"


struct RsrcForkHeader
{
	be_uint32_t data_offset;
	be_uint32_t mgmt_offset;
	be_uint32_t data_size;
	be_uint32_t mgmt_size;
};

struct CmpfRsrcEntry
{
	// 1 64K-Block
	le_uint32_t off;
	le_uint32_t size;
};

struct CmpfRsrc
{
	le_uint32_t entries;
	CmpfRsrcEntry entry[32];
};

bool IsDecompAlgoSupported(uint16_t algo)
{
	switch (algo)
	{
	case 3:
	case 4:
	case 7:
	case 8:
	case 9:
	case 10:
	case 11:
	case 12:
	case 13:
	case 14:
		return true;
	default:
		return false;
	}
}

bool IsDecompAlgoInRsrc(uint16_t algo)
{
	switch (algo)
	{
	case 4:
	case 8:
	case 10:
	case 12:
	case 14:
		return true;
	default:
		return false;
	}
}

bool DecompressFile(ApfsDir &dir, uint64_t ino, std::vector<uint8_t> &decompressed, const std::vector<uint8_t> &compressed)
{
	if (compressed.size() < sizeof(CompressionHeader))
		return false;

	const CompressionHeader *hdr = reinterpret_cast<const CompressionHeader *>(compressed.data());
	const uint8_t *cdata = compressed.data() + sizeof(CompressionHeader);
	size_t csize = compressed.size() - sizeof(CompressionHeader);
	size_t decoded_bytes = 0;

#if 1 // Disable to get compressed data
	if (g_debug & Dbg_Cmpfs)
	{
		std::cout << "DecompressFile " << compressed.size() << " => " << hdr->size << ", algo = " << hdr->algo;

		switch (hdr->algo)
		{
		case 3: std::cout << " (Zlib, Attr)"; break;
		case 4: std::cout << " (Zlib, Rsrc)"; break;
		case 7: std::cout << " (LZVN, Attr)"; break;
		case 8: std::cout << " (LZVN, Rsrc)"; break;
		case 9: std::cout << " (Uncompressed, Attr)"; break;
		case 10: std::cout << " (Uncompressed, Rsrc)"; break;
		case 11: std::cout << " (LZFSE, Attr)"; break;
		case 12: std::cout << " (LZFSE, Rsrc)"; break;
		case 13: std::cout << " (LZBITMAP, Attr)"; break;
		case 14: std::cout << " (LZBITMAP, Rsrc)"; break;
		default: std::cout << " (Unknown)"; break;
		}

		std::cout << std::endl;
	}

	if (!IsDecompAlgoSupported(hdr->algo))
	{
		if (g_debug & Dbg_Errors) {
			std::cout << "Unsupported decompression algorithm." << std::endl;
			DumpHex(std::cout, compressed.data(), compressed.size());
		}
		return false;
	}

	if (IsDecompAlgoInRsrc(hdr->algo))
	{
		std::vector<uint8_t> rsrc;
		size_t k;

		bool rc = dir.GetAttribute(rsrc, ino, "com.apple.ResourceFork");

		if (!rc)
		{
			if (g_debug & Dbg_Errors)
				std::cout << "Decmpfs: Could not find resource fork " << ino << std::endl;
			decompressed.clear();
			return false;
		}

		if (hdr->algo == 4) // Zlib, rsrc
		{
			RsrcForkHeader rsrc_hdr;

			memcpy(&rsrc_hdr, rsrc.data(), sizeof(rsrc_hdr));

			if (rsrc_hdr.data_offset > rsrc.size())
			{
				if (g_debug & Dbg_Errors)
					std::cout << "Decmpfs: Invalid data offset in rsrc header." << std::endl;
				return false;
			}

			const uint8_t *cmpf_rsrc_base = rsrc.data() + rsrc_hdr.data_offset + sizeof(uint32_t);
			const CmpfRsrc *cmpf_rsrc = reinterpret_cast<const CmpfRsrc *>(cmpf_rsrc_base);

			decompressed.resize((hdr->size + 0xFFFF) & 0xFFFF0000);

			for (k = 0; k < cmpf_rsrc->entries; k++)
			{
				size_t src_offset = cmpf_rsrc->entry[k].off;
				const uint8_t *src = cmpf_rsrc_base + src_offset;
				size_t src_len = cmpf_rsrc->entry[k].size;
				uint8_t *dst = decompressed.data() + 0x10000 * k;
				size_t expected_len = hdr->size - (0x10000 * k);
				if (expected_len > 0x10000)
					expected_len = 0x10000;

				if (src_len > 0x10001)
				{
					if (g_debug & Dbg_Errors)
						std::cout << "Decmpfs: In rsrc, src_len too big (" << src_len << ")" << std::endl;
					return false;
				}

				if (src[0] == 0x78)
				{
					decoded_bytes = DecompressZLib(dst, 0x10000, src, src_len);
				}
				else if ((src[0] & 0x0F) == 0x0F)
				{
					memcpy(dst, src + 1, src_len - 1);
					decoded_bytes = src_len - 1;
				}
				else
				{
					if (g_debug & Dbg_Errors)
						std::cout << "Decmpfs: Something wrong with zlib data." << std::endl;
					decompressed.clear();
					return false;
				}

				if (expected_len != decoded_bytes)
				{
					if (g_debug & Dbg_Errors)
						std::cout << "Decmpfs: Expected len != decompressed len: " << expected_len << " != " << decoded_bytes << std::endl;
					return false;
				}
			}
		}
		else
		{
			const uint32_t *off_list = reinterpret_cast<const uint32_t *>(rsrc.data());

			decompressed.resize((hdr->size + 0xFFFF) & 0xFFFF0000);

			for (k = 0; (k << 16) < decompressed.size(); k++) {
				size_t expected_len = hdr->size - (0x10000 * k);
				if (expected_len > 0x10000)
					expected_len = 0x10000;
				const uint8_t *src = rsrc.data() + off_list[k];
				size_t src_len = off_list[k + 1] - off_list[k];

				if (src_len > 0x10001) {
					if (g_debug & Dbg_Errors)
						std::cout << "Decmpfs: In rsrc, src_len too big (" << src_len << ")" << std::endl;
					return false;
				}

				switch (hdr->algo) {
				case 8:
					if (src[0] == 0x06) {
						memcpy(decompressed.data() + (k << 16), src + 1, src_len - 1);
						decoded_bytes = src_len - 1;
					}
					else
						decoded_bytes = DecompressLZVN(decompressed.data() + (k << 16), expected_len, src, src_len);
					break;
				case 10:
					// Assuming ...
					memcpy(decompressed.data() + (k << 16), src + 1, src_len - 1);
					decoded_bytes = src_len - 1;
					break;
				case 12:
					// Assuming ...
					decoded_bytes = DecompressLZFSE(decompressed.data() + (k << 16), expected_len, src, src_len);
					// TODO is there also an uncompressed variant?
					break;
				case 14:
					if (src[0] == 0xFF) {
						memcpy(decompressed.data() + (k << 16), src + 1, src_len - 1);
						decoded_bytes = src_len - 1;
					}
					else
						decoded_bytes = DecompressLZBITMAP(decompressed.data() + (k << 16), expected_len, src, src_len);
					break;
				default:
					decoded_bytes = 0;
					break;
				}

				if (decoded_bytes != expected_len) {
					if (g_debug & Dbg_Errors)
						std::cout << "Decmpfs: Expected length != decompressed length: " << expected_len << " != " << decoded_bytes << " [k = " << k << "]" << std::endl;

					return false;
				}
			}
		}

		decompressed.resize(hdr->size);
	}
	else
	{
		decompressed.resize(hdr->size);

		switch (hdr->algo) {
		case 3:
			if (cdata[0] == 0x78)
				decoded_bytes = DecompressZLib(decompressed.data(), decompressed.size(), cdata, csize);
			else if (cdata[0] == 0xFF) {
				assert(hdr->size == csize - 1);
				decompressed.assign(cdata + 1, cdata + csize);
				decoded_bytes = decompressed.size();
			}
			else
				return false;
			break;

		case 7:
			if (cdata[0] == 0x06) {
				assert(hdr->size == csize - 1);
				decompressed.assign(cdata + 1, cdata + csize);
				decoded_bytes = decompressed.size();
			}
			else
				decoded_bytes = DecompressLZVN(decompressed.data(), decompressed.size(), cdata, csize);
			break;

		case 9:
			assert(cdata[0] == 0xCC);
			assert(hdr->size == csize - 1);
			decompressed.assign(cdata + 1, cdata + csize);
			decoded_bytes = decompressed.size();
			break;

		case 11:
			// TODO uncompressed variant?
			decoded_bytes = DecompressLZFSE(decompressed.data(), decompressed.size(), cdata, csize);
			break;

		case 13:
			if (cdata[0] == 0xFF) {
				assert(hdr->size == csize - 1);
				decompressed.assign(cdata + 1, cdata + csize);
				decoded_bytes = decompressed.size();
			}
			else
				decoded_bytes = DecompressLZBITMAP(decompressed.data(), decompressed.size(), cdata, csize);
			break;

		default:
			decoded_bytes = 0;
			break;
		}

		if (decoded_bytes != hdr->size)
		{
			if (g_debug & Dbg_Errors)
				std::cout << "Decmpfs: In attr, expected len != decoded len: " << hdr->size << " != " << decoded_bytes << std::endl;
			return false;
		}
	}

#else
	if (IsDecompAlgoInRsrc(hdr->algo))
	{
		dir.GetAttribute(decompressed, ino, "com.apple.ResourceFork");
	}
	else
	{
		decompressed = compressed;
	}
#endif

	return true;
}
