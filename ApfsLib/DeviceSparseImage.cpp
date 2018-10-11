#include <cstring>

#include "Endian.h"

#include "DeviceSparseImage.h"

#pragma pack(push, 4)

struct ImageHeader
{
	be<uint32_t> magic;
	be<uint32_t> unk_04;
	be<uint32_t> sectors_per_chunk;
	be<uint32_t> unk_0C;
	be<uint32_t> unk_10;
	be<uint64_t> next_offs;
	be<uint64_t> total_sectors;
	be<uint32_t> padding[7];
	be<uint32_t> chunk_pos[0x3F0];
};

struct SectionHeader
{
	be<uint32_t> magic;
	be<uint32_t> index;
	be<uint32_t> unk_08;
	be<uint64_t> next_offs;
	be<uint32_t> padding[9];
	be<uint32_t> chunk_pos[0x3F2];
};

#pragma pack(pop)

constexpr int SECTOR_SIZE = 0x200;
constexpr uint32_t SPRS_MAGIC = 0x73707273;

DeviceSparseImage::DeviceSparseImage()
{
	m_chunk_size = 0;
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

	ImageHeader ihdr;
	SectionHeader shdr;
	uint32_t off;
	uint64_t base;
	uint64_t next;
	size_t k;

	m_img.Read(0, &ihdr, sizeof(ihdr));

	if (ihdr.magic != SPRS_MAGIC)
	{
		m_img.Close();
		m_img.Reset();
		return false;
	}

	m_size = ihdr.total_sectors * SECTOR_SIZE;
	m_chunk_size = ihdr.sectors_per_chunk * SECTOR_SIZE;

	m_chunk_offs.resize((m_size + m_chunk_size - 1) / m_chunk_size, 0);

	base = 0x1000;

	for (k = 0; k < 0x3F0; k++)
	{
		off = ihdr.chunk_pos[k];
		if (off)
			m_chunk_offs[off - 1] = base + m_chunk_size * k;
	}

	next = ihdr.next_offs;
	base = next + 0x1000;

	while (next)
	{
		m_img.Read(next, &shdr, sizeof(shdr));

		if (ihdr.magic != SPRS_MAGIC)
		{
			m_img.Close();
			m_img.Reset();
			return false;
		}

		for (k = 0; k < 0x3F2; k++)
		{
			off = shdr.chunk_pos[k];
			if (off)
				m_chunk_offs[off - 1] = base + m_chunk_size * k;
		}

		next = shdr.next_offs;
		base = next + 0x1000;
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
		chunk_offs = offs & (m_chunk_size - 1);

		read_size = len;

		if ((chunk_offs + read_size) > m_chunk_size)
			read_size = m_chunk_size - chunk_offs;

		chunk_base = m_chunk_offs[chunk];

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
