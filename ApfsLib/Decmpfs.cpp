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

#include <iostream>
#include <cstring>
#include <cassert>

#include "Decmpfs.h"
#include "Inflate.h"

#ifdef __linux__
#include <byteswap.h>
#endif

#include "FastCompression.h"
#include "Global.h"

struct RsrcForkHeader
{
	uint32_t data_off_be;
	uint32_t mgmt_off_be;
	uint32_t data_size_be;
	uint32_t mgmt_size_be;
};

struct CmpfRsrcEntry
{
	// 1 64K-Block
	uint32_t off;
	uint32_t size;
};

struct CmpfRsrc
{
	uint32_t entries;
	CmpfRsrcEntry entry[32];
};

static size_t DecompressZLib(uint8_t *dst, size_t dst_size, const uint8_t *src, size_t src_size)
{
	size_t nwr = 0;

	if (src[0] == 0x78)
	{
		Inflate inf;

		nwr = inf.Decompress(dst, dst_size, src + 2, src_size - 2);

		// assert(nwr == dst_size);
	}

	return nwr;
}

bool IsDecompAlgoSupported(uint16_t algo)
{
	switch (algo)
	{
	case 3:
	case 4:
	case 7:
	case 8:
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
		return true;
	default:
		return false;
	}
}

bool DecompressFile(ApfsDir &dir, uint64_t ino, std::vector<uint8_t> &decompressed, const std::vector<uint8_t> &compressed)
{
#if 1
	if (compressed.size() < sizeof(CompressionHeader))
		return false;

	const CompressionHeader *hdr = reinterpret_cast<const CompressionHeader *>(compressed.data());
	const uint8_t *cdata = compressed.data() + sizeof(CompressionHeader);
	size_t csize = compressed.size() - sizeof(CompressionHeader);

	if (hdr->algo == 3)
	{
		if (g_debug > 0)
			std::cout << "DecompressFile " << compressed.size() << " => " << hdr->size << std::endl;

		decompressed.resize(hdr->size);

		if (compressed[0x10] == 0x78)
		{
			DecompressZLib(decompressed.data(), decompressed.size(), cdata, csize);
		}
		else if (compressed[0x10] == 0xFF)
		{
			decompressed.assign(cdata + 1, cdata + csize);
		}
	}
	else if (hdr->algo == 4)
	{
		std::vector<uint8_t> rsrc;

		bool rc = dir.GetAttribute(rsrc, ino, "com.apple.ResourceFork");

		if (!rc)
		{
			decompressed.clear();
			return false;
		}

		RsrcForkHeader rsrc_hdr;

		memcpy(&rsrc_hdr, rsrc.data(), sizeof(rsrc_hdr));
		rsrc_hdr.data_off_be = bswap_32(rsrc_hdr.data_off_be);
		rsrc_hdr.data_size_be = bswap_32(rsrc_hdr.data_size_be);
		rsrc_hdr.mgmt_off_be = bswap_32(rsrc_hdr.mgmt_off_be);
		rsrc_hdr.mgmt_size_be = bswap_32(rsrc_hdr.mgmt_size_be);

		uint32_t rsrc_size = bswap_32(*reinterpret_cast<uint32_t *>(rsrc.data() + rsrc_hdr.data_off_be));
		const uint8_t *cmpf_rsrc_base = rsrc.data() + rsrc_hdr.data_off_be + sizeof(uint32_t);
		const CmpfRsrc *cmpf_rsrc = reinterpret_cast<const CmpfRsrc *>(cmpf_rsrc_base);

		decompressed.resize((hdr->size + 0xFFFF) & 0xFFFF0000);

		uint8_t blk[0x10000];
		size_t k;
		size_t off = 0;

		for (k = 0; k < cmpf_rsrc->entries; k++)
		{
			// DecompressZLib(decompressed.data() + 0x10000 * k, 0x10000, cmpf_rsrc_base + cmpf_rsrc->entry[k].off, cmpf_rsrc->entry[k].size);
			off = DecompressZLib(blk, 0x10000, cmpf_rsrc_base + cmpf_rsrc->entry[k].off, cmpf_rsrc->entry[k].size);

			if (g_debug > 0)
				std::cout << "DecompressZLib dst = " << (0x10000 * k) << " / 10000 src = " << cmpf_rsrc->entry[k].off << " / " << cmpf_rsrc->entry[k].size << " => " << off << std::endl;

			std::copy(blk, blk + 0x10000, decompressed.begin() + (0x10000 * k));
		}
	}
	else if (hdr->algo == 7)
	{
		if (g_debug > 0)
			std::cout << "Decompress LZVN compressed file " << compressed.size() << " => " << hdr->size << std::endl;

		decompressed.resize(hdr->size);

		if (cdata[0] == 0x06)
			decompressed.assign(cdata + 1, cdata + csize);
		else
			lzvn_decode(decompressed.data(), decompressed.size(), cdata, csize);
	}
	else if (hdr->algo == 8)
	{
		if (g_debug > 0)
			std::cout << "Decompress LZVN compressed resource file ..." << std::endl;

		std::vector<uint8_t> rsrc;
		size_t k;

		bool rc = dir.GetAttribute(rsrc, ino, "com.apple.ResourceFork");

		if (!rc)
		{
			decompressed.clear();
			return false;
		}

		const uint32_t *off_list = reinterpret_cast<const uint32_t *>(rsrc.data());

		decompressed.resize((hdr->size + 0xFFFF) & 0xFFFF0000);

		for (k = 0; (k << 16) < decompressed.size(); k++)
			lzvn_decode(decompressed.data() + (k << 16), 0x10000, rsrc.data() + off_list[k], off_list[k + 1] - off_list[k]);

		decompressed.resize(hdr->size);

		return rc;
	}

	else
	{
		if (g_debug > 0)
			std::cout << "DecompressFile: Unknown Algorithm " << hdr->algo << std::endl;

		decompressed = compressed;
	}
#else
	decompressed = compressed;
#endif

	return true;
}
