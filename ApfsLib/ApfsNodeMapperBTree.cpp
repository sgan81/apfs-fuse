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

static int CompareOMapKey(const void *skey, size_t skey_len, const void *ekey, size_t ekey_len, void *context)
{
	(void)context;
	(void)skey_len;
	(void)ekey_len;

	assert(skey_len == sizeof(omap_key_t));
	assert(ekey_len == sizeof(omap_val_t));

	const omap_key_t *skey_map = reinterpret_cast<const omap_key_t *>(skey);
	const omap_key_t *ekey_map = reinterpret_cast<const omap_key_t *>(ekey);

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

bool ApfsNodeMapperBTree::Init(oid_t omap_oid, xid_t xid)
{
	std::vector<uint8_t> blk;

	blk.resize(m_container.GetBlocksize());

	if (!m_container.ReadAndVerifyHeaderBlock(blk.data(), omap_oid))
	{
		std::cerr << "ERROR: Invalid omap block @ oid 0x" << std::hex << omap_oid << std::endl;
		return false;
	}

	memcpy(&m_omap, blk.data(), sizeof(omap_phys_t));

	if ((m_omap.om_o.o_type & OBJECT_TYPE_MASK) == OBJECT_TYPE_BTREE)
	{
		std::cerr << "ERROR: Wrong omap type 0x" << std::hex << m_omap.om_o.o_type << std::endl;
		return false;
	}

	return m_tree.Init(m_omap.om_tree_oid, xid);
}

bool ApfsNodeMapperBTree::Lookup(omap_res_t &omr, oid_t oid, xid_t xid)
{
	omap_key_t key;

	const omap_key_t *res_key = nullptr;
	const omap_val_t *res_val = nullptr;

	BTreeEntry res;

	key.ok_oid = oid;
	key.ok_xid = xid;

	// std::cout << std::hex << "GetBlockID: nodeid = " << nodeid << ", version = " << version << " => blockid = ";

	if (!m_tree.Lookup(res, &key, sizeof(key), CompareOMapKey, this, false))
	{
		// std::cout << "NOT FOUND" << std::endl;
		std::cerr << std::hex << "oid " << oid << " xid " << xid << " NOT FOUND!!!" << std::endl;
		return false;
	}

	assert(res.val_len == sizeof(omap_val_t));

	res_key = reinterpret_cast<const omap_key_t *>(res.key);
	res_val = reinterpret_cast<const omap_val_t *>(res.val);

	if (key.ok_oid != res_key->ok_oid)
	{
		// std::cout << "NOT FOUND" << std::endl;
		std::cerr << std::hex << "oid " << oid << " xid " << xid << " NOT FOUND!!!" << std::endl;
		return false;
	}

	// std::cout << val->blockid << std::endl;

	omr.flags = res_val->ov_flags;
	omr.size = res_val->ov_size;
	omr.paddr = res_val->ov_paddr;

	return true;
}
