#pragma once

#include <vector>

#include "DiskStruct.h"
#include "ApfsNodeMapper.h"

class Container;
class BlockDumper;

class CheckPointMap : public ApfsNodeMapper
{
public:
	CheckPointMap(Container &container);
	virtual ~CheckPointMap();

	bool Init(oid_t root_oid, uint32_t blk_count);
	bool Lookup(omap_res_t & res, oid_t oid, xid_t xid) override;

	void dump(BlockDumper &bd);

private:
	Container &m_container;
	std::vector<uint8_t> m_cpm_data;
	oid_t m_cpm_oid;
	uint32_t m_blksize;
};
