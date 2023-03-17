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

#include "Global.h"
#include "DiskStruct.h"
#include "Object.h"
#include "ObjPtr.h"

class BTree;
class BTreeNode;
class BTreeIterator;
class BlockDumper;

class Container;
class Volume;

// ekey < skey: -1, ekey > skey: 1, ekey == skey: 0
typedef int(*BTCompareFunc)(const void *skey, size_t skey_len, const void *ekey, size_t ekey_len, uint64_t context, int& res);

int CompareU64Key(const void *skey, size_t skey_len, const void *ekey, size_t ekey_len, uint64_t context, int& res);

struct BTreeParams
{
	btree_info_fixed_t info;
	BTCompareFunc cmp_func;
	uint64_t cmp_ctx;
};

class BTreeNode : public Object
{
public:
	BTreeNode();
	virtual ~BTreeNode();

	int init(const void* params) override;

	uint32_t nkeys() const { return m_btn->btn_nkeys; }
	uint16_t level() const { return m_btn->btn_level; }
	uint16_t flags() const { return m_btn->btn_flags; }

	int find_ge(const void* key, uint16_t key_len, int& index, bool& equal) const;
	int find_le(const void* key, uint16_t key_len, int& index, bool& equal) const;
	int key_ptr_len(int index, const void*& kptr, uint16_t& klen) const;
	int val_ptr_len(int index, const void*& vptr, uint16_t& vlen) const;
	int child_val(int index, btn_index_node_val_t& binv) const;

protected:
	const btree_node_phys_t *m_btn;
	const void* m_table;
	const uint8_t* m_keys;
	const uint8_t* m_vals;

	btree_info_fixed_t m_info;
	BTCompareFunc m_cmp_func;
	uint64_t m_cmp_ctx;
};

class BTree
{
public:
	enum class FindMode { EQ, LE, LT, GE, GT };
	friend class BTreeIterator;

	BTree();
	~BTree();

	bool isValid() const { return static_cast<bool>(m_root); }
	int Init(Object* owner, oid_t oid, xid_t xid, uint32_t type, uint32_t subtype, BTCompareFunc cmp_func, uint64_t cmp_ctx);

	int LookupFirst(void* key, uint16_t& key_len, void* val, uint16_t& val_len);
	int Lookup(void* key, uint16_t srch_key_len, uint16_t& key_len, void* val, uint16_t& val_len, FindMode mode);

	uint16_t GetKeyLen() const { return m_treeinfo.bt_fixed.bt_key_size; }
	uint16_t GetValLen() const { return m_treeinfo.bt_fixed.bt_val_size; }
	uint32_t GetBTFlags() const { return m_treeinfo.bt_fixed.bt_flags; }
	uint32_t key_count() const { return m_treeinfo.bt_key_count; }

	uint32_t tree_type() const { return m_root ? m_root->subtype() : 0; }

	void dump(BlockDumper &out);

private:
	void DumpTreeInternal(BlockDumper &out, const ObjPtr<BTreeNode> &node);
	int GetNode(ObjPtr<BTreeNode>& node, const btn_index_node_val_t& binv);

	Container* m_nx;
	Volume* m_fs;

	ObjPtr<BTreeNode> m_root;
	btree_info_t m_treeinfo;
	xid_t m_snap_xid;

	BTreeParams m_params;
};

class BTreeIterator
{
public:
	BTreeIterator();
	~BTreeIterator();

	int init(BTree* tree, void* key_buf, uint16_t key_len, uint16_t key_buf_len, void* val_buf, uint16_t val_buf_len, BTree::FindMode mode);
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
