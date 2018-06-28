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

BTreeNode::BTreeNode(BTree &tree, const uint8_t *block, size_t blocksize, uint64_t nid_parent, uint64_t bid) :
	m_tree(tree),
	m_nid_parent(nid_parent),
	m_bid(bid)
{
	m_block.assign(block, block + blocksize);
	m_hdr = reinterpret_cast<const APFS_BlockHeader *>(m_block.data());
	m_bt = reinterpret_cast<const APFS_BTHeader *>(m_block.data() + sizeof(APFS_BlockHeader));

	assert(m_bt->keys_offs == 0);

	m_keys_start = 0x38 + m_bt->keys_len;
	m_vals_start = (nid_parent != 0) ? blocksize : blocksize - sizeof(APFS_BTFooter);
}

std::shared_ptr<BTreeNode> BTreeNode::CreateNode(BTree & tree, const uint8_t * block, size_t blocksize, uint64_t nid_parent, uint64_t bid)
{
	const APFS_BTHeader *bt = reinterpret_cast<const APFS_BTHeader *>(block + sizeof(APFS_BlockHeader));

	if (bt->flags & 4)
		return std::make_shared<BTreeNodeFix>(tree, block, blocksize, nid_parent, bid);
	else
		return std::make_shared<BTreeNodeVar>(tree, block, blocksize, nid_parent, bid);
}

BTreeNode::~BTreeNode()
{
}

BTreeNodeFix::BTreeNodeFix(BTree &tree, const uint8_t *block, size_t blocksize, uint64_t nid_parent, uint64_t bid) :
	BTreeNode(tree, block, blocksize, nid_parent, bid)
{
	m_entries = reinterpret_cast<const APFS_BTEntryFixed *>(m_block.data() + 0x38);
}

bool BTreeNodeFix::GetEntry(BTreeEntry & result, uint32_t index) const
{
	result.clear();

	if (index >= m_bt->entries_cnt)
		return false;

	result.key = m_block.data() + m_keys_start + m_entries[index].key_offs;
	result.key_len = m_tree.GetKeyLen();

	if (m_entries[index].value_offs != 0xFFFF)
	{
		result.val = m_block.data() + m_vals_start - m_entries[index].value_offs;
		result.val_len = (m_bt->level > 0) ? sizeof(uint64_t) : m_tree.GetValLen();
	}
	else
	{
		result.val = nullptr;
		result.val_len = 0;
	}

	return true;
}

BTreeNodeVar::BTreeNodeVar(BTree &tree, const uint8_t *block, size_t blocksize, uint64_t nid_parent, uint64_t bid) :
	BTreeNode(tree, block, blocksize, nid_parent, bid)
{
	m_entries = reinterpret_cast<const APFS_BTEntry *>(m_block.data() + 0x38);
}

bool BTreeNodeVar::GetEntry(BTreeEntry & result, uint32_t index) const
{
	result.clear();

	if (index >= m_bt->entries_cnt)
		return false;

	result.key = m_block.data() + m_keys_start + m_entries[index].key_offs;
	result.key_len = m_entries[index].key_len;

	if (m_entries[index].value_offs != 0xFFFF)
	{
		result.val = m_block.data() + m_vals_start - m_entries[index].value_offs;
		result.val_len = m_entries[index].value_len;
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
	m_nodeid_map = nullptr;
	m_xid = 0;
	m_debug = false;
}

BTree::~BTree()
{
#ifdef BTREE_USE_MAP
	m_nodes.clear();
#endif
}

bool BTree::Init(uint64_t nid_root, uint64_t xid, ApfsNodeMapper *node_map)
{
	m_nodeid_map = node_map;
	m_xid = xid;

	m_root_node = GetNode(nid_root, 0);

	if (m_root_node)
	{
		memcpy(&m_treeinfo, m_root_node->block().data() + m_root_node->block().size() - sizeof(APFS_BTFooter), sizeof(APFS_BTFooter));
		return true;
	}
	else
	{
		std::cerr << "ERROR: BTree Init: Unable to get root node " << nid_root << std::endl;
		return false;
	}
}

bool BTree::Lookup(BTreeEntry &result, const void *key, size_t key_size, BTCompareFunc func, void *context, bool exact)
{
	if (!m_root_node)
		return false;

	uint64_t nodeid;
	uint64_t parentid;
	int index;

	std::shared_ptr<BTreeNode> node(m_root_node);
	BTreeEntry e;

	if (m_debug)
	{
		// std::cout << std::hex << "BTree::Lookup: key=" << *reinterpret_cast<const uint64_t *>(key) << " root=" << node->nodeid() << std::endl;
		std::cout << "BTree::Lookup: ";
		DumpHex(std::cout, reinterpret_cast<const byte_t *>(key), key_size, key_size);
	}

	while (node->level() > 0)
	{
		index = FindBin(node, key, key_size, func, context, FindMode::LE);

		if (index < 0)
			return false;

		node->GetEntry(e, index);

		assert(e.val_len == sizeof(uint64_t));

		nodeid = *reinterpret_cast<const uint64_t *>(e.val);

		parentid = node->nodeid();
		node = GetNode(nodeid, parentid);

		if (!node)
		{
			std::cerr << "BTree::Lookup: Node " << nodeid << " with parent " << parentid << " not found." << std::endl;
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
	uint64_t nodeid;
	int index;

	std::shared_ptr<BTreeNode> node(m_root_node);
	BTreeEntry e;

	it.Init(this, m_root_node->level());

	if (m_debug)
		std::cout << std::hex << "BTree::GetIterator: key=" << *reinterpret_cast<const uint64_t *>(key) << " root=" << node->nodeid() << std::endl;

	while (node->level() > 0)
	{
		index = FindBin(node, key, key_size, func, context, FindMode::LE);

		if (index < 0)
			index = 0;

		node->GetEntry(e, index);

		assert(e.val_len == sizeof(uint64_t));

		nodeid = *reinterpret_cast<const uint64_t *>(e.val);

		it.Set(node->level(), node, index);

		node = GetNode(nodeid, node->nodeid());
	}

	index = FindBin(node, key, key_size, func, context, FindMode::GE);

	if (m_debug)
		std::cout << "Result = " << node->nodeid() << ":" << index << std::endl;

	if (index < 0)
	{
		index = node->entries_cnt() - 1;
		it.Set(node->level(), node, index);
		it.next();
		if (m_debug)
			std::cout << "Iterator next entry" << std::endl;
	}
	else
	{
		it.Set(node->level(), node, index);
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
	uint64_t nodeid_child;
	uint64_t nodeid_parent;

	if (!node)
		return;

	out.DumpNode(node->block().data(), node->blockid());

	if (node->level() > 0)
	{
		cnt = node->entries_cnt();

		for (k = 0; k < cnt; k++)
		{
			node->GetEntry(e, k);

			if (e.val_len != sizeof(uint64_t))
				continue; // assert ...

			nodeid_child = *reinterpret_cast<const uint64_t *>(e.val);

			nodeid_parent = node->nodeid();
			child = GetNode(nodeid_child, nodeid_parent);

			if (child)
				DumpTreeInternal(out, child);
			else
				out.st() << "Child node " << nodeid_child << " of parent " << nodeid_parent << " not found!" << std::endl;
		}
	}
}

std::shared_ptr<BTreeNode> BTree::GetNode(uint64_t nid, uint64_t nid_parent)
{
	std::shared_ptr<BTreeNode> node;

#ifdef BTREE_USE_MAP
	m_mutex.lock();
	std::map<uint64_t, std::shared_ptr<BTreeNode>>::iterator it = m_nodes.find(nid);

	if (it != m_nodes.end())
		node = it->second;

	m_mutex.unlock();

	if (!node)
#endif
	{
		node_info_t ni;

		ni.flags = 0;
		ni.size = 0x1000;
		ni.bid = nid;

		std::vector<byte_t> blk;

		if (m_nodeid_map)
		{
			if (!m_nodeid_map->GetBlockID(ni, nid, m_xid))
			{
				std::cerr << "ERROR: GetNode: Node mapper: nid " << std::hex << nid << " xid " << m_xid << " not found." << std::endl;
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
			if (!m_volume->ReadBlocks(blk.data(), ni.bid, 1, (ni.flags & 4) != 0, ni.bid))
			{
				std::cerr << "ERROR: GetNode: ReadBlocks failed!" << std::endl;
				return node;
			}

			if (!VerifyBlock(blk.data(), blk.size()))
			{
				std::cerr << "ERROR: GetNode: VerifyBlock failed!" << std::endl;
				return node;
			}
		}
		else
		{
			if (!m_container.ReadAndVerifyHeaderBlock(blk.data(), ni.bid))
			{
				std::cerr << "ERROR: GetNode: ReadAndVerifyHeaderBlock failed!" << std::endl;
				return node;
			}
		}

		node = BTreeNode::CreateNode(*this, blk.data(), blk.size(), nid_parent, ni.bid);
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

		m_nodes[nid] = node;

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
		DumpHex(std::cout, reinterpret_cast<const byte_t *>(key), key_size, key_size);
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
			DumpHex(std::cout, reinterpret_cast<const byte_t *>(e.key), e.key_len, e.key_len);
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
	m_max_level = 0;
	std::fill(std::begin(m_bt_index), std::end(m_bt_index), 0);
}

BTreeIterator::~BTreeIterator()
{
}

bool BTreeIterator::next()
{
	m_bt_index[0]++;

	if (m_bt_index[0] < m_bt_node[0]->entries_cnt())
	{
		return true;
	}
	else
	{
		std::shared_ptr<BTreeNode> node = next_internal(1);
		if (node)
		{
			m_bt_index[0] = 0;
			m_bt_node[0] = node;
			return true;
		}
	}
	return false;
}

bool BTreeIterator::prev()
{
	// Not implemented

	return false;
}

bool BTreeIterator::GetEntry(BTreeEntry& res) const
{
	if (m_bt_node[0] == nullptr)
		return false;

	return m_bt_node[0]->GetEntry(res, m_bt_index[0]);
}

void BTreeIterator::Init(BTree* tree, uint16_t max_level)
{
	m_tree = tree;
	m_max_level = max_level;
}

void BTreeIterator::Set(uint16_t level, const std::shared_ptr<BTreeNode> &node, uint32_t index)
{
	m_bt_node[level] = node;
	m_bt_index[level] = index;
}

std::shared_ptr<BTreeNode> BTreeIterator::next_internal(uint16_t level)
{
	if (level > m_max_level)
		return nullptr;

	std::shared_ptr<BTreeNode> node;
	BTreeEntry bte;
	uint64_t nodeid;
	uint64_t parentid;
	bool rc;

	m_bt_index[level]++;

	if (m_bt_index[level] >= m_bt_node[level]->entries_cnt())
	{
		node = next_internal(level + 1);
		if (node)
		{
			m_bt_index[level] = 0;
			m_bt_node[level] = node;
		}
		else
			m_bt_index[level]--;
	}

	rc = m_bt_node[level]->GetEntry(bte, m_bt_index[level]);

	if (!rc)
	{
		std::cerr << "BTreeIterator next_internal getting entry failed. Node-ID " << m_bt_node[level]->nodeid() << ", Entry " << m_bt_index[level] << ", Level " << level << std::endl;
		return std::shared_ptr<BTreeNode>();
	}

	assert(bte.val_len == sizeof(uint64_t));

	nodeid = *reinterpret_cast<const uint64_t *>(bte.val);
	parentid = (level <= m_max_level) ? m_bt_node[level]->nodeid() : 0;

	return m_tree->GetNode(nodeid, parentid);
}

std::shared_ptr<BTreeNode> BTreeIterator::prev_internal(uint16_t level)
{
	// Not implemented

	(void) level;

	return std::shared_ptr<BTreeNode>();
}
