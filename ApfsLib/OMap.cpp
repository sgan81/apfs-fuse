/*
 *	This file is part of apfs-fuse, a read-only implementation of APFS
 *	(Apple File System) for FUSE.
 *	Copyright (C) 2023 Simon Gander
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

#include <cassert>
#include <cinttypes>

#include "ObjCache.h"
#include "OMap.h"
#include "Container.h"
#include "Volume.h"
#include "Util.h"
#include "Debug.h"

#define log_debug(...)

int CompareOMapKey(const void *skey, size_t skey_len, const void *ekey, size_t ekey_len, uint64_t context, int& res)
{
	(void)context;

	if (skey_len != sizeof(omap_key_t)) return EINVAL;
	if (ekey_len != sizeof(omap_key_t)) return EINVAL;

	const omap_key_t *skey_map = reinterpret_cast<const omap_key_t *>(skey);
	const omap_key_t *ekey_map = reinterpret_cast<const omap_key_t *>(ekey);

	if (ekey_map->ok_oid < skey_map->ok_oid)
		res = -1;
	else if (ekey_map->ok_oid > skey_map->ok_oid)
		res = 1;
	else if (ekey_map->ok_xid < skey_map->ok_xid)
		res = -1;
	else if (ekey_map->ok_xid > skey_map->ok_xid)
		res = 1;
	else
		res = 0;
	return 0;
}

OMap::OMap()
{
	om_phys = nullptr;
}

OMap::~OMap()
{
}

int OMap::init(const void* params)
{
	(void)params;
	om_phys = reinterpret_cast<const omap_phys_t*>(data());
	return 0;
}

int OMap::lookup(oid_t oid, xid_t xid, xid_t* xid_o, uint32_t* flags, uint32_t* size, paddr_t* paddr)
{
	if (!m_tree.isValid()) {
		int err;
		Object* owner;

		owner = fs();
		if (owner == nullptr)
			owner = nx();

		err = m_tree.Init(owner, om_phys->om_tree_oid, 0, om_phys->om_tree_type, OBJECT_TYPE_OMAP, CompareOMapKey, 0);
		if (err) {
			log_error("omap: failed to init tree, err = %d\n", err);
			return err;
		}
	}

	omap_key_t ok;
	omap_val_t ov;
	uint16_t key_len = sizeof(omap_key_t);
	uint16_t val_len = sizeof(omap_val_t);
	int err;

	if (xid == 0) xid = UINT64_MAX;

	ok.ok_oid = oid;
	ok.ok_xid = xid;

	err = m_tree.Lookup(&ok, sizeof(omap_key_t), key_len, &ov, val_len, BTree::FindMode::LE);
	if (err) {
		log_error("omap lookup %" PRIx64 "/%" PRIx64 " failed, err = %d.\n", oid, xid, err);
		return err;
	}

	if (ov.ov_flags & OMAP_VAL_DELETED)
		return ENOENT;

	if (ok.ok_oid != oid) return ENOENT;
	if (xid_o) *xid_o = ok.ok_xid;
	if (flags) *flags = ov.ov_flags;
	if (size) *size = ov.ov_size;
	if (paddr) *paddr = ov.ov_paddr;

	log_debug("omap lookup oid %" PRIx64 " xid %" PRIx64 " => xid %" PRIx64 " flags %x size %x paddr %" PRIx64 "\n", oid, xid, ok.ok_xid, ov.ov_flags, ov.ov_size, ov.ov_paddr);

	return 0;
}

void OMap::dump_tree()
{
	if (!m_tree.isValid()) {
		int err;
		Object* owner;

		owner = fs();
		if (owner == nullptr)
			owner = nx();

		err = m_tree.Init(owner, om_phys->om_tree_oid, 0, om_phys->om_tree_type, OBJECT_TYPE_OMAP, CompareOMapKey, 0);
		if (err) {
			log_error("omap: failed to init tree, err = %d\n", err);
			return;
		}
	}

	dbg_dump_btree(m_tree);
}
