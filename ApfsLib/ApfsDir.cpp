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

#include <iostream>
#include <iomanip>
#include <algorithm>

#include <cassert>
#include <cstring>

#include "ApfsDir.h"
#include "ApfsVolume.h"
#include "ApfsContainer.h"
#include "BTree.h"
#include "Util.h"

// static uint32_t g_txt_fmt; // Hack

#ifndef _MSC_VER
template<size_t L>
void strcpy_s(char (&dst)[L], const char *src)
{
	strncpy(dst, src, L);
}
#endif

ApfsDir::Inode::Inode()
{
	id = 0;
	memset(&ino, 0, sizeof(ino));
	memset(&sizes, 0, sizeof(sizes));
	unk_param = 0;
}

ApfsDir::Inode::Inode(const ApfsDir::Inode& o)
{
	id = o.id;
	ino = o.ino;
	name = o.name;
	sizes = o.sizes;
	unk_param = o.unk_param;
}

ApfsDir::Name::Name()
{
	parent_id = 0;
	hash = 0;
	inode_id = 0;
	timestamp = 0;
}

ApfsDir::Name::Name(const ApfsDir::Name& o)
{
	parent_id = o.parent_id;
	hash = o.hash;
	name = o.name;
	inode_id = o.inode_id;
	timestamp = o.timestamp;
}

ApfsDir::ApfsDir(ApfsVolume &vol) :
	m_vol(vol),
	m_bt(vol.getDirectory())
{
	m_txt_fmt = vol.getTextFormat();

	// m_bt.EnableDebugOutput();
}

ApfsDir::~ApfsDir()
{
}

bool ApfsDir::GetInode(ApfsDir::Inode& res, uint64_t inode)
{
	BTreeEntry bte;
	uint64_t key;
	bool rc;

	key = inode | KeyType_Object;

	rc = m_bt.Lookup(bte, &key, sizeof(uint64_t), CompareStdDirKey, this, true);

	if (!rc || (bte.val == nullptr))
		return false;

	const uint8_t *idata = reinterpret_cast<const uint8_t *>(bte.val);
	const APFS_Inode *obj = reinterpret_cast<const APFS_Inode *>(bte.val);
	const APFS_InodeEntry *ie = reinterpret_cast<const APFS_InodeEntry *>(idata + sizeof(APFS_Inode));

	res.id = inode;
	res.ino = *obj;
	res.name.clear();
	memset(&res.sizes, 0, sizeof(res.sizes));
	res.unk_param = 0;

	uint16_t entry_base = sizeof(APFS_Inode) + (obj->entries_cnt * sizeof(APFS_InodeEntry));
	uint16_t k;


	for (k = 0; k < obj->entries_cnt; k++)
	{
		switch (ie[k].type)
		{
		case 0x0204:
			res.name = reinterpret_cast<const char *>(idata + entry_base);
			break;

		case 0x2008:
			res.sizes = *reinterpret_cast<const APFS_Inode_Sizes *>(idata + entry_base);
			break;

		case 0x280D:
			res.unk_param = *reinterpret_cast<const uint64_t *>(idata + entry_base);
			break;

		default:
			std::cerr << std::hex << "WARNING!!!: Unknown Inode Attribute " << std::setw(4) << ie[k].type << " at inode " << inode << std::endl;
			break;
		}

		entry_base += ((ie[k].len + 7) & 0xFFF8);
	}

	return true;
}

bool ApfsDir::ListDirectory(std::vector<Name> &dir, uint64_t inode)
{
	BTreeIterator it;
	BTreeEntry res;
	bool rc;
	uint64_t skey;

	const APFS_Name *vdata;
	const uint8_t *kdata;

	skey = inode | KeyType_Name;

	dir.clear();

	if (m_txt_fmt & 9)
	{
		APFS_Key_Name key;
		key.parent_id = skey;
		key.hash = 0;
		key.name[0] = 0;

		rc = m_bt.GetIterator(it, &key, 12, CompareStdDirKey, this);
	}
	else
	{
		APFS_Key_Name_Old key;
		key.parent_id = skey;
		key.name_len = 0;
		key.name[0] = 0;

		rc = m_bt.GetIterator(it, &key, 10, CompareStdDirKey, this);
	}

	if (!rc)
		return false;

	for (;;)
	{
		Name e;

		rc = it.GetEntry(res);
		if (!rc)
			break;


		kdata = reinterpret_cast<const uint8_t *>(res.key);

		if (g_debug > 8) {
			DumpBuffer(kdata, res.key_len, "entry key");
			DumpBuffer(reinterpret_cast<const uint8_t*>(res.val), res.val_len,
			           "entry val");
		}

		e.parent_id = *reinterpret_cast<const uint64_t *>(kdata);

		if (e.parent_id != skey)
			break;

		e.parent_id &= 0x0FFFFFFFFFFFFFFFULL;

		if (m_txt_fmt != 0)
		{
			e.hash = *reinterpret_cast<const uint32_t *>(kdata + 8);
			e.name = reinterpret_cast<const char *>(kdata + 12);
		}
		else
		{
			e.hash = 0;
			e.name = reinterpret_cast<const char *>(res.key) + 10;
		}

		// assert(res.val_len == sizeof(APFS_Name));

		vdata = reinterpret_cast<const APFS_Name *>(res.val);

		e.inode_id = vdata->id;
		e.timestamp = vdata->timestamp;

		dir.push_back(e);

		it.next();
	}

	return true;
}

bool ApfsDir::LookupName(ApfsDir::Name& res, uint64_t parent_id, const char* name)
{
	bool rc;
	BTreeEntry e;

	res.parent_id = parent_id;
	res.name = name;

	if (m_txt_fmt & 9)
	{
		APFS_Key_Name key;
		key.parent_id = parent_id | KeyType_Name;
		key.hash = HashFilename(name, strlen(name) + 1, (m_txt_fmt & 0x09) == 0x01);
		strcpy_s(key.name, name);
		// printf("Lookup key: key=%016lX hash=%08X name=%s\n", key.parent_id, key.hash, key.name);

		res.hash = key.hash;

		rc = m_bt.Lookup(e, &key, 12 + (key.hash & 0x3FF), CompareStdDirKey, this, true);
	}
	else
	{
		APFS_Key_Name_Old key;
		key.parent_id = parent_id | KeyType_Name;
		key.name_len = strlen(name) + 1;
		strcpy_s(key.name, name);
		// printf("Lookup old key: key=%016lX nlen=%04X name=%s\n", key.parent_id, key.name_len, key.name);

		res.hash = 0;

		rc = m_bt.Lookup(e, &key, 10 + key.name_len, CompareStdDirKey, this, true);
	}

	if (!rc)
		return false;

	const APFS_Name *v = reinterpret_cast<const APFS_Name *>(e.val);

	res.inode_id = v->id;
	res.timestamp = v->timestamp;
	// v->unk ?
	return true;
}

bool ApfsDir::ReadFile(void* data, uint64_t inode, uint64_t offs, size_t size)
{
	BTreeEntry e;
	APFS_Key_Extent key;
	const APFS_Key_Extent *ext_key = nullptr;
	const APFS_Extent *ext_val = nullptr;
	bool rc;

	assert((offs & 0xFFF) == 0);
	assert((size & 0xFFF) == 0);

	uint8_t *bdata = reinterpret_cast<uint8_t *>(data);

	size_t cur_size;
	unsigned idx;

	while (size > 0)
	{
		key.inode = inode | KeyType_Extent;
		key.offset = offs;

		if (g_debug > 8) {
			std::cout << "ReadFile for inode " << inode
				<< ", offset=" << offs
				<< "\n";
		}

		rc = m_bt.Lookup(e, &key, sizeof(key), CompareStdDirKey, this, false);

		if (!rc)
			return false;

		ext_key = reinterpret_cast<const APFS_Key_Extent *>(e.key);
		ext_val = reinterpret_cast<const APFS_Extent *>(e.val);

		if (g_debug > 8) {
			std::cout << " key->inode=" << ext_key->inode
				<< "; key->offset=" << ext_key->offset
				<< "\n"
				<< " val->size=" << ext_val->size
				<< "; val->block=" << ext_val->block
				<< "; val->crypto_id=" << ext_val->crypto_id
				<< "\n";
		}

		if (ext_key->inode != key.inode)
			return false;

		idx = (offs - ext_key->offset) >> 12;
		// ext_val->size has a mysterious upper byte set. At least sometimes.
		// Let us clear it.
		uint64_t extent_size = ext_val->size & 0x00FFFFFFFFFFFFFFULL;

		if (g_debug > 8) {
			std::cout << " key has offset " << ext_key->offset << "\n"
				<< " val has block=" << ext_val->block
				<< ", size=" << extent_size << "\n";
		}

		cur_size = size;
		if (((idx << 12) + cur_size) > extent_size)
			cur_size = extent_size - (idx << 12);
		if (cur_size == 0)
			break; // Die Freuden von Fuse ...
		if (ext_val->block != 0) {
			uint64_t block_id = ext_val->block;
			m_vol.ReadBlocks(bdata, block_id + idx, cur_size >> 12, true,
										   ext_val->crypto_id + idx);
			if (g_debug > 8) {
				std::cout << "reading at offset " << offs
				  << " length " << cur_size
					<< " starting from block " << block_id + idx
					<< " (extent starts at block " << block_id
					<< ")\n";
			}
		}
		else {
			memset(bdata, 0, cur_size);
		}
		bdata += cur_size;
		offs += cur_size;
		size -= cur_size;
		// printf("ReadFile: offs=%016lX size=%016lX\n", offs, size);
	}

	return true;
}

bool ApfsDir::ListAttributes(std::vector<std::string>& names, uint64_t inode)
{
	APFS_Key_Attribute skey;
	const APFS_Key_Attribute *ekey;
	BTreeIterator it;
	BTreeEntry res;
	bool rc;

	skey.inode_key = inode | KeyType_Attribute;
	skey.name_len = 0;
	skey.name[0] = 0;

	rc = m_bt.GetIterator(it, &skey, 10 + skey.name_len, CompareStdDirKey, this);
	if (!rc)
		return false;

	for (;;)
	{
		rc = it.GetEntry(res);
		if (!rc)
			break;

		ekey = reinterpret_cast<const APFS_Key_Attribute *>(res.key);

		if (ekey->inode_key != skey.inode_key)
			break;

		names.push_back(ekey->name);

		it.next();
	}

	return true;
}

bool ApfsDir::GetAttribute(std::vector<uint8_t>& data, uint64_t inode, const char* name)
{
	APFS_Key_Attribute skey;
	const APFS_Attribute *attr;
	const APFS_AttributeLink *alnk = nullptr;

	const uint8_t *adata;
	BTreeEntry res;
	bool rc;

	skey.inode_key = inode | KeyType_Attribute;
	skey.name_len = strlen(name) + 1;
	strcpy_s(skey.name, name);

	rc = m_bt.Lookup(res, &skey, 10 + skey.name_len, CompareStdDirKey, this, true);
	if (!rc)
		return false;

	attr = reinterpret_cast<const APFS_Attribute *>(res.val);
	adata = reinterpret_cast<const uint8_t *>(res.val) + sizeof(APFS_Attribute);

	if (g_debug > 8) {
		std::cout << "GetAttribute: type=" << attr->type << "\n";
	}

	// Original has attr->type == 1, but apparently this could work for
	// type 0x11 as well. Is it a flag?
	if (attr->type & 0x1)
	{
		assert(attr->size == 0x30);
		alnk = reinterpret_cast<const APFS_AttributeLink *>(adata);
		if (g_debug > 8) {
			std::cout << "parsing as a link\n"
				<< " size=" << alnk->size
				<< " size_on_disk=" << alnk->size_on_disk
				<< "\n";
			Inode inode_info;
			GetInode(inode_info, alnk->object_id);
			std::cout << " inode says size=" << inode_info.sizes.size
				<< " size_on_disk=" << inode_info.sizes.size_on_disk
				<< "\n";
		}

		if (g_debug > 8)
			DumpBuffer(adata, sizeof(*alnk), "attribute link data");

		data.resize(alnk->size_on_disk);
		ReadFile(data.data(), alnk->object_id, 0, data.size()); // Read must be multiple of 4K ...
		data.resize(alnk->size);
		if (g_debug > 8 && data.size() >= 0x10) {
			DumpBuffer(data.data(), 0x10, "start of attribute content");
		}
	}
	else // if (attr->type == 2)
	{
		data.assign(adata, adata + attr->size);
	}

	return true;
}

int ApfsDir::CompareStdDirKey(const void *skey, size_t skey_len, const void *ekey, size_t ekey_len, void *context)
{
	// assert(skey_len == 8);
	// assert(ekey_len == 8);

	(void)ekey_len;

	ApfsDir *dir = reinterpret_cast<ApfsDir *>(context);

	uint64_t ks = *reinterpret_cast<const uint64_t *>(skey);
	uint64_t ke = *reinterpret_cast<const uint64_t *>(ekey);

	// std::cout << std::hex << std::uppercase << std::setfill('0');

	ks = (ks << 4) | (ks >> 60);
	ke = (ke << 4) | (ke >> 60);

	// std::cout << std::setw(16) << ks << " : " << std::setw(16) << ke << std::endl;

	if (ke < ks)
		return -1;
	if (ke > ks)
		return 1;

	if (skey_len > 8)
	{
		switch ((ks & 0xF) << 60)
		{
		case KeyType_Name:
			if (dir->m_txt_fmt & 9)
			{
				const APFS_Key_Name *s = reinterpret_cast<const APFS_Key_Name *>(skey);
				const APFS_Key_Name *e = reinterpret_cast<const APFS_Key_Name *>(ekey);

				if ((e->hash & 0xFFFFFC00) < (s->hash & 0xFFFFFC00))
					return -1;
				if ((e->hash & 0xFFFFFC00) > (s->hash & 0xFFFFFC00))
					return 1;

				for (size_t k = 0; k < (e->hash & 0x3FF); k++)
				{
					if (e->name[k] < s->name[k])
						return -1;
					if (e->name[k] > s->name[k])
						return 1;
				}
			}
			else
			{
				const APFS_Key_Name_Old *s = reinterpret_cast<const APFS_Key_Name_Old *>(skey);
				const APFS_Key_Name_Old *e = reinterpret_cast<const APFS_Key_Name_Old *>(ekey);

				size_t cnt = std::min(e->name_len, s->name_len);

				for (size_t k = 0; k < cnt; k++)
				{
					if (e->name[k] < s->name[k])
						return -1;
					if (e->name[k] > s->name[k])
						return 1;
				}

				if (e->name_len < s->name_len)
					return -1;
				if (e->name_len > s->name_len)
					return 1;
			}
			break;
		case KeyType_Extent:
		{
			const APFS_Key_Extent *s = reinterpret_cast<const APFS_Key_Extent *>(skey);
			const APFS_Key_Extent *e = reinterpret_cast<const APFS_Key_Extent *>(ekey);

			assert(skey_len == sizeof(APFS_Key_Extent));
			assert(ekey_len == sizeof(APFS_Key_Extent));

			if (e->offset < s->offset)
				return -1;
			if (e->offset > s->offset)
				return 1;
		}
			break;
		case KeyType_Attribute:
		{
			const APFS_Key_Attribute *s = reinterpret_cast<const APFS_Key_Attribute *>(skey);
			const APFS_Key_Attribute *e = reinterpret_cast<const APFS_Key_Attribute *>(ekey);

			size_t cnt = std::max(e->name_len, s->name_len);

			for (size_t k = 0; k < cnt; k++)
			{
				if (e->name[k] < s->name[k])
					return -1;
				if (e->name[k] > s->name[k])
					return 1;
			}

			if (e->name_len < s->name_len)
				return -1;
			if (e->name_len > s->name_len)
				return 1;
		}
			break;
		}
	}

	return 0;
}
