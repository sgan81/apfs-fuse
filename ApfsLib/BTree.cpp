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
#include <iomanip>

#include "ApfsContainer.h"
#include "ApfsVolume.h"
#include "BTree.h"
#include "Util.h"
#include "BlockDumper.h"

int CompareStdKey(const void *skey, size_t skey_len, const void *ekey, size_t ekey_len, void *context)
{
	// assert(skey_len == 8);
	// assert(ekey_len == 8);

	(void)skey_len;
	(void)ekey_len;
	(void)context;

	uint64_t ks = *reinterpret_cast<const uint64_t *>(skey);
	uint64_t ke = *reinterpret_cast<const uint64_t *>(ekey);

	if (ke < ks)
		return -1;
	if (ke > ks)
		return 1;
	return 0;
}

BTreeEntry::BTreeEntry()
{
	key = nullptr;
	val = nullptr;
	key_len = 0;
	val_len = 0;
}

BTreeEntry::~BTreeEntry()
{
}

void BTreeEntry::clear()
{
	key = nullptr;
	val = nullptr;
	key_len = 0;
	val_len = 0;

	m_node.reset();
}

BTreeNode::BTreeNode(BTree &tree, const uint8_t *block, size_t blocksize, paddr_t paddr, const std::shared_ptr<BTreeNode> &parent, uint32_t parent_index) :
	m_tree(tree),
	m_parent_index(parent_index),
	m_parent(parent),
	m_paddr(paddr)
{
	m_block.assign(block, block + blocksize);
	m_btn = reinterpret_cast<const btree_node_phys_t *>(m_block.data());

	assert(m_btn->btn_table_space.off == 0);

	m_keys_start = sizeof(btree_node_phys_t) + m_btn->btn_table_space.len;
	if (parent)
		m_vals_start = blocksize;
	else
		m_vals_start = blocksize - sizeof(btree_info_t);
}

std::shared_ptr<BTreeNode> BTreeNode::CreateNode(BTree & tree, const uint8_t * block, size_t blocksize, paddr_t paddr, const std::shared_ptr<BTreeNode> &parent, uint32_t parent_index)
{
	const btree_node_phys_t *btn = reinterpret_cast<const btree_node_phys_t *>(block);

	if (btn->btn_flags & BTNODE_FIXED_KV_SIZE)
		return std::make_shared<BTreeNodeFix>(tree, block, blocksize, paddr, parent, parent_index);
	else
		return std::make_shared<BTreeNodeVar>(tree, block, blocksize, paddr, parent, parent_index);
}

BTreeNode::~BTreeNode()
{
}

BTreeNodeFix::BTreeNodeFix(BTree &tree, const uint8_t *block, size_t blocksize, paddr_t paddr, const std::shared_ptr<BTreeNode> &parent, uint32_t parent_index) :
	BTreeNode(tree, block, blocksize, paddr, parent, parent_index)
{
	m_entries = reinterpret_cast<const kvoff_t *>(m_block.data() + sizeof(btree_node_phys_t));
}

bool BTreeNodeFix::GetEntry(BTreeEntry & result, uint32_t index) const
{
	result.clear();

	if (index >= m_btn->btn_nkeys)
		return false;

	result.key = m_block.data() + m_keys_start + m_entries[index].k;
	result.key_len = m_tree.GetKeyLen();

	if (m_entries[index].v != BTOFF_INVALID)
	{
		result.val = m_block.data() + m_vals_start - m_entries[index].v;
		result.val_len = (m_btn->btn_flags & BTNODE_LEAF) ? m_tree.GetValLen() : sizeof(oid_t);
	}
	else
	{
		result.val = nullptr;
		result.val_len = 0;
	}

	return true;
}

BTreeNodeVar::BTreeNodeVar(BTree &tree, const uint8_t *block, size_t blocksize, paddr_t paddr, const std::shared_ptr<BTreeNode> &parent, uint32_t parent_index) :
	BTreeNode(tree, block, blocksize, paddr, parent, parent_index)
{
	m_entries = reinterpret_cast<const kvloc_t *>(m_block.data() + sizeof(btree_node_phys_t));
}

bool BTreeNodeVar::GetEntry(BTreeEntry & result, uint32_t index) const
{
	result.clear();

	if (index >= m_btn->btn_nkeys)
		return false;

	result.key = m_block.data() + m_keys_start + m_entries[index].k.off;
	result.key_len = m_entries[index].k.len;

	if (m_entries[index].v.off != BTOFF_INVALID)
	{
		result.val = m_block.data() + m_vals_start - m_entries[index].v.off;
		result.val_len = m_entries[index].v.len;
	}
	else
	{
		result.val = nullptr;
		result.val_len = 0;
	}

	return true;
}

BTree::BTree(ApfsContainer &container, ApfsVolume *volume) :
	m_container(container)
{
	m_volume = volume;

	m_root_node = nullptr;
	m_omap = nullptr;
	m_xid = 0;
	m_debug = false;
}

BTree::~BTree()
{
#ifdef BTREE_USE_MAP
	m_nodes.clear();
#endif
}

bool BTree::Init(oid_t oid_root, xid_t xid, ApfsNodeMapper *omap)
{
	std::shared_ptr<BTreeNode> dummy;

	m_omap = omap;
	m_xid = xid;

	m_root_node = GetNode(oid_root, dummy, 0);

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

bool BTree::Lookup(BTreeEntry &result, const void *key, size_t key_size, BTCompareFunc func, void *context, bool exact)
{
	if (!m_root_node)
		return false;

	oid_t oid;
	oid_t oid_parent;
	int index;

	std::shared_ptr<BTreeNode> node(m_root_node);
	BTreeEntry e;

	if (m_debug)
	{
		// std::cout << std::hex << "BTree::Lookup: key=" << *reinterpret_cast<const uint64_t *>(key) << " root=" << node->nodeid() << std::endl;
		std::cout << "BTree::Lookup: ";
		DumpHex(std::cout, reinterpret_cast<const uint8_t *>(key), key_size, key_size);
	}

	while (node->level() > 0)
	{
		index = FindBin(node, key, key_size, func, context, FindMode::LE);

		if (index < 0)
			return false;

		node->GetEntry(e, index);

		assert(e.val_len == sizeof(oid_t));

		oid = *reinterpret_cast<const oid_t *>(e.val);
		oid_parent = node->nodeid();

		node = GetNode(oid, node, index);

		if (!node)
		{
			std::cerr << "BTree::Lookup: Node " << oid << " with parent " << oid_parent << " not found." << std::endl;
			return false;
		}
	}

	index = FindBin(node, key, key_size, func, context, exact ? FindMode::EQ : FindMode::LE);

	if (m_debug)
		std::cout << "Result = " << node->nodeid() << ":" << index << std::endl;

	if (index < 0)
		return false;

	node->GetEntry(result, index);
	result.m_node = node;

	return true;
}

bool BTree::GetIterator(BTreeIterator& it, const void* key, size_t key_size, BTCompareFunc func, void *context)
{
	oid_t oid;
	int index;

	std::shared_ptr<BTreeNode> node(m_root_node);
	BTreeEntry e;

	if (m_debug)
		std::cout << std::hex << "BTree::GetIterator: key=" << *reinterpret_cast<const uint64_t *>(key) << " root=" << node->nodeid() << std::endl;

	while (node->level() > 0)
	{
		index = FindBin(node, key, key_size, func, context, FindMode::LE);

		if (index < 0)
			index = 0;

		node->GetEntry(e, index);

		assert(e.val_len == sizeof(oid_t));

		oid = *reinterpret_cast<const oid_t *>(e.val);

		node = GetNode(oid, node, index);
	}

	index = FindBin(node, key, key_size, func, context, FindMode::GE);

	if (m_debug)
		std::cout << "Result = " << node->nodeid() << ":" << index << std::endl;

	if (index < 0)
	{
		index = node->entries_cnt() - 1;
		it.Setup(this, node, index);
		it.next();
		if (m_debug)
			std::cout << "Iterator next entry" << std::endl;
	}
	else
	{
		it.Setup(this, node, index);
	}

#if 0
	it.GetEntry(e);

	if (func(key, key_size, e.key, e.key_len) < 0)
		it.next();
#endif

	return true;
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
	BTreeEntry e;
	std::shared_ptr<BTreeNode> child;
	oid_t oid_child;
	oid_t oid_parent;

	if (!node)
		return;

	out.DumpNode(node->block().data(), node->paddr());

	if (node->level() > 0)
	{
		cnt = node->entries_cnt();

		for (k = 0; k < cnt; k++)
		{
			node->GetEntry(e, k);

			if (e.val_len != sizeof(uint64_t))
				continue; // assert ...

			oid_parent = node->nodeid();
			oid_child = *reinterpret_cast<const uint64_t *>(e.val);

			child = GetNode(oid_child, node, k);

			if (child)
				DumpTreeInternal(out, child);
			else
				out.st() << "Child node " << oid_child << " of parent " << oid_parent << " not found!" << std::endl;
		}
	}
}

std::shared_ptr<BTreeNode> BTree::GetNode(oid_t oid, const std::shared_ptr<BTreeNode> &parent, uint32_t parent_index)
{
	std::shared_ptr<BTreeNode> node;

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

		omr.flags = 0;
		omr.size = m_treeinfo.bt_fixed.bt_node_size;
		omr.paddr = oid;

		std::vector<uint8_t> blk;

		if (m_omap)
		{
			if (!m_omap->Lookup(omr, oid, m_xid))
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

			if (!VerifyBlock(blk.data(), blk.size()))
			{
				std::cerr << "ERROR: GetNode: VerifyBlock failed!" << std::endl;
				if (g_debug & Dbg_Errors)
					DumpHex(std::cerr, blk.data(), blk.size());
				return node;
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

		node = BTreeNode::CreateNode(*this, blk.data(), blk.size(), omr.paddr, parent, parent_index);
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

uint32_t BTree::Find(const std::shared_ptr<BTreeNode> &node, const void *key, size_t key_size, BTCompareFunc func, void *context)
{
	uint32_t k;
	uint32_t cnt;
	int res_cur = 0;
	int res_prev = 0;
	BTreeEntry e;

	cnt = node->entries_cnt();

	if (cnt == 0)
		return 0;

	// Erst mal linear suchen ...
	for (k = 0; k < cnt; k++)
	{
		node->GetEntry(e, k);

		res_cur = func(key, key_size, e.key, e.key_len, context);

		if (res_cur > 0 && res_prev <= 0)
			break;

		res_prev = res_cur;
	}

	return k;
}

int BTree::FindBin(const std::shared_ptr<BTreeNode>& node, const void* key, size_t key_size, BTCompareFunc func, void *context, FindMode mode)
{
	static const char resstr[3] = { '<', '=', '>' };

	int beg;
	int end;
	int mid = -1;
	int cnt = node->entries_cnt();
	int res;

	BTreeEntry e;
	int rc;

	if (cnt <= 0)
		return -1;

	beg = 0;
	end = cnt - 1;

	if (m_debug)
	{
		std::cout << "FindBin    : ";
		DumpHex(std::cout, reinterpret_cast<const uint8_t *>(key), key_size, key_size);
	}

	while (beg <= end)
	{
		mid = (beg + end) / 2;

		node->GetEntry(e, mid);
		rc = func(key, key_size, e.key, e.key_len, context);

		if (m_debug)
		{
			std::cout << std::dec << std::setfill(' ');
			std::cout << std::setw(2) << beg << " [" << std::setw(2) << mid << "] " << std::setw(2) << end << " : " << resstr[rc + 1] << " : ";
			DumpHex(std::cout, reinterpret_cast<const uint8_t *>(e.key), e.key_len, e.key_len);
		}

		if (rc == 0)
			break;

		if (rc == -1)
			beg = mid + 1;
		else if (rc == 1)
			end = mid - 1;
	}

	if (m_debug)
	{
		std::cout << std::dec << std::setfill(' ');
		std::cout << " => " << resstr[rc + 1] << ", " << mid;
	}

	switch (mode)
	{
	case FindMode::EQ:
		res = (rc == 0) ? mid : -1;
		break;
	case FindMode::LE:
		res = (rc <= 0) ? mid : (mid - 1);
		break;
	case FindMode::LT:
		res = (rc < 0) ? mid : (mid - 1);
		break;
	case FindMode::GE:
		res = (rc >= 0) ? mid : (mid + 1);
		break;
	case FindMode::GT:
		res = (rc > 0) ? mid : (mid + 1);
		break;
	default:
		assert(false);
		res = -1;
		break;
	}

	if (res == cnt)
		res = -1;

	if (m_debug)
		std::cout << " => " << res << std::endl;

	return res;
}

BTreeIterator::BTreeIterator()
{
	m_tree = nullptr;
	m_index = 0;
}

BTreeIterator::BTreeIterator(BTree *tree, const std::shared_ptr<BTreeNode> &node, uint32_t index)
{
	m_tree = tree;
	m_node = node;
	m_index = index;
}

BTreeIterator::~BTreeIterator()
{
}

void BTreeIterator::Setup(BTree* tree, const std::shared_ptr<BTreeNode>& node, uint32_t index)
{
	m_tree = tree;
	m_node = node;
	m_index = index;
}


bool BTreeIterator::next()
{
	if (!m_node)
		return false;

	m_index++;

	if (m_index < m_node->entries_cnt())
		return true;
	else {
		std::shared_ptr<BTreeNode> node = next_node();
		if (node) {
			m_index = 0;
			m_node = node;
			return true;
		}
	}
	return false;
}

bool BTreeIterator::GetEntry(BTreeEntry& res) const
{
	if (!m_node)
		return false;

	return m_node->GetEntry(res, m_index);
}

#undef BTITDBG

std::shared_ptr<BTreeNode> BTreeIterator::next_node()
{
	std::shared_ptr<BTreeNode> node;
	uint32_t pidx;
	BTreeEntry e;
	oid_t oid;

	node = m_node;

#ifdef BTITDBG
	std::cout << "======== ******** BTreeIterator::next_node() ******** ========" << std::endl;
	std::cout << "  Current node: " << node->nodeid() << std::endl;
#endif

	do {
		pidx = node->parent_index();
		node = node->parent();
#ifdef BTITDBG
		std::cout << "  Navigating up to node " << node->nodeid() << " index " << pidx << std::endl;
#endif
		pidx++;
	} while (node && pidx >= node->entries_cnt());

	if (!node)
		return std::shared_ptr<BTreeNode>();

	while (node->level() > 0) {
		node->GetEntry(e, pidx);
		assert(e.val_len == sizeof(oid_t));
		if (e.val_len != sizeof(oid_t))
			std::cerr << "BTreeIterator next_node val_len != sizeof(oid_t)" << std::endl;
		oid = *reinterpret_cast<const le<oid_t>*>(e.val);
#ifdef BTITDBG
		std::cout << "  Navigating down to node " << oid << std::endl;
#endif
		node = m_tree->GetNode(oid, node, pidx);

		if (!node)
			std::cerr << "Failed to load btree node oid " << oid << std::endl;

		pidx = 0;
	}

	return node;
}
