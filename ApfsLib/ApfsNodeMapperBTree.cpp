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

#include <cassert>
#include <cstring>
#include <iostream>

#include "ApfsNodeMapperBTree.h"
#include "ApfsContainer.h"

static int CompareNodeMapKey(const void *skey, size_t skey_len, const void *ekey, size_t ekey_len, void *context)
{
	(void)context;

	assert(skey_len == sizeof(APFS_Key_B_NodeID_Map));
	assert(ekey_len == sizeof(APFS_Key_B_NodeID_Map));

	const APFS_Key_B_NodeID_Map *skey_map = reinterpret_cast<const APFS_Key_B_NodeID_Map *>(skey);
	const APFS_Key_B_NodeID_Map *ekey_map = reinterpret_cast<const APFS_Key_B_NodeID_Map *>(ekey);

	if (ekey_map->nodeid < skey_map->nodeid)
		return -1;
	if (ekey_map->nodeid > skey_map->nodeid)
		return 1;
	if (ekey_map->version < skey_map->version)
		return -1;
	if (ekey_map->version > skey_map->version)
		return 1;
	return 0;
}

ApfsNodeMapperBTree::ApfsNodeMapperBTree(ApfsContainer &container) :
	m_tree(container),
	m_container(container)
{
}

ApfsNodeMapperBTree::~ApfsNodeMapperBTree()
{
}

bool ApfsNodeMapperBTree::Init(uint64_t hdr_block_id, uint64_t version)
{
	std::vector<byte_t> blk;

	blk.resize(m_container.GetBlocksize());

	if (!m_container.ReadAndVerifyHeaderBlock(blk.data(), hdr_block_id))
		return false;

	memcpy(&m_root_ptr, blk.data(), sizeof(APFS_Block_4_B_BTreeRootPtr));

	if (m_root_ptr.hdr.type != 0x4000000B)
		return false;

	return m_tree.Init(m_root_ptr.entry[0].blk, version);
}

bool ApfsNodeMapperBTree::GetBlockID(node_info_t &info, uint64_t nodeid, uint64_t version)
{
	APFS_Key_B_NodeID_Map key;

	const APFS_Key_B_NodeID_Map *rkey = nullptr;
	const APFS_Value_B_NodeID_Map *val = nullptr;

	BTreeEntry res;

	key.nodeid = nodeid;
	key.version = version;

	// std::cout << std::hex << "GetBlockID: nodeid = " << nodeid << ", version = " << version << " => blockid = ";

	if (!m_tree.Lookup(res, &key, sizeof(key), CompareNodeMapKey, this, false))
	{
		// std::cout << "NOT FOUND" << std::endl;
		std::cerr << std::hex << "nodeid " << nodeid << " version " << version << " NOT FOUND!!!" << std::endl;
		return false;
	}

	assert(res.val_len == sizeof(APFS_Value_B_NodeID_Map));

	rkey = reinterpret_cast<const APFS_Key_B_NodeID_Map *>(res.key);
	val = reinterpret_cast<const APFS_Value_B_NodeID_Map *>(res.val);

	if (key.nodeid != rkey->nodeid)
	{
		// std::cout << "NOT FOUND" << std::endl;
		std::cerr << std::hex << "nodeid " << nodeid << " version " << version << " NOT FOUND!!!" << std::endl;
		return false;
	}

	// std::cout << val->blockid << std::endl;

	info.flags = val->flags;
	info.size = val->size;
	info.block_no = val->blockid;

	return true;
}
