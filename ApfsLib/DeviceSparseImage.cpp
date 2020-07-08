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

#include <cstring>

#include "Endian.h"

#include "DeviceSparseImage.h"

#ifdef _MSC_VER
#pragma pack(push, 4)
#define __attribute__(x)
#endif

struct HeaderNode
{
	be_uint32_t signature;
	be_uint32_t version;
	be_uint32_t sectors_per_band;
	be_uint32_t flags;
	be_uint32_t total_sectors_low;
	be_uint64_t next_node_offset;
	be_uint64_t total_sectors;
	be_uint32_t padding[7];
	be_uint32_t band_id[0x3F0];
} __attribute__((packed, aligned(4)));

struct IndexNode
{
	be_uint32_t magic;
	be_uint32_t index_node_number;
	be_uint32_t flags;
	be_uint64_t next_node_offset;
	be_uint32_t padding[9];
	be_uint32_t band_id[0x3F2];
} __attribute__((packed, aligned(4)));

#ifdef _MSC_VER
#pragma pack(pop)
#undef __attribute__
#endif

constexpr int SECTOR_SIZE = 0x200;
constexpr size_t NODE_SIZE = 0x1000;
constexpr size_t BAND_SIZE = 0x100000;
constexpr uint32_t SPRS_SIGNATURE = 0x73707273;

DeviceSparseImage::DeviceSparseImage()
{
	   m_band_size = 0;
	m_size = 0;
}

DeviceSparseImage::~DeviceSparseImage()
{
	m_img.Close();
	m_img.Reset();
}

bool DeviceSparseImage::Open(const char * name)
{
	if (!m_img.Open(name))
		return false;

	if (!m_img.CheckSetupEncryption())
	{
		m_img.Close();
		m_img.Reset();
		return false;
	}

	HeaderNode hdr;
	IndexNode idx;
	uint32_t off;
	uint64_t base;
	uint64_t next;
	size_t k;

	m_img.Read(0, &hdr, sizeof(hdr));

	if (hdr.signature != SPRS_SIGNATURE)
	{
		m_img.Close();
		m_img.Reset();
		return false;
	}

	m_size = hdr.total_sectors * SECTOR_SIZE;
	   m_band_size = hdr.sectors_per_band * SECTOR_SIZE;

	   m_band_offset.resize((m_size + m_band_size - 1) / m_band_size, 0);

	base = 0x1000;

	for (k = 0; k < 0x3F0; k++)
	{
		off = hdr.band_id[k];
		if (off)
			         m_band_offset[off - 1] = base + m_band_size * k;
	}

	next = hdr.next_node_offset;
	base = next + NODE_SIZE;

	while (next)
	{
		m_img.Read(next, &idx, sizeof(idx));

		if (hdr.signature != SPRS_SIGNATURE)
		{
			m_img.Close();
			m_img.Reset();
			return false;
		}

		for (k = 0; k < 0x3F2; k++)
		{
			off = idx.band_id[k];
			if (off)
				            m_band_offset[off - 1] = base + m_band_size * k;
		}

		next = idx.next_node_offset;
		base = next + NODE_SIZE;
	}

	return true;
}

void DeviceSparseImage::Close()
{
	m_img.Close();
	m_img.Reset();
}

bool DeviceSparseImage::Read(void * data, uint64_t offs, uint64_t len)
{
	uint32_t chunk;
	uint32_t chunk_offs;
	uint64_t chunk_base;
	size_t read_size;
	uint8_t *bdata = reinterpret_cast<uint8_t *>(data);

	while (len > 0)
	{
		chunk = offs >> 20; // TODO
		chunk_offs = offs & (m_band_size - 1);

		read_size = len;

		if ((chunk_offs + read_size) > m_band_size)
			read_size = m_band_size - chunk_offs;

		chunk_base = m_band_offset[chunk];

		if (chunk_base)
			m_img.Read(chunk_base + chunk_offs, bdata, read_size);
		else
			memset(bdata, 0, read_size);

		len -= read_size;
		offs += read_size;
		bdata += read_size;
	}

	return true;
}

uint64_t DeviceSparseImage::GetSize() const
{
	return m_size;
}
