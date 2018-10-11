#pragma once

#include <vector>

#include "DiskStruct.h"
#include "ApfsNodeMapper.h"

class ApfsContainer;
class BlockDumper;

class CheckPointMap : public ApfsNodeMapper
{
public:
	CheckPointMap(ApfsContainer &container);
	virtual ~CheckPointMap();

	bool Init(uint64_t root_oid, uint32_t blk_count);
	bool GetBlockID(node_info_t &info, uint64_t oid, uint64_t xid) override;

	void dump(BlockDumper &bd);

private:
	ApfsContainer &m_container;
	std::vector<uint8_t> m_cpm_data;
	uint64_t m_cpm_oid;
	uint32_t m_blksize;
};
