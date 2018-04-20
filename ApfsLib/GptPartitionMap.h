#pragma once

#include <cstdint>
#include <vector>

#include "Endian.h"
#include "Crc32.h"

class Device;

class GptPartitionMap
{
public:
	GptPartitionMap();

	bool LoadAndVerify(Device &dev);

	int FindFirstAPFSPartition();
	bool GetPartitionOffsetAndSize(int partnum, uint64_t &offset, uint64_t &size);

	// void ListEntries();

private:
	Crc32 m_crc;

	std::vector<uint8_t> m_gpt_data;
};
