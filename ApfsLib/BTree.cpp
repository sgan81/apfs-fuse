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
#include <cstdio>
#include <cinttypes>

#include <iostream>
#include <iomanip>

#include "ObjCache.h"
#include "Container.h"
#include "Volume.h"
#include "BTree.h"
#include "Util.h"
#include "BlockDumper.h"
#include "Debug.h"

// #define BTREE_DEBUG_VERBOSE
#define BTREE_DEBUG

int CompareU64Key(const void *skey, size_t skey_len, const void *ekey, size_t ekey_len, uint64_t context, int& res)
{
	if (skey_len != sizeof(uint64_t) || ekey_len != sizeof(uint64_t))
		return EINVAL;

	(void)context;

	uint64_t ks = *reinterpret_cast<const le_uint64_t *>(skey);
	uint64_t ke = *reinterpret_cast<const le_uint64_t *>(ekey);

	if (ke < ks)
		res = -1;
	if (ke > ks)
		res = 1;
	else
		res = 0;
	return 0;
}

BTreeNode::BTreeNode() : m_info()
{
	m_btn = nullptr;
	m_table = nullptr;
	m_keys = nullptr;
	m_vals = nullptr;

	m_cmp_func = nullptr;
	m_cmp_ctx = 0;
}

BTreeNode::~BTreeNode()
{
}

int BTreeNode::init(const void* params)
{
	if (params == nullptr)
		return EINVAL;

	const BTreeParams* bp = reinterpret_cast<const BTreeParams*>(params);
	const uint8_t* d = data();

	m_btn = reinterpret_cast<const btree_node_phys_t*>(d);
	m_table = d + sizeof(btree_node_phys_t);
	m_keys = d + sizeof(btree_node_phys_t) * m_btn->btn_table_space.len;
	m_vals = d + size();
	if (m_btn->btn_flags & BTNODE_ROOT)
		m_vals -= sizeof(btree_info_t);

	m_info = bp->info;
	m_cmp_func = bp->cmp_func;
	m_cmp_ctx = bp->cmp_ctx;

	assert(m_btn->btn_table_space.off == 0);

	return 0;
}

int BTreeNode::find_ge(const void* key, uint16_t key_len, int& index, bool& equal) const
{
#ifdef BTREE_DEBUG_VERBOSE
	static const char resstr[3] = { '<', '=', '>' };
#endif

	int beg;
	int end;
	int mid;
	const void* kptr;
	uint16_t klen;
	int err;
	int cmp_res;

	beg = 0;
	end = nkeys() - 1;

	index = -1;
	equal = false;

#ifdef BTREE_DEBUG_VERBOSE
	std::cout << "find_ge    : ";
	DumpHex(std::cout, reinterpret_cast<const uint8_t *>(key), key_len, key_len);
#endif

	while (beg <= end)
	{
		mid = (beg + end) >> 1;

		err = key_ptr_len(mid, kptr, klen);
		if (err) return err;

		// TODO aligned access ...
		err = m_cmp_func(key, key_len, kptr, klen, m_cmp_ctx, cmp_res);

#ifdef BTREE_DEBUG_VERBOSE
		std::cout << std::dec << std::setfill(' ');
		std::cout << std::setw(2) << beg << " [" << std::setw(2) << mid << "] " << std::setw(2) << end << " : " << resstr[rc + 1] << " : ";
		DumpHex(std::cout, reinterpret_cast<const uint8_t *>(kptr), klen, klen);
#endif

		if (cmp_res < 0)
			beg = mid + 1;
		else if (cmp_res > 0)
			end = mid - 1;
		else {
			index = mid;
			equal = true;
			return 0;
		}
	}

#ifdef BTREE_DEBUG_VERBOSE
	std::cout << std::dec << std::setfill(' ');
	std::cout << " => " << resstr[rc + 1] << ", " << mid;
#endif

	index = beg;
	equal = false;
	return 0;
}

int BTreeNode::find_le(const void* key, uint16_t key_len, int& index, bool& equal) const
{
	int err = find_ge(key, key_len, index, equal);
	if (err == 0 && equal == false && index > 0)
		index--;
	return err;
}

int BTreeNode::key_ptr_len(int index, const void *& kptr, uint16_t& klen) const
{
	if (index < 0 || static_cast<uint32_t>(index) >= m_btn->btn_nkeys) {
		kptr = nullptr;
		klen = 0;
		return ERANGE;
	}

	if (m_btn->btn_flags & BTNODE_FIXED_KV_SIZE) {
		const kvoff_t* e = reinterpret_cast<const kvoff_t*>(m_table);
		kptr = m_keys + e[index].k;
		klen = m_info.bt_key_size;
	} else {
		const kvloc_t* e = reinterpret_cast<const kvloc_t*>(m_table);
		kptr = m_keys + e[index].k.off;
		klen = e[index].k.len;
	}

	return 0;
}

int BTreeNode::val_ptr_len(int index, const void *& vptr, uint16_t& vlen) const
{
	if (index < 0 || static_cast<uint32_t>(index) >= m_btn->btn_nkeys) {
		vptr = nullptr;
		vlen = 0;
		return ERANGE;
	}

	uint16_t vo;
	uint16_t vl;

	if (m_btn->btn_flags & BTNODE_FIXED_KV_SIZE) {
		const kvoff_t* e = reinterpret_cast<const kvoff_t*>(m_table);
		vo = e[index].v;
		if (m_btn->btn_flags & BTNODE_LEAF)
			vl = m_info.bt_val_size;
		else
			vl = sizeof(uint64_t); // TODO + hash len
	} else {
		const kvloc_t* e = reinterpret_cast<const kvloc_t*>(m_table);
		vo = e[index].v.off;
		vl = e[index].v.len;
	}

	if (vo != BTOFF_INVALID) {
		vptr = m_vals - vo;
		vlen = vl;
	} else {
		vptr = nullptr;
		vlen = 0;
	}

	return 0;
}

int BTreeNode::child_val(int index, btn_index_node_val_t& binv) const
{
	const void *vptr;
	uint16_t vlen;
	int err;

	err = val_ptr_len(index, vptr, vlen);
	if (err) return err;

	memcpy(&binv, vptr, sizeof(oid_t)); // TODO: Hashed nodes
	return 0;
}

BTree::BTree() : m_treeinfo(), m_params()
{
	m_nx = nullptr;
	m_fs = nullptr;
}

BTree::~BTree()
{
}

int BTree::Init(Object* owner, oid_t oid, xid_t xid, uint32_t type, uint32_t subtype, BTCompareFunc cmp_func, uint64_t cmp_ctx)
{
	int err;

	if (owner->type() == OBJECT_TYPE_FS) {
		m_fs = static_cast<Volume*>(owner);
		m_nx = &m_fs->getContainer();
	} else {
		m_fs = nullptr;
		m_nx = static_cast<Container*>(owner);
	}

	m_params.cmp_func = cmp_func;
	m_params.cmp_ctx = cmp_ctx;

	if (oid == 0)
		return EINVAL;

	err = m_nx->cache().getObj(m_root, &m_params, oid, xid, type, subtype, m_nx->GetBlocksize(), 0, m_fs);
	if (err) {
		log_error("BTree init: unable to get root node, error = %d\n", err);
		return err;
	}

	memcpy(&m_treeinfo, m_root->data() + m_root->size() - sizeof(btree_info_t), sizeof(btree_info_t));
	m_params.info = m_treeinfo.bt_fixed;

	return 0;
}

int BTree::LookupFirst(void* key, uint16_t& key_len, void* val, uint16_t& val_len)
{
	if (!m_root)
		return EINVAL;

	ObjPtr<BTreeNode> node(m_root);
	btn_index_node_val_t binv = {};
	bool ok;
	int err;
	const void* kptr;
	const void* vptr;
	uint16_t klen;
	uint16_t vlen;

	while (node->level() > 0) {
		ok = node->child_val(0, binv);
		if (!ok) return ENOENT;
		err = GetNode(node, binv);
		if (err) {
			log_error("Unable to get child node %" PRIx64 ", error = %d\n", binv.binv_child_oid, err);
			return err;
		}
	}

	ok = node->key_ptr_len(0, kptr, klen);
	if (!ok) return EINVAL;
	node->val_ptr_len(0, vptr, vlen);
	if (!ok) return EINVAL;
	if (klen > key_len)
		return ERANGE;
	else
		key_len = klen;
	if (vlen > val_len)
		return ERANGE;
	else
		val_len = vlen;
	memcpy(key, kptr, klen);
	if (vlen > 0)
		memcpy(val, vptr, vlen);

	return 0;
}

int BTree::Lookup(void* key, uint16_t srch_key_len, uint16_t& key_len, void* val, uint16_t& val_len, FindMode mode)
{
	if (!m_root)
		return EINVAL;

	int err;
	int index;
	bool equal;
	btn_index_node_val_t binv = {};
	btn_index_node_val_t binv_nb = {};
	const void* kptr;
	const void* vptr;
	uint16_t klen;
	uint16_t vlen;

	ObjPtr<BTreeNode> node(m_root);

#ifdef BTREE_DEBUG
	static const char* modestr[5] = { "EQ", "LE", "LT", "GE", "GT" };
	// std::cout << std::hex << "BTree::Lookup: key=" << *reinterpret_cast<const uint64_t *>(key) << " root=" << node->nodeid() << std::endl;
	// std::cout << "BTree::Lookup: ";
	log_debug("btree lookup %s : ", modestr[static_cast<unsigned>(mode)]);
	if (node->subtype() == OBJECT_TYPE_FSTREE || node->subtype() == 0)
		dbg_print_btkey_fs(key, srch_key_len, true);
	else
		DumpHex(std::cout, reinterpret_cast<const uint8_t *>(key), srch_key_len, srch_key_len);
#endif

	while (true) {
		while (node->level() > 0) {
			err = node->find_le(key, srch_key_len, index, equal);
			if (err) return err;

			if (index > 0 && mode == FindMode::LT) {
				err = node->child_val(index - 1, binv_nb);
				assert(err == 0);
				if (err) return err;
			}
			else if (static_cast<uint32_t>(index + 1) < node->nkeys() && (mode == FindMode::GE || mode == FindMode::GT)) {
				err = node->child_val(index + 1, binv_nb);
				assert(err == 0);
				if (err) return err;
			}

			err = node->child_val(index, binv);
			assert(err == 0);
			if (err) return err;

			err = GetNode(node, binv);
			if (err) {
				log_error("BTree lookup: error getting child node %" PRIx64 ", err = %d\n", binv.binv_child_oid, err);
				return EINVAL;
			}
		}

		err = node->find_ge(key, srch_key_len, index, equal);
		if (err) return err;

		if (!equal) {
			if (mode == FindMode::EQ) {
				return ENOENT;
			} else if (mode == FindMode::LE || mode == FindMode::LT) {
				if (index > 0) {
					index--;
					break;
				}
			} else if (mode == FindMode::GE || mode == FindMode::GT) {
				if (static_cast<uint32_t>(index) < node->nkeys())
					break;
			}
		} else {
			if (mode == FindMode::LT) {
				if (index > 0) {
					index--;
					break;
				}
			} else if (mode == FindMode::GT) {
				if (static_cast<uint32_t>(index + 1) < node->nkeys()) {
					index++;
					break;
				}
			} else
				break;
		}

		if (binv_nb.binv_child_oid == 0)
			return ENOENT;
		err = GetNode(node, binv_nb);
		if (err) {
			log_error("BTree lookup: error getting nb node %" PRIx64 ", err = %d\n", binv_nb.binv_child_oid, err);
			return err;
		}
		binv_nb.binv_child_oid = 0;
	}

	if (node->nkeys() == 0)
		return ENOENT;
	else {
		err = node->key_ptr_len(index, kptr, klen);
		if (err) return err;

		err = node->val_ptr_len(index, vptr, vlen);
		if (err) return err;

		if (klen > key_len)
			return ERANGE;
		else
			key_len = klen;

		if (vlen > val_len)
			return ERANGE;
		else
			val_len = vlen;

		memcpy(key, kptr, klen);
		if (vlen > 0)
			memcpy(val, vptr, vlen);
	}

	return 0;
}

void BTree::dump(BlockDumper& out)
{
	if (m_root)
		DumpTreeInternal(out, m_root);
}

void BTree::DumpTreeInternal(BlockDumper& out, const ObjPtr<BTreeNode> &node)
{
	size_t k;
	size_t cnt;
	ObjPtr<BTreeNode> child;
	btn_index_node_val_t binv;
	int err;

	if (!node)
		return;

	out.DumpNode(node->data(), node->paddr());

	if (node->level() > 0)
	{
		cnt = node->nkeys();

		for (k = 0; k < cnt; k++)
		{
			node->child_val(k, binv);

			err = GetNode(child, binv);
			if (err == 0) {
				DumpTreeInternal(out, child);
			} else {
				log_error("Error %d getting child node.\n", err);
			}
		}
	}
}

int BTree::GetNode(ObjPtr<BTreeNode>& ptr, const btn_index_node_val_t& binv)
{
	Object* node;
	oid_t oid;
	uint32_t otype;
	int err;

	if (!m_root)
		return EINVAL;

	otype = (m_root->type_and_flags() & OBJECT_TYPE_FLAGS_MASK) | OBJECT_TYPE_BTREE_NODE;

	oid = binv.binv_child_oid;
	if (m_root->flags() & BTNODE_HASHED)
		oid += m_root->oid();

	err = m_nx->cache().getObj(node, &m_params, oid, m_root->xid(), otype, m_root->subtype(), m_treeinfo.bt_fixed.bt_node_size, 0, m_root->fs());
	if (err) {
		log_error("BTree: error %d getting node.\n", err);
		return err;
	}

	ptr = node;
	return 0;
}

BTreeIterator::BTreeIterator()
{
	m_tree = nullptr;
	m_key_buf = nullptr;
	m_val_buf = nullptr;
	m_key_buf_len = 0;
	m_val_buf_len = 0;
	m_key_len = 0;
	m_val_len = 0;
}

BTreeIterator::~BTreeIterator()
{
}

int BTreeIterator::init(BTree* tree, void* key_buf, uint16_t key_len, uint16_t key_buf_len, void* val_buf, uint16_t val_buf_len, BTree::FindMode mode)
{
	int err;

	m_tree = tree;
	m_key_buf = key_buf;
	m_val_buf = val_buf;
	m_key_buf_len = key_buf_len;
	m_val_buf_len = val_buf_len;
	m_key_len = key_buf_len;
	m_val_len = val_buf_len;

	err = m_tree->Lookup(m_key_buf, key_len, m_key_len, m_val_buf, m_val_len, mode);

	return err;
}

int BTreeIterator::initFirst(BTree* tree, void* key_buf, uint16_t key_buf_len, void* val_buf, uint16_t val_buf_len)
{
	int err;

	m_tree = tree;
	m_key_buf = key_buf;
	m_val_buf = val_buf;
	m_key_buf_len = key_buf_len;
	m_val_buf_len = val_buf_len;
	m_key_len = key_buf_len;
	m_val_len = val_buf_len;

	log_debug("iterator init\n");
	err = m_tree->LookupFirst(m_key_buf, m_key_len, m_val_buf, m_val_len);

	return err;
}

bool BTreeIterator::next()
{
	uint16_t skey_len = m_key_len;
	int err;

	m_key_len = m_key_buf_len;
	m_val_len = m_val_buf_len;

	log_debug("iterator next\n");
	err = m_tree->Lookup(m_key_buf, skey_len, m_key_len, m_val_buf, m_val_len, BTree::FindMode::GT);
	return err == 0;
}
