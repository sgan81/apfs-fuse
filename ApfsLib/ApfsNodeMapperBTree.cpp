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
	(void)skey_len;
	(void)ekey_len;

	assert(skey_len == sizeof(APFS_OMap_Key));
	assert(ekey_len == sizeof(APFS_OMap_Key));

	const APFS_OMap_Key *skey_map = reinterpret_cast<const APFS_OMap_Key *>(skey);
	const APFS_OMap_Key *ekey_map = reinterpret_cast<const APFS_OMap_Key *>(ekey);

	if (ekey_map->ok_oid < skey_map->ok_oid)
		return -1;
	if (ekey_map->ok_oid > skey_map->ok_oid)
		return 1;
	if (ekey_map->ok_xid < skey_map->ok_xid)
		return -1;
	if (ekey_map->ok_xid > skey_map->ok_xid)
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

bool ApfsNodeMapperBTree::Init(uint64_t bid_root, uint64_t xid)
{
	std::vector<byte_t> blk;

	blk.resize(m_container.GetBlocksize());

	if (!m_container.ReadAndVerifyHeaderBlock(blk.data(), bid_root))
	{
		std::cerr << "ERROR: Invalid header block 0x" << std::hex << bid_root << std::endl;
		return false;
	}

	memcpy(&m_root_ptr, blk.data(), sizeof(APFS_Block_4_B_BTreeRootPtr));

	if (APFS_OBJ_TYPE(m_root_ptr.hdr.o_type) != BlockType_BTreeRootPtr)
	{
		std::cerr << "ERROR: Wrong header type 0x" << std::hex << m_root_ptr.hdr.o_type << std::endl;
		return false;
	}

	return m_tree.Init(m_root_ptr.entry[0].blk, xid);
}

bool ApfsNodeMapperBTree::GetBlockID(node_info_t &info, uint64_t nid, uint64_t xid)
{
	APFS_OMap_Key key;

	const APFS_OMap_Key *res_key = nullptr;
	const APFS_OMap_Val *res_val = nullptr;

	BTreeEntry res;

	key.ok_oid = nid;
	key.ok_xid = xid;

	// std::cout << std::hex << "GetBlockID: nodeid = " << nodeid << ", version = " << version << " => blockid = ";

	if (!m_tree.Lookup(res, &key, sizeof(key), CompareNodeMapKey, this, false))
	{
		// std::cout << "NOT FOUND" << std::endl;
		std::cerr << std::hex << "nid " << nid << " xid " << xid << " NOT FOUND!!!" << std::endl;
		return false;
	}

	assert(res.val_len == sizeof(APFS_OMap_Val));

	res_key = reinterpret_cast<const APFS_OMap_Key *>(res.key);
	res_val = reinterpret_cast<const APFS_OMap_Val *>(res.val);

	if (key.ok_oid != res_key->ok_oid)
	{
		// std::cout << "NOT FOUND" << std::endl;
		std::cerr << std::hex << "nid " << nid << " xid " << xid << " NOT FOUND!!!" << std::endl;
		return false;
	}

	// std::cout << val->blockid << std::endl;

	info.flags = res_val->ov_flags;
	info.size = res_val->ov_size;
	info.bid = res_val->ov_paddr;

	return true;
}
