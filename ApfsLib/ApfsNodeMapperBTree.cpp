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
#include <cinttypes>

#include "ApfsNodeMapperBTree.h"
#include "Container.h"
#include "Util.h"

static int CompareOMapKey(const void *skey, size_t skey_len, const void *ekey, size_t ekey_len, const void *context)
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

ApfsNodeMapperBTree::ApfsNodeMapperBTree(Container &container) :
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

	return m_tree.Init(m_omap.om_tree_oid, xid, CompareOMapKey, nullptr);
}

bool ApfsNodeMapperBTree::Lookup(omap_res_t &omr, oid_t oid, xid_t xid)
{
	omap_key_t ok;
	omap_val_t ov;
	uint16_t ok_len = sizeof(omap_key_t);
	uint16_t ov_len = sizeof(omap_val_t);
	int err;

	ok.ok_oid = oid;
	ok.ok_xid = xid;
	memset(&omr, 0, sizeof(omap_res_t));

	log_debug("omap lookup oid %" PRIx64 " xid %" PRIx64, oid, xid);

	err = m_tree.Lookup(&ok, sizeof(omap_key_t), ok_len, &ov, ov_len, BTree::FindMode::LE);
	if (err != 0 && err != ENOENT) {
		log_error("omap lookup oid %" PRIx64 " xid %" PRIx64 " error %d.\n", oid, xid, err);
		return false; // TODO ...
	}

	if (err == ENOENT || ok.ok_oid != oid) {
		log_error("omap lookup oid %" PRIx64 " xid %" PRIx64 " not found.\n", oid, xid);
		return false;
	}

	assert(ov_len == sizeof(omap_val_t));

	omr.oid = ok.ok_oid;
	omr.xid = ok.ok_xid;
	omr.flags = ov.ov_flags;
	omr.size = ov.ov_size;
	omr.paddr = ov.ov_paddr;

	log_debug(" => oid %" PRIx64 " xid %" PRIx64 " / flags %x size %x paddr %" PRIx64 "\n", omr.oid, omr.xid, omr.flags, omr.size, omr.paddr);

	return true;
}
