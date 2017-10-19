#pragma once

#pragma pack(1)

#include <cstdint>

struct PM_GUID
{
	uint32_t Data1;
	uint16_t Data2;
	uint16_t Data3;
	uint8_t  Data4[8];
};

struct PMAP_GptHeader
{
	uint64_t Signature;
	uint32_t Revision;
	uint32_t HeaderSize;
	uint32_t HeaderCRC32;
	uint32_t Reserved;
	uint64_t MyLBA;
	uint64_t AlternateLBA;
	uint64_t FirstUsableLBA;
	uint64_t LastUsableLBA;
	PM_GUID  DiskGUID;
	uint64_t PartitionEntryLBA;
	uint32_t NumberOfPartitionEntries;
	uint32_t SizeOfPartitionEntry;
	uint32_t PartitionEntryArrayCRC32;
};

struct PMAP_Entry
{
	PM_GUID  PartitionTypeGUID;
	PM_GUID  UniquePartitionGUID;
	uint64_t StartingLBA;
	uint64_t EndingLBA;
	uint64_t Attributes;
	char16_t PartitionName[36];
};

static_assert(sizeof(PMAP_Entry) == 128, "PMAP Entry wrong size");

#pragma pack()
