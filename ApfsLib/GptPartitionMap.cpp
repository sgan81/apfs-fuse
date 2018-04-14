#include <cstdio>
#include <cstring>

#include "Device.h"
#include "GptPartitionMap.h"

typedef uint8_t PM_GUID[0x10];

#pragma pack(1)

struct PMAP_GptHeader
{
	le<uint64_t> Signature;
	le<uint32_t> Revision;
	le<uint32_t> HeaderSize;
	le<uint32_t> HeaderCRC32;
	le<uint32_t> Reserved;
	le<uint64_t> MyLBA;
	le<uint64_t> AlternateLBA;
	le<uint64_t> FirstUsableLBA;
	le<uint64_t> LastUsableLBA;
	PM_GUID      DiskGUID;
	le<uint64_t> PartitionEntryLBA;
	le<uint32_t> NumberOfPartitionEntries;
	le<uint32_t> SizeOfPartitionEntry;
	le<uint32_t> PartitionEntryArrayCRC32;
};

struct PMAP_Entry
{
	PM_GUID      PartitionTypeGUID;
	PM_GUID      UniquePartitionGUID;
	le<uint64_t> StartingLBA;
	le<uint64_t> EndingLBA;
	le<uint64_t> Attributes;
	le<uint16_t> PartitionName[36];
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
}

bool GptPartitionMap::LoadAndVerify(Device & dev)
{
	std::vector<uint8_t> hdr_data;

	PMAP_GptHeader *hdr;

	hdr_data.resize(0x1000);

	dev.Read(hdr_data.data(), 0, 0x1000);

	hdr = reinterpret_cast<PMAP_GptHeader *>(hdr_data.data() + 0x200);

	if (hdr->Signature != 0x5452415020494645)
		return false;

	if (hdr->Revision != 0x00010000)
		return false;

	if (hdr->HeaderSize > 0x200)
		return false;

	if (hdr->SizeOfPartitionEntry != 0x80)
		return false;

	if (hdr->PartitionEntryLBA != 2)
		return false;

	uint32_t hdr_crc;
	uint32_t calc_crc;

	hdr_crc = hdr->HeaderCRC32;
	hdr->HeaderCRC32 = 0;

	m_crc.SetCRC(0xFFFFFFFF);
	m_crc.Calc(hdr_data.data() + 0x200, hdr->HeaderSize);
	calc_crc = m_crc.GetCRC() ^ 0xFFFFFFFF;

	if (calc_crc != hdr_crc)
		return false;

	size_t mapsize;

	mapsize = (0x200 * hdr->PartitionEntryLBA) + (hdr->NumberOfPartitionEntries * hdr->SizeOfPartitionEntry);
	mapsize = (mapsize + 0xFFF) & ~0xFFF;

	hdr_data.clear();
	hdr_data.resize(mapsize);
	dev.Read(hdr_data.data(), 0, hdr_data.size());

	hdr = reinterpret_cast<PMAP_GptHeader *>(hdr_data.data() + 0x200);

	m_crc.SetCRC(0xFFFFFFFF);
	m_crc.Calc(hdr_data.data() + (0x200 * hdr->PartitionEntryLBA), hdr->SizeOfPartitionEntry * hdr->NumberOfPartitionEntries);
	calc_crc = m_crc.GetCRC() ^ 0xFFFFFFFF;

	if (calc_crc != hdr->PartitionEntryArrayCRC32)
		return false;

	m_gpt_data = hdr_data;

	return true;
}

int GptPartitionMap::FindFirstAPFSPartition()
{
	if (m_gpt_data.size() == 0)
		return -1;

	const PMAP_GptHeader *hdr = reinterpret_cast<const PMAP_GptHeader *>(m_gpt_data.data() + 0x200);
	const PMAP_Entry *entry = reinterpret_cast<const PMAP_Entry *>(m_gpt_data.data() + (0x200 * hdr->PartitionEntryLBA));
	int k;
	int rc = -1;

	for (k = 0; k < hdr->NumberOfPartitionEntries; k++)
	{
		if (entry[k].StartingLBA == 0 && entry[k].EndingLBA == 0)
			break;

		if (!memcmp(entry[k].PartitionTypeGUID, partitiontype_apfs, sizeof(PM_GUID)))
		{
			rc = k;
			break;
		}
	}

	return rc;
}

bool GptPartitionMap::GetPartitionOffsetAndSize(int partnum, uint64_t & offset, uint64_t & size)
{
	if (m_gpt_data.size() == 0)
		return false;

	const PMAP_GptHeader *hdr = reinterpret_cast<const PMAP_GptHeader *>(m_gpt_data.data() + 0x200);
	const PMAP_Entry *entry = reinterpret_cast<const PMAP_Entry *>(m_gpt_data.data() + (0x200 * hdr->PartitionEntryLBA));

	offset = entry[partnum].StartingLBA * 0x200;
	size = (entry[partnum].EndingLBA - entry[partnum].StartingLBA + 1) * 0x200;

	return true;
}

void GptPartitionMap::ListEntries()
{
	if (m_gpt_data.size() == 0)
		return;

	const PMAP_GptHeader *hdr = reinterpret_cast<const PMAP_GptHeader *>(m_gpt_data.data() + 0x200);
	const PMAP_Entry *entry = reinterpret_cast<const PMAP_Entry *>(m_gpt_data.data() + (0x200 * hdr->PartitionEntryLBA));
	size_t k;
	size_t n;

	for (k = 0; k < hdr->NumberOfPartitionEntries; k++)
	{
		const PMAP_Entry &e = entry[k];

		if (e.StartingLBA == 0 && e.EndingLBA == 0)
			break;

		PrintGUID(e.PartitionTypeGUID);
		printf(" ");
		PrintGUID(e.UniquePartitionGUID);
		printf(" %016llX %016llX ", e.StartingLBA.get(), e.EndingLBA.get());
		printf("%016llX ", e.Attributes.get());

		for (n = 0; (n < 36) && (e.PartitionName[n] != 0); n++)
			printf("%c", e.PartitionName[n].get());

		printf("\n");
	}
}
