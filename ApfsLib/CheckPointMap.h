/*
 *	This file is part of apfs-fuse, a read-only implementation of APFS
 *	(Apple File System) for FUSE.
 *	Copyright (C) 2017 Simon Gander
 *
 *	Apfs-fuse is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Apfs-fuse is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with apfs-fuse.  If not, see <http://www.gnu.org/licenses/>.
 */

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
