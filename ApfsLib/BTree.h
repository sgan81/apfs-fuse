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

#include <vector>
#include <map>
#include <memory>
#include <mutex>

#include "Global.h"
#include "DiskStruct.h"

#include "ApfsNodeMapper.h"

class BTree;
class BTreeNode;
class BTreeIterator;
class BlockDumper;

class ApfsContainer;
class ApfsVolume;

// This enables a rudimentary disk cache ...
#define BTREE_USE_MAP
// TODO: Think about a better solution.
// 8192 will take max. 32 MB of RAM. Higher may be faster, but use more RAM.
#define BTREE_MAP_MAX_NODES 8192

// ekey < skey: -1, ekey > skey: 1, ekey == skey: 0
typedef int(*BTCompareFunc)(const void *skey, size_t skey_len, const void *ekey, size_t ekey_len, void *context);

int CompareStdKey(const void *skey, size_t skey_len, const void *ekey, size_t ekey_len, void *context);

class BTreeEntry
{
	friend class BTree;
public:
	BTreeEntry();
	~BTreeEntry();

	BTreeEntry(const BTreeEntry &o) = delete;
	BTreeEntry &operator=(const BTreeEntry &o) = delete;

	void clear();

	const void *key;
	const void *val;
	size_t key_len;
	size_t val_len;

private:
	std::shared_ptr<BTreeNode> m_node;
};

class BTreeNode
{
protected:
	BTreeNode(BTree &tree, const uint8_t *block, size_t blocksize, uint64_t nid_parent, uint64_t bid);

public:
	static std::shared_ptr<BTreeNode> CreateNode(BTree &tree, const uint8_t *block, size_t blocksize, uint64_t nid_parent, uint64_t bid);

	virtual ~BTreeNode();

	uint64_t nodeid() const { return m_hdr->nid; }
	uint32_t entries_cnt() const { return m_bt->entries_cnt; }
	uint16_t level() const { return m_bt->level; }
	uint64_t blockid() const { return m_bid; }

	virtual bool GetEntry(BTreeEntry &result, uint32_t index) const = 0;
	// virtual uint32_t Find(const void *key, size_t key_size, BTCompareFunc func) const = 0;

	const std::vector<byte_t> &block() const { return m_block; }

protected:
	std::vector<uint8_t> m_block;
	BTree &m_tree;

	uint16_t m_keys_start; // Up
	uint16_t m_vals_start; // Dn

	const uint64_t m_nid_parent;
	const uint64_t m_bid;

	const APFS_BlockHeader *m_hdr;
	const APFS_BTHeader *m_bt;
};

class BTreeNodeFix : public BTreeNode
{
public:
	BTreeNodeFix(BTree &tree, const uint8_t *block, size_t blocksize, uint64_t nid_parent, uint64_t bid);

	bool GetEntry(BTreeEntry &result, uint32_t index) const override;
	// uint32_t Find(const void *key, size_t key_size, BTCompareFunc func) const override;

private:
	const APFS_BTEntryFixed *m_entries;
};

class BTreeNodeVar : public BTreeNode
{
public:
	BTreeNodeVar(BTree &tree, const uint8_t *block, size_t blocksize, uint64_t nid_parent, uint64_t bid);

	bool GetEntry(BTreeEntry &result, uint32_t index) const override;
	// uint32_t Find(const void *key, size_t key_size, BTCompareFunc func) const override;

private:
	const APFS_BTEntry *m_entries;
};

class BTree
{
	enum class FindMode
	{
		EQ,
		LE,
		LT,
		GE,
		GT
	};

	friend class BTreeIterator;
public:
	BTree(ApfsContainer &container, ApfsVolume *vol = nullptr);
	~BTree();

	bool Init(uint64_t nid_root, uint64_t xid, ApfsNodeMapper *node_map = nullptr);

	bool Lookup(BTreeEntry &result, const void *key, size_t key_size, BTCompareFunc func, void *context, bool exact);
	bool GetIterator(BTreeIterator &it, const void *key, size_t key_size, BTCompareFunc func, void *context);

	uint16_t GetKeyLen() const { return m_treeinfo.min_key_size; }
	uint16_t GetValLen() const { return m_treeinfo.min_val_size; }

	void dump(BlockDumper &out);

	void EnableDebugOutput() { m_debug = true; }

private:
	void DumpTreeInternal(BlockDumper &out, const std::shared_ptr<BTreeNode> &node);
	uint32_t Find(const std::shared_ptr<BTreeNode> &node, const void *key, size_t key_size, BTCompareFunc func, void *context);
	int FindBin(const std::shared_ptr<BTreeNode> &node, const void *key, size_t key_size, BTCompareFunc func, void *context, FindMode mode);

	std::shared_ptr<BTreeNode> GetNode(uint64_t nid, uint64_t nid_parent);

	ApfsContainer &m_container;
	ApfsVolume *m_volume;

	std::shared_ptr<BTreeNode> m_root_node;
	ApfsNodeMapper *m_nodeid_map;

	APFS_BTFooter m_treeinfo;

	uint64_t m_xid;
	bool m_debug;

#ifdef BTREE_USE_MAP
	std::map<uint64_t, std::shared_ptr<BTreeNode>> m_nodes;
	std::mutex m_mutex;
#endif
};

class BTreeIterator
{
public:
	BTreeIterator();
	~BTreeIterator();

	bool next();
	bool prev();

	bool GetEntry(BTreeEntry &res) const;

	void Init(BTree *tree, uint16_t max_level);
	void Set(uint16_t level, const std::shared_ptr<BTreeNode> &node, uint32_t index);

private:
	std::shared_ptr<BTreeNode> next_internal(uint16_t level);
	std::shared_ptr<BTreeNode> prev_internal(uint16_t level);

	std::shared_ptr<BTreeNode> m_bt_node[8];
	uint32_t m_bt_index[8];

	BTree *m_tree;
	uint16_t m_max_level;
};
