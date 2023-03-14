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

#include "ApfsContainer.h"
#include "ApfsVolume.h"
#include "BTree.h"
#include "Util.h"
#include "BlockDumper.h"

#define BTREE_DEBUG

int CompareU64Key(const void *skey, size_t skey_len, const void *ekey, size_t ekey_len, void *context)
{
	// assert(skey_len == 8);
	// assert(ekey_len == 8);

	(void)skey_len;
	(void)ekey_len;
	(void)context;

	uint64_t ks = *reinterpret_cast<const le_uint64_t *>(skey);
	uint64_t ke = *reinterpret_cast<const le_uint64_t *>(ekey);

	if (ke < ks)
		return -1;
	if (ke > ks)
		return 1;
	return 0;
}

BTreeNode::BTreeNode(BTree &tree, const uint8_t *block, size_t blocksize, paddr_t paddr, BTCompareFunc cmp_func, const void* cmp_ctx) :
	m_tree(tree),
	m_paddr(paddr)
{
	m_cmp_func = cmp_func;
	m_cmp_ctx = cmp_ctx;

	m_block.assign(block, block + blocksize);
	m_btn = reinterpret_cast<const btree_node_phys_t *>(m_block.data());
	m_table = m_block.data() + sizeof(btree_node_phys_t);
	m_keys = m_block.data() + sizeof(btree_node_phys_t) + m_btn->btn_table_space.len;
	m_vals = m_block.data() + blocksize;
	if (m_btn->btn_flags & BTNODE_ROOT)
		m_vals -= sizeof(btree_info_t);

	assert(m_btn->btn_table_space.off == 0);
}

std::shared_ptr<BTreeNode> BTreeNode::CreateNode(BTree & tree, const uint8_t * block, size_t blocksize, paddr_t paddr, BTCompareFunc cmp_func, const void* cmp_ctx)
{
	return std::make_shared<BTreeNode>(tree, block, blocksize, paddr, cmp_func, cmp_ctx);
}

BTreeNode::~BTreeNode()
{
}

int BTreeNode::find_ge(const void* key, uint16_t key_len, int& index, bool& equal)
{
	static const char resstr[3] = { '<', '=', '>' };

	int beg;
	int end;
	int mid;
	const void* kptr;
	uint16_t klen;
	bool ok;

	int rc;

	beg = 0;
	end = nkeys() - 1;

	index = -1;
	equal = false;

#ifdef BTREE_DEBUG
	std::cout << "FindBin    : ";
	DumpHex(std::cout, reinterpret_cast<const uint8_t *>(key), key_len, key_len);
#endif

	while (beg <= end)
	{
		mid = (beg + end) >> 1;

		ok = key_ptr_len(mid, kptr, klen);
		if (!ok)
			return ENOENT;

		// TODO aligned access ...
		rc = m_cmp_func(key, key_len, kptr, klen, m_cmp_ctx);

#ifdef BTREE_DEBUG
		std::cout << std::dec << std::setfill(' ');
		std::cout << std::setw(2) << beg << " [" << std::setw(2) << mid << "] " << std::setw(2) << end << " : " << resstr[rc + 1] << " : ";
		DumpHex(std::cout, reinterpret_cast<const uint8_t *>(kptr), klen, klen);
#endif

		if (rc < 0)
			beg = mid + 1;
		else if (rc > 0)
			end = mid - 1;
		else {
			index = mid;
			equal = true;
			return 0;
		}
	}

#ifdef BTREE_DEBUG
	std::cout << std::dec << std::setfill(' ');
	std::cout << " => " << resstr[rc + 1] << ", " << mid;
#endif

	index = beg;
	equal = false;
	return 0;
}

int BTreeNode::find_le(const void* key, uint16_t key_len, int& index, bool& equal)
{
	int err = find_ge(key, key_len, index, equal);
	if (err == 0 && equal == false && index > 0)
		index--;
	return err;
}

bool BTreeNode::key_ptr_len(int index, const void *& kptr, uint16_t& klen)
{
	if (index < 0 || static_cast<uint32_t>(index) >= m_btn->btn_nkeys) {
		kptr = nullptr;
		klen = 0;
		return false;
	}

	if (m_btn->btn_flags & BTNODE_FIXED_KV_SIZE) {
		const kvoff_t* e = reinterpret_cast<const kvoff_t*>(m_table);
		kptr = m_keys + e[index].k;
		klen = m_tree.GetKeyLen();
	} else {
		const kvloc_t* e = reinterpret_cast<const kvloc_t*>(m_table);
		kptr = m_keys + e[index].k.off;
		klen = e[index].k.len;
	}

	return true;
}

bool BTreeNode::val_ptr_len(int index, const void *& vptr, uint16_t& vlen)
{
	if (index < 0 || static_cast<uint32_t>(index) >= m_btn->btn_nkeys) {
		vptr = nullptr;
		vlen = 0;
		return false;
	}

	uint16_t vo;
	uint16_t vl;

	if (m_btn->btn_flags & BTNODE_FIXED_KV_SIZE) {
		const kvoff_t* e = reinterpret_cast<const kvoff_t*>(m_table);
		vo = e[index].v;
		if (m_btn->btn_flags & BTNODE_LEAF)
			vl = m_tree.GetValLen();
		else
			vl = 8; // TODO + hash len
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

	return true;
}

bool BTreeNode::child_val(int index, btn_index_node_val_t& binv)
{
	const void *vptr;
	uint16_t vlen;

	if (!val_ptr_len(index, vptr, vlen)) return false;

	memcpy(&binv, vptr, sizeof(oid_t)); // TODO: Hashed nodes
	return true;
}

BTree::BTree(ApfsContainer &container, ApfsVolume *volume) :
	m_container(container)
{
	m_volume = volume;

	m_root_node = nullptr;
	m_omap = nullptr;
	m_xid = 0;
	m_cmp_func = nullptr;
	m_cmp_ctx = nullptr;
}

BTree::~BTree()
{
#ifdef BTREE_USE_MAP
	m_nodes.clear();
#endif
}

bool BTree::Init(oid_t oid_root, xid_t xid, BTCompareFunc cmp_func, const void* cmp_ctx, ApfsNodeMapper *omap)
{
	btn_index_node_val_t binv = {};

	m_omap = omap;
	m_oid = oid_root;
	m_xid = xid;
	m_cmp_func = cmp_func;
	m_cmp_ctx = cmp_ctx;

	if (oid_root == 0) return false;

	binv.binv_child_oid = oid_root;
	m_root_node = GetNode(binv);

	if (m_root_node)
	{
		memcpy(&m_treeinfo, m_root_node->block().data() + m_root_node->block().size() - sizeof(btree_info_t), sizeof(btree_info_t));
		return true;
	}
	else
	{
		std::cerr << "ERROR: BTree Init: Unable to get root node " << oid_root << std::endl;
		return false;
	}
}

int BTree::LookupFirst(void* key, uint16_t& key_len, void* val, uint16_t& val_len)
{
	if (!m_root_node)
		return EINVAL;

	std::shared_ptr<BTreeNode> node(m_root_node);
	btn_index_node_val_t binv = {};
	bool ok;
	const void* kptr;
	const void* vptr;
	uint16_t klen;
	uint16_t vlen;

	while (node->level() > 0) {
		ok = node->child_val(0, binv);
		if (!ok) return ENOENT;
		node = GetNode(binv);
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
	if (!m_root_node)
		return EINVAL;

	int err;
	int index;
	bool equal;
	bool ok;
	btn_index_node_val_t binv = {};
	btn_index_node_val_t binv_nb = {};
	const void* kptr;
	const void* vptr;
	uint16_t klen;
	uint16_t vlen;

	std::shared_ptr<BTreeNode> node(m_root_node);

#ifdef BTREE_DEBUG
	// std::cout << std::hex << "BTree::Lookup: key=" << *reinterpret_cast<const uint64_t *>(key) << " root=" << node->nodeid() << std::endl;
	std::cout << "BTree::Lookup: ";
	DumpHex(std::cout, reinterpret_cast<const uint8_t *>(key), srch_key_len, srch_key_len);
#endif

	while (true) {
		while (node->level() > 0) {
			err = node->find_le(key, srch_key_len, index, equal);
			if (err) return err;

			if (index > 0 && mode == FindMode::LT) {
				ok = node->child_val(index - 1, binv_nb);
				assert(ok);
				if (!ok) return EINVAL;
			}
			else if (static_cast<uint32_t>(index + 1) < node->nkeys() && (mode == FindMode::GE || mode == FindMode::GT)) {
				ok = node->child_val(index + 1, binv_nb);
				assert(ok);
				if (!ok) return EINVAL;
			}

			ok = node->child_val(index, binv);
			assert(ok);
			if (!ok) return ENOENT;

			node = GetNode(binv);
			if (!node) {
				log_error("BTree lookup: node %" PRIx64 " not found.\n", binv.binv_child_oid);
				return EINVAL;
			}
		}

		node->find_ge(key, srch_key_len, index, equal);
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
		node = GetNode(binv_nb);
		binv_nb.binv_child_oid = 0;
	}

	if (node->nkeys() == 0)
		return ENOENT;
	else {
		ok = node->key_ptr_len(index, kptr, klen);
		if (!ok) return EINVAL;
		ok = node->val_ptr_len(index, vptr, vlen);
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
	}

	return 0;
}

void BTree::dump(BlockDumper& out)
{
	if (m_root_node)
		DumpTreeInternal(out, m_root_node);
}

void BTree::DumpTreeInternal(BlockDumper& out, const std::shared_ptr<BTreeNode> &node)
{
	size_t k;
	size_t cnt;
	std::shared_ptr<BTreeNode> child;
	btn_index_node_val_t binv;

	if (!node)
		return;

	out.DumpNode(node->block().data(), node->paddr());

	if (node->level() > 0)
	{
		cnt = node->nkeys();

		for (k = 0; k < cnt; k++)
		{
			node->child_val(k, binv);

			if (m_omap) {
				omap_res_t omr;
				m_omap->Lookup(omr, binv.binv_child_oid, m_xid);
				// out.st() << "omap: " << omr.oid << " " << omr.xid << " => " << omr.flags << " " << omr.size << " " << omr.paddr << std::endl;

				if (omr.flags & OMAP_VAL_DELETED) {
					out.st() << "Omap val " << binv.binv_child_oid << " deleted." << std::endl;
					continue;
				}
			}

			child = GetNode(binv);

			if (child)
				DumpTreeInternal(out, child);
			else
				out.st() << "Child node " << binv.binv_child_oid << " of parent " << node->nodeid() << " not found!" << std::endl;
		}
	}
}

std::shared_ptr<BTreeNode> BTree::GetNode(const btn_index_node_val_t& binv)
{
	std::shared_ptr<BTreeNode> node;
	oid_t oid;
	bool rc;

	// printf("GetNode oid=%" PRIx64 "\n", oid);

	oid = binv.binv_child_oid;
	if (m_root_node->flags() & BTNODE_HASHED)
		oid += m_oid;

#ifdef BTREE_USE_MAP
	m_mutex.lock();
	auto it = m_nodes.find(oid);

	if (it != m_nodes.end())
		node = it->second;

	m_mutex.unlock();

	if (!node)
#endif
	{
		omap_res_t omr;

		omr.oid = oid;
		omr.xid = m_xid;
		omr.flags = 0;
		omr.size = m_treeinfo.bt_fixed.bt_node_size;
		omr.paddr = oid;

		std::vector<uint8_t> blk;

		if (m_omap)
		{
			rc = m_omap->Lookup(omr, oid, m_xid);

			if (g_debug & Dbg_Info) {
				std::cout << "omap: oid=" << omr.oid << " xid=" << omr.xid << " flags=" << omr.flags << " size=" << omr.size << " paddr=" << omr.paddr << std::endl;
			}

			if (!rc)
			{
				std::cerr << "ERROR: GetNode: omap entry oid " << std::hex << oid << " xid " << m_xid << " not found." << std::endl;
				return node;
			}
		}

		blk.resize(m_container.GetBlocksize());

		if (m_volume)
		{
			// TODO: is the crypto_id always equal to the block ID here?
			// I think so, the xts id and the block id only differ when the
			// volume has been converted from a HFS/FileVault volume, which
			// used CoreStorage. After conversions, the block numbers do not
			// match anymore, since the CoreStorage data has been removed
			// and assigned to the apfs volume. But the metadata is always
			// fresh and therefore the ids should match.
			if (!m_volume->ReadBlocks(blk.data(), omr.paddr, 1, (omr.flags & OMAP_VAL_ENCRYPTED) ? omr.paddr : 0))
			{
				std::cerr << "ERROR: GetNode: ReadBlocks failed!" << std::endl;
				return node;
			}

			if (!(omr.flags & OMAP_VAL_NOHEADER)) {
				if (!VerifyBlock(blk.data(), blk.size()))
				{
					std::cerr << "ERROR: GetNode: VerifyBlock failed!" << std::endl;
					if (g_debug & Dbg_Errors)
						DumpHex(std::cerr, blk.data(), blk.size());
					return node;
				}
			} else {
				/*
				std::cout << "BTNode @ " << omr.paddr << ":" << std::endl;
				DumpHex(std::cout, blk.data(), blk.size());
				std::cout << std::endl;
				*/
			}
		}
		else
		{
			if (!m_container.ReadAndVerifyHeaderBlock(blk.data(), omr.paddr))
			{
				std::cerr << "ERROR: GetNode: ReadAndVerifyHeaderBlock failed!" << std::endl;
				return node;
			}
		}

		node = BTreeNode::CreateNode(*this, blk.data(), blk.size(), omr.paddr, m_cmp_func, m_cmp_ctx);
#ifdef BTREE_USE_MAP
		m_mutex.lock();

		if (m_nodes.size() > BTREE_MAP_MAX_NODES)
		{
#if 0
			m_nodes.clear(); // TODO: Make this somewhat more intelligent ...
#else
			// This might be somewhat more intelligent ...
			for (it = m_nodes.begin(); it != m_nodes.end();)
			{
				if (it->second.use_count() == 1)
					it = m_nodes.erase(it);
				else
					++it;
			}
#endif
		}

		m_nodes[oid] = node;

		m_mutex.unlock();
#endif
	}

	return node;
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

int BTreeIterator::init(BTree* tree, void* key_buf, uint16_t key_buf_len, uint16_t key_len, void* val_buf, uint16_t val_buf_len, BTree::FindMode mode)
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

	err = m_tree->LookupFirst(m_key_buf, m_key_len, m_val_buf, m_val_len);

	return err;
}

bool BTreeIterator::next()
{
	uint16_t skey_len = m_key_len;
	int err;

	m_key_len = m_key_buf_len;
	m_val_len = m_val_buf_len;

	err = m_tree->Lookup(m_key_buf, skey_len, m_key_len, m_val_buf, m_val_len, BTree::FindMode::GT);
	return err == 0;
}
