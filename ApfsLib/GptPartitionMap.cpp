#include <cstdio>
#include <cstring>
#include <cinttypes>

#include "Device.h"
#include "GptPartitionMap.h"

typedef uint8_t PM_GUID[0x10];

#pragma pack(1)

struct PMAP_GptHeader
{
	le_uint64_t Signature;
	le_uint32_t Revision;
	le_uint32_t HeaderSize;
	le_uint32_t HeaderCRC32;
	le_uint32_t Reserved;
	le_uint64_t MyLBA;
	le_uint64_t AlternateLBA;
	le_uint64_t FirstUsableLBA;
	le_uint64_t LastUsableLBA;
	PM_GUID      DiskGUID;
	le_uint64_t PartitionEntryLBA;
	le_uint32_t NumberOfPartitionEntries;
	le_uint32_t SizeOfPartitionEntry;
	le_uint32_t PartitionEntryArrayCRC32;
};

struct PMAP_Entry
{
	PM_GUID      PartitionTypeGUID;
	PM_GUID      UniquePartitionGUID;
	le_uint64_t StartingLBA;
	le_uint64_t EndingLBA;
	le_uint64_t Attributes;
	le_uint16_t PartitionName[36];
};

static_assert(sizeof(PMAP_GptHeader) == 92, "PMAP GPT-Header wrong size");
static_assert(sizeof(PMAP_Entry) == 128, "PMAP Entry wrong size");

#pragma pack()

static const PM_GUID partitiontype_apfs = { 0xEF, 0x57, 0x34, 0x7C, 0x00, 0x00, 0xAA, 0x11, 0xAA, 0x11, 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC };

static void PrintGUID(const PM_GUID &guid)
{
	printf("%02X%02X%02X%02X-", guid[3], guid[2], guid[1], guid[0]);
	printf("%02X%02X-", guid[5], guid[4]);
	printf("%02X%02X-", guid[7], guid[6]);
	printf("%02X%02X-", guid[9], guid[8]);
	printf("%02X%02X%02X%02X%02X%02X", guid[10], guid[11], guid[12], guid[13], guid[14], guid[15]);
}

GptPartitionMap::GptPartitionMap() : m_crc(true)
{
	m_hdr = nullptr;
	m_map = nullptr;
	m_sector_size = 0x200;
}

bool GptPartitionMap::LoadAndVerify(Device & dev)
{
	PMAP_GptHeader *hdr;

	m_hdr = nullptr;
	m_map = nullptr;
	m_entry_data.clear();
	m_hdr_data.clear();

	m_sector_size = dev.GetSectorSize();

	m_hdr_data.resize(m_sector_size);

	dev.Read(m_hdr_data.data(), m_sector_size, m_sector_size);

	hdr = reinterpret_cast<PMAP_GptHeader *>(m_hdr_data.data());

	if (hdr->Signature != 0x5452415020494645) {
		m_hdr_data.resize(0x1000);
		dev.Read(m_hdr_data.data(), 0x1000, 0x1000);

		hdr = reinterpret_cast<PMAP_GptHeader *>(m_hdr_data.data());

		if (hdr->Signature != 0x5452415020494645)
			return false;

		m_sector_size = 0x1000;
	}

	if (hdr->Revision != 0x00010000)
		return false;

	if (hdr->HeaderSize > m_sector_size)
		return false;

	if (hdr->SizeOfPartitionEntry != 0x80)
		return false;

	uint32_t hdr_crc;
	uint32_t calc_crc;

	hdr_crc = hdr->HeaderCRC32;
	hdr->HeaderCRC32 = 0;

	m_crc.SetCRC(0xFFFFFFFF);
	m_crc.Calc(m_hdr_data.data(), hdr->HeaderSize);
	calc_crc = m_crc.GetCRC() ^ 0xFFFFFFFF;

	if (calc_crc != hdr_crc) {
		m_hdr_data.clear();
		return false;
	}

	size_t mapsize;

	mapsize = hdr->NumberOfPartitionEntries * hdr->SizeOfPartitionEntry;
	mapsize = (mapsize + m_sector_size - 1) & ~(static_cast<size_t>(m_sector_size) - 1);

	m_entry_data.resize(mapsize);
	dev.Read(m_entry_data.data(), m_sector_size * hdr->PartitionEntryLBA, m_entry_data.size());

	m_crc.SetCRC(0xFFFFFFFF);
	m_crc.Calc(m_entry_data.data(), hdr->SizeOfPartitionEntry * hdr->NumberOfPartitionEntries);
	calc_crc = m_crc.GetCRC() ^ 0xFFFFFFFF;

	if (calc_crc != hdr->PartitionEntryArrayCRC32) {
		m_entry_data.clear();
		m_hdr_data.clear();
		return false;
	}

	m_hdr = reinterpret_cast<const PMAP_GptHeader *>(m_hdr_data.data());
	m_map = reinterpret_cast<const PMAP_Entry *>(m_entry_data.data());

	return true;
}

int GptPartitionMap::FindFirstAPFSPartition()
{
	if (!m_hdr || !m_map)
		return -1;

	unsigned int k;
	int rc = -1;

	for (k = 0; k < m_hdr->NumberOfPartitionEntries; k++)
	{
		if (m_map[k].StartingLBA == 0 && m_map[k].EndingLBA == 0)
			break;

		if (!memcmp(m_map[k].PartitionTypeGUID, partitiontype_apfs, sizeof(PM_GUID)))
		{
			rc = k;
			break;
		}
	}

	return rc;
}

bool GptPartitionMap::GetPartitionOffsetAndSize(int partnum, uint64_t & offset, uint64_t & size)
{
	if (!m_hdr || !m_map)
		return false;

	offset = m_map[partnum].StartingLBA * m_sector_size;
	size = (m_map[partnum].EndingLBA - m_map[partnum].StartingLBA + 1) * m_sector_size;

	return true;
}

void GptPartitionMap::ListEntries()
{
	if (!m_hdr || !m_map)
		return;

	size_t k;
	size_t n;

	for (k = 0; k < m_hdr->NumberOfPartitionEntries; k++)
	{
		const PMAP_Entry &e = m_map[k];

		if (e.StartingLBA == 0 && e.EndingLBA == 0)
			break;

		PrintGUID(e.PartitionTypeGUID);
		printf(" ");
		PrintGUID(e.UniquePartitionGUID);
		printf(" %016" PRIX64 " %016" PRIX64 " ", e.StartingLBA, e.EndingLBA);
		printf("%016" PRIX64 " ", e.Attributes);

		for (n = 0; (n < 36) && (e.PartitionName[n] != 0); n++)
			printf("%c", e.PartitionName[n]);

		printf("\n");
	}
}
