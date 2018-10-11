#include "DeviceVDI.h"

#pragma pack(1)

struct VdiDiskGeometry
{
	le<uint32_t> cylinders;
	le<uint32_t> heads;
	le<uint32_t> sectors;
	le<uint32_t> sector_size;
};

struct VdiPreHeader
{
	char file_info[0x40];
	le<uint32_t> signature; // 0xBEDA107F
	le<uint32_t> version; // 0x00010001
};

struct VdiHeader1Plus
{
	le<uint32_t> struct_size;
	le<uint32_t> image_type;
	le<uint32_t> flags;
	char comment[0x100];
	le<uint32_t> off_blocks;
	le<uint32_t> off_data;
	VdiDiskGeometry legacy_geometry;
	le<uint32_t> dummy;
	le<uint64_t> disk_size;
	le<uint32_t> block_size;
	le<uint32_t> block_extra;
	le<uint32_t> blocks_total;
	le<uint32_t> blocks_allocated;
	char uuid_create[0x10];
	char uuid_modify[0x10];
	char uuid_linkage[0x10];
	char uuid_parent_modify[0x10];
	VdiDiskGeometry lchs_geometry;
};

#pragma pack()


DeviceVDI::DeviceVDI()
{
	m_disk_size = 0;
	m_block_size = 0;
	m_block_count = 0;
	m_data_offset = 0;

	m_vdi = nullptr;
}

DeviceVDI::~DeviceVDI()
{
}

bool DeviceVDI::Open(const char * name)
{
	VdiPreHeader phdr;
	VdiHeader1Plus hdr;

	Close();

	fopen_s(&m_vdi, name, "rb");
	if (m_vdi)
	{
		if (fread(&phdr, sizeof(phdr), 1, m_vdi) == 1)
		{
			if (fread(&hdr, sizeof(hdr), 1, m_vdi) == 1)
			{
				if (phdr.signature == 0xBEDA107F && phdr.version == 0x00010001)
				{
					m_disk_size = hdr.disk_size;
					m_block_size = hdr.block_size;
					m_block_count = hdr.blocks_total;
					m_data_offset = hdr.off_data;

					m_block_map.resize(m_block_count, 0xFF);

					fseek(m_vdi, hdr.off_blocks, SEEK_SET);

					fread(m_block_map.data(), sizeof(uint32_t), m_block_count, m_vdi);

					fseek(m_vdi, 0, SEEK_END);

					return true;
				}
			}
		}
	}

	Close();

	return false;
}

void DeviceVDI::Close()
{
	if (m_vdi)
	{
		fclose(m_vdi);
		m_vdi = nullptr;
	}
}

bool DeviceVDI::Read(void * data, uint64_t offs, uint64_t len)
{
	uint64_t block_nr;
	uint64_t block_offs;
	uint64_t vdi_offs;
	uint32_t map_entry;

	uint64_t read_size;
	uint8_t *pdata = reinterpret_cast<uint8_t *>(data);

	if (!m_vdi)
		return false;

	while (len > 0)
	{
		block_nr = offs >> 20;
		block_offs = offs & 0xFFFFF;

		map_entry = m_block_map[block_nr];

		read_size = m_block_size - block_offs;

		if (read_size > len)
			read_size = len;

		if (map_entry == 0xFFFFFFFF)
		{
			memset(pdata, 0, read_size);
		}
		else
		{
			fseek(m_vdi, map_entry * m_block_size + m_data_offset + block_offs, SEEK_SET);
			fread(pdata, 1, read_size, m_vdi);
		}

		pdata += read_size;
		offs += read_size;
		len -= read_size;
	}

	return true;
}

uint64_t DeviceVDI::GetSize() const
{
	return m_disk_size;
}
