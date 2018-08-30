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

#ifndef _MSC_VER
template<size_t L>
void strcpy_s(char (&dst)[L], const char *src)
{
	strncpy(dst, src, L - 1);
	dst[L - 1] = 0;
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

	key = inode | KeyType_Inode;

	rc = m_bt.Lookup(bte, &key, sizeof(uint64_t), CompareStdDirKey, this, true);

	if (!rc || (bte.val == nullptr))
		return false;

	const uint8_t *idata = reinterpret_cast<const uint8_t *>(bte.val);
	const APFS_Inode_Val *obj = reinterpret_cast<const APFS_Inode_Val *>(bte.val);
	const APFS_XF_Header *xf_hdr = reinterpret_cast<const APFS_XF_Header *>(idata + sizeof(APFS_Inode_Val));
	const APFS_XF_Entry *xf = reinterpret_cast<const APFS_XF_Entry *>(idata + sizeof(APFS_Inode_Val) + sizeof(APFS_XF_Header));

	res.id = inode;
	res.ino = *obj;
	res.name.clear();
	memset(&res.sizes, 0, sizeof(res.sizes));
	res.unk_param = 0;

	uint16_t entry_base = sizeof(APFS_Inode_Val) + sizeof(APFS_XF_Header) + (xf_hdr->xf_num_exts * sizeof(APFS_XF_Entry));
	uint16_t k;


	for (k = 0; k < xf_hdr->xf_num_exts; k++)
	{
		switch (xf[k].xf_type)
		{
		case INO_EXT_TYPE_NAME:
			res.name = reinterpret_cast<const char *>(idata + entry_base);
			break;

		case INO_EXT_TYPE_DSTREAM:
			res.sizes = *reinterpret_cast<const APFS_DStream *>(idata + entry_base);
			break;

		case INO_EXT_TYPE_SPARSE_BYTES:
			res.unk_param = *reinterpret_cast<const uint64_t *>(idata + entry_base);
			break;

		default:
			std::cerr << std::hex << "WARNING!!!: Unknown Inode XF : ";
			std::cerr << std::setw(2) << static_cast<unsigned>(xf[k].xf_type) << ' ';
			std::cerr << std::setw(2) << static_cast<unsigned>(xf[k].xf_flags) << ' ';
			std::cerr << std::setw(4) << xf[k].xf_length;
			std::cerr << " : at inode " << inode << std::endl;
			DumpHex(std::cerr, idata + entry_base, xf[k].xf_length, 8);
			break;
		}

		entry_base += ((xf[k].xf_length + 7) & 0xFFF8);
	}

	return true;
}

bool ApfsDir::ListDirectory(std::vector<Name> &dir, uint64_t inode)
{
	BTreeIterator it;
	BTreeEntry res;
	bool rc;
	uint64_t skey;

	const APFS_DRec_Val *vdata;
	const uint8_t *kdata;

	skey = inode | KeyType_DirRecord;

	dir.clear();

	if (m_txt_fmt & 9)
	{
		APFS_DRec_Key key;
		key.parent_id = skey;
		key.hash = 0;
		key.name[0] = 0;

		rc = m_bt.GetIterator(it, &key, 12, CompareStdDirKey, this);
	}
	else
	{
		APFS_DRec_Key_Old key;
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

		if (g_debug & Dbg_Dir)
		{
			DumpBuffer(kdata, res.key_len, "entry key");
			DumpBuffer(reinterpret_cast<const uint8_t*>(res.val), res.val_len, "entry val");
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

		vdata = reinterpret_cast<const APFS_DRec_Val *>(res.val);

		e.inode_id = vdata->file_id;
		e.timestamp = vdata->date_added;

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
		APFS_DRec_Key key;
		key.parent_id = parent_id | KeyType_DirRecord;
		key.hash = HashFilename(name, strlen(name) + 1, (m_txt_fmt & 0x09) == 0x01);
		strcpy_s(key.name, name);
		if (g_debug & Dbg_Dir)
		{
			std::cout << "Lookup key: key=" << key.parent_id << " hash=" << key.hash << " name='" << key.name << "'" << std::endl;
			dump_utf8(std::cout, key.name);
		}
		res.hash = key.hash;

		rc = m_bt.Lookup(e, &key, 12 + (key.hash & 0x3FF), CompareStdDirKey, this, true);
	}
	else
	{
		APFS_DRec_Key_Old key;
		key.parent_id = parent_id | KeyType_DirRecord;
		key.name_len = strlen(name) + 1;
		strcpy_s(key.name, name);
		if (g_debug & Dbg_Dir)
		{
			std::cout << "Lookup old key: key=" << key.parent_id << " name_len=" << key.name_len << " name='" << key.name << "'" << std::endl;
			dump_utf8(std::cout, key.name);
		}
		res.hash = 0;

		rc = m_bt.Lookup(e, &key, 10 + key.name_len, CompareStdDirKey, this, true);
	}

	if (!rc)
		return false;

	const APFS_DRec_Val *v = reinterpret_cast<const APFS_DRec_Val *>(e.val);

	res.inode_id = v->file_id;
	res.timestamp = v->date_added;
	// v->unk ?
	return true;
}

bool ApfsDir::ReadFile(void* data, uint64_t inode, uint64_t offs, size_t size)
{
	BTreeEntry e;
	APFS_FileExtent_Key key;
	const APFS_FileExtent_Key *ext_key = nullptr;
	const APFS_FileExtent_Val *ext_val = nullptr;
	bool rc;

	assert((offs & 0xFFF) == 0);
	assert((size & 0xFFF) == 0);

	uint8_t *bdata = reinterpret_cast<uint8_t *>(data);

	size_t cur_size;
	unsigned idx;

	while (size > 0)
	{
		key.obj_id = inode | KeyType_FileExtent;
		key.logical_addr = offs;

		if (g_debug & Dbg_Dir)
			std::cout << "ReadFile(inode=" << inode << ",offs=" << offs << ",size=" << size << ")" << std::endl;

		rc = m_bt.Lookup(e, &key, sizeof(key), CompareStdDirKey, this, false);

		if (!rc)
			return false;

		ext_key = reinterpret_cast<const APFS_FileExtent_Key *>(e.key);
		ext_val = reinterpret_cast<const APFS_FileExtent_Val *>(e.val);

		if (g_debug & Dbg_Dir)
		{
			std::cout << "Key: inode=" << ext_key->obj_id << ", offset=" << ext_key->logical_addr << std::endl;
			std::cout << "Value: size=" << ext_val->flags_length << ", block=" << ext_val->phys_block_num << ", xts_iv=" << ext_val->crypto_id << std::endl;
		}

		if (ext_key->obj_id != key.obj_id)
			return false;

		// TODO: 12 is dependent on block size ...
		idx = (offs - ext_key->logical_addr) >> 12;
		// ext_val->size has a mysterious upper byte set. At least sometimes.
		// Let us clear it.
		uint64_t extent_size = ext_val->flags_length & 0x00FFFFFFFFFFFFFFULL;

		cur_size = size;
		if (((idx << 12) + cur_size) > extent_size)
			cur_size = extent_size - (idx << 12);
		if (cur_size == 0)
			break;
		if (ext_val->phys_block_num != 0)
			m_vol.ReadBlocks(bdata, ext_val->phys_block_num + idx, cur_size >> 12, true, ext_val->crypto_id + idx);
		else
			memset(bdata, 0, cur_size);
		bdata += cur_size;
		offs += cur_size;
		size -= cur_size;
		// printf("ReadFile: offs=%016lX size=%016lX\n", offs, size);
	}

	return true;
}

bool ApfsDir::ListAttributes(std::vector<std::string>& names, uint64_t inode)
{
	APFS_Xattr_Key skey;
	const APFS_Xattr_Key *ekey;
	BTreeIterator it;
	BTreeEntry res;
	bool rc;

	skey.inode_key = inode | KeyType_Xattr;
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

		ekey = reinterpret_cast<const APFS_Xattr_Key *>(res.key);

		if (ekey->inode_key != skey.inode_key)
			break;

		names.push_back(ekey->name);

		it.next();
	}

	return true;
}

bool ApfsDir::GetAttribute(std::vector<uint8_t>& data, uint64_t inode, const char* name)
{
	APFS_Xattr_Key skey;
	const APFS_Xattr_Val *attr;
	const APFS_Xattr_External *alnk = nullptr;

	const uint8_t *adata;
	BTreeEntry res;
	bool rc;

	skey.inode_key = inode | KeyType_Xattr;
	skey.name_len = strlen(name) + 1;
	strcpy_s(skey.name, name);

	rc = m_bt.Lookup(res, &skey, 10 + skey.name_len, CompareStdDirKey, this, true);
	if (!rc)
		return false;

	attr = reinterpret_cast<const APFS_Xattr_Val *>(res.val);
	adata = reinterpret_cast<const uint8_t *>(res.val) + sizeof(APFS_Xattr_Val);

	if (g_debug & Dbg_Dir)
		std::cout << "GetAttribute: type=" << attr->type << std::endl;

	if (attr->type & 0x0001)
	{
		// Attribute contents are stored in a file
		assert(attr->size == 0x30);
		alnk = reinterpret_cast<const APFS_Xattr_External *>(adata);
		if (g_debug & Dbg_Dir)
		{
			std::cout << "Attr is link: size = " << alnk->stream.size << ", size on disk = " << alnk->stream.alloced_size << std::endl;
		}

		if (g_debug & Dbg_Dir)
		{
			std::cout << "Attribute is link:" << std::endl;
			std::cout << "  obj_id       : " << alnk->obj_id << std::endl;
			std::cout << "  size         : " << alnk->stream.size << std::endl;
			std::cout << "  alloced_size : " << alnk->stream.alloced_size << std::endl;
			std::cout << "  default_crypto_id : " << alnk->stream.default_crypto_id << std::endl;
			std::cout << "  unk@20  : " << alnk->stream.unk_18 << std::endl;
			std::cout << "  unk@28  : " << alnk->stream.unk_20 << std::endl;

			Inode inode_info;
			bool rc;

			rc = GetInode(inode_info, alnk->obj_id);

			if (rc)
				std::cout << "Inode: size = " << inode_info.sizes.size << " size on disk = " << inode_info.sizes.alloced_size << std::endl;
			else
				std::cout << "No inode found for this attribute." << std::endl;
		}

		data.resize(alnk->stream.alloced_size);
		ReadFile(data.data(), alnk->obj_id, 0, data.size()); // Read must be multiple of 4K ...
		data.resize(alnk->stream.size);

		if (g_debug & Dbg_Dir)
		{
			size_t dmpsize = 0x40;
			if (dmpsize > data.size())
				dmpsize = data.size();
			DumpBuffer(data.data(), dmpsize, "start of attribute content");
		}
	}
	else // if (attr->type == 2)
	{
		// Attribute contents are stored in the btree
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

	uint64_t ks = *reinterpret_cast<const le<uint64_t> *>(skey);
	uint64_t ke = *reinterpret_cast<const le<uint64_t> *>(ekey);

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
		case KeyType_DirRecord:
			if (dir->m_txt_fmt & 9)
			{
				const APFS_DRec_Key *s = reinterpret_cast<const APFS_DRec_Key *>(skey);
				const APFS_DRec_Key *e = reinterpret_cast<const APFS_DRec_Key *>(ekey);

				if ((e->hash & 0xFFFFFC00) < (s->hash & 0xFFFFFC00))
					return -1;
				if ((e->hash & 0xFFFFFC00) > (s->hash & 0xFFFFFC00))
					return 1;

#if 1
				return StrCmpUtf8NormalizedFolded(e->name, s->name, (dir->m_txt_fmt & APFS_APSB_CaseInsensitive) != 0);
#else
				// TODO: This is not case insensitive ...
				for (size_t k = 0; k < (e->hash & 0x3FF); k++)
				{
					if (e->name[k] < s->name[k])
						return -1;
					if (e->name[k] > s->name[k])
						return 1;
				}
#endif
			}
			else
			{
				const APFS_DRec_Key_Old *s = reinterpret_cast<const APFS_DRec_Key_Old *>(skey);
				const APFS_DRec_Key_Old *e = reinterpret_cast<const APFS_DRec_Key_Old *>(ekey);

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
		case KeyType_FileExtent:
		{
			const APFS_FileExtent_Key *s = reinterpret_cast<const APFS_FileExtent_Key *>(skey);
			const APFS_FileExtent_Key *e = reinterpret_cast<const APFS_FileExtent_Key *>(ekey);

			assert(skey_len == sizeof(APFS_FileExtent_Key));
			assert(ekey_len == sizeof(APFS_FileExtent_Key));

			if (e->logical_addr < s->logical_addr)
				return -1;
			if (e->logical_addr > s->logical_addr)
				return 1;
		}
			break;
		case KeyType_Xattr:
		{
			const APFS_Xattr_Key *s = reinterpret_cast<const APFS_Xattr_Key *>(skey);
			const APFS_Xattr_Key *e = reinterpret_cast<const APFS_Xattr_Key *>(ekey);

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
