/*
	This file is part of apfs-fuse, a read-only implementation of APFS
	(Apple File System) for FUSE.
	Copyright (C) 2017 Simon Gander

	Apfs-fuse is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	Apfs-fuse is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with apfs-fuse.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include "DiskStruct.h"

#include "ApfsNodeMapper.h"
#include "BTree.h"

class BlockDumper;

class ApfsNodeMapperBTree : public ApfsNodeMapper
{
public:
	ApfsNodeMapperBTree(Container &container);
	virtual ~ApfsNodeMapperBTree();

	bool Init(oid_t omap_oid, xid_t xid);
	bool Lookup(omap_res_t & omr, oid_t oid, xid_t xid) override;

	void dump(BlockDumper &bd) { m_tree.dump(bd); }

private:
	omap_phys_t m_omap;
	BTree m_tree;

	Container &m_container;
};
