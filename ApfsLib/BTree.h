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
typedef int(*BTCompareFunc)(const void *skey, size_t skey_len, const void *ekey, size_t ekey_len, const void *context);

int CompareU64Key(const void *skey, size_t skey_len, const void *ekey, size_t ekey_len, const void *context);

class BTreeNode
{
public:
	BTreeNode(BTree &tree, const uint8_t *block, size_t blocksize, paddr_t paddr, BTCompareFunc cmp_func, const void* cmp_ctx);

	static std::shared_ptr<BTreeNode> CreateNode(BTree &tree, const uint8_t *block, size_t blocksize, paddr_t paddr, BTCompareFunc cmp_func, const void* cmp_ctx);

	virtual ~BTreeNode();

	uint64_t nodeid() const { return m_btn->btn_o.o_oid; }
	uint32_t nkeys() const { return m_btn->btn_nkeys; }
	uint16_t level() const { return m_btn->btn_level; }
	uint16_t flags() const { return m_btn->btn_flags; }
	paddr_t paddr() const { return m_paddr; }

	int find_ge(const void* key, uint16_t key_len, int& index, bool& equal);
	int find_le(const void* key, uint16_t key_len, int& index, bool& equal);
	bool key_ptr_len(int index, const void*& kptr, uint16_t& klen);
	bool val_ptr_len(int index, const void*& vptr, uint16_t& vlen);
	bool child_val(int index, btn_index_node_val_t& binv);

	const std::vector<uint8_t> &block() const { return m_block; }

protected:
	std::vector<uint8_t> m_block;
	BTree &m_tree;

	const paddr_t m_paddr;

	const btree_node_phys_t *m_btn;
	const void* m_table;
	const uint8_t* m_keys;
	const uint8_t* m_vals;

	BTCompareFunc m_cmp_func;
	const void* m_cmp_ctx;
};

class BTree
{
public:
	enum class FindMode
	{
		EQ,
		LE,
		LT,
		GE,
		GT
	};

	friend class BTreeIterator;

	BTree(ApfsContainer &container, ApfsVolume *vol = nullptr);
	~BTree();

	bool Init(oid_t oid_root, xid_t xid, BTCompareFunc cmp_func, const void* cmp_ctx, ApfsNodeMapper *omap = nullptr);

	int LookupFirst(void* key, uint16_t& key_len, void* val, uint16_t& val_len);
	int Lookup(void* key, uint16_t srch_key_len, uint16_t& key_len, void* val, uint16_t& val_len, FindMode mode);

	uint16_t GetKeyLen() const { return m_treeinfo.bt_fixed.bt_key_size; }
	uint16_t GetValLen() const { return m_treeinfo.bt_fixed.bt_val_size; }
	uint32_t GetBTFlags() const { return m_treeinfo.bt_fixed.bt_flags; }

	void dump(BlockDumper &out);

private:
	void DumpTreeInternal(BlockDumper &out, const std::shared_ptr<BTreeNode> &node);
	uint32_t Find(const std::shared_ptr<BTreeNode> &node, const void *key, size_t key_size, BTCompareFunc func, void *context);
	int FindBin(const std::shared_ptr<BTreeNode> &node, const void *key, size_t key_size, BTCompareFunc func, void *context, FindMode mode);

	std::shared_ptr<BTreeNode> GetNode(const btn_index_node_val_t& binv);

	ApfsContainer &m_container;
	ApfsVolume *m_volume;

	std::shared_ptr<BTreeNode> m_root_node;
	ApfsNodeMapper *m_omap;

	btree_info_t m_treeinfo;

	oid_t m_oid;
	xid_t m_xid;

	BTCompareFunc m_cmp_func;
	const void* m_cmp_ctx;

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

	int init(BTree* tree, void* key_buf, uint16_t key_buf_len, uint16_t key_len, void* val_buf, uint16_t val_buf_len, BTree::FindMode mode);
	int initFirst(BTree* tree, void* key_buf, uint16_t key_buf_len, void* val_buf, uint16_t val_buf_len);
	bool next();

	uint16_t key_len() const { return m_key_len; }
	uint16_t val_len() const { return m_val_len; }

private:
	BTree *m_tree;

	void *m_key_buf;
	void *m_val_buf;
	uint16_t m_key_buf_len;
	uint16_t m_val_buf_len;
	uint16_t m_key_len;
	uint16_t m_val_len;
};
