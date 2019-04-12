#pragma once

#include <cstdint>
#include <vector>

#include "Endian.h"
#include "Crc32.h"

class Device;

struct PMAP_GptHeader;
struct PMAP_Entry;

class GptPartitionMap
{
public:
	GptPartitionMap();

	bool LoadAndVerify(Device &dev);

	int FindFirstAPFSPartition();
	bool GetPartitionOffsetAndSize(int partnum, uint64_t &offset, uint64_t &size);

	void ListEntries();

private:
	Crc32 m_crc;

	std::vector<uint8_t> m_hdr_data;
	std::vector<uint8_t> m_entry_data;

	const PMAP_GptHeader *m_hdr;
	const PMAP_Entry *m_map;
	unsigned int m_sector_size;
};
