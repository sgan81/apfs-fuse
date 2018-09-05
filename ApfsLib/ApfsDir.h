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

#include <string>
#include <vector>

#include "DiskStruct.h"

class BTree;
class ApfsVolume;

class ApfsDir
{
public:
	struct Inode
	{
		Inode();
		Inode(const Inode &other);

		uint64_t id;

		APFS_Inode_Val ino;
		std::string name;
		APFS_DStream sizes;
		uint64_t unk_param;
	};

	struct Name
	{
		Name();
		Name(const Name &other);

		uint64_t parent_id;
		uint32_t hash;
		std::string name;

		uint64_t inode_id;
		uint64_t timestamp;
		// TBD
	};


	ApfsDir(ApfsVolume &vol);
	~ApfsDir();

	bool GetInode(Inode &res, uint64_t inode);

	bool ListDirectory(std::vector<Name> &dir, uint64_t inode);
	bool LookupName(Name &res, uint64_t parent_id, const char *name);
	bool ReadFile(void *data, uint64_t inode, uint64_t offs, size_t size);
	bool ListAttributes(std::vector<std::string> &names, uint64_t inode);
	bool GetAttribute(std::vector<uint8_t> &data, uint64_t inode, const char *name);

private:
	static int CompareStdDirKey(const void *skey, size_t skey_len, const void *ekey, size_t ekey_len, void *context);

	ApfsVolume &m_vol;
	BTree &m_bt;
	uint32_t m_txt_fmt;
	uint32_t m_blksize;
	uint64_t m_blksize_mask_hi;
	uint64_t m_blksize_mask_lo;
	int m_blksize_sh;
	std::vector<uint8_t> m_tmp_blk;
};
