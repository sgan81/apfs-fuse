#pragma once

#include "DiskStruct.h"
#include <cstdint>
#include <string>
#include <vector>
#include "Volume.h"

struct Inode
{
	Inode();
	Inode(const Inode &other);

	/* TODO */
	uint64_t obj_id;

	uint64_t parent_id;
	uint64_t private_id;

	uint64_t create_time;
	uint64_t mod_time;
	uint64_t change_time;
	uint64_t access_time;

	uint64_t internal_flags;

	uint64_t nchildren_nlink;

	cp_key_class_t default_protection_class;
	uint32_t write_generation_counter;
	uint32_t bsd_flags;
	uint32_t owner;
	uint32_t group;
	uint16_t mode;

	uint64_t uncompressed_size;

	uint64_t snap_xid;
	uint64_t delta_tree_oid;
	uint64_t prev_fsize;
	// FinderInfo
	uint64_t ds_size;
	uint64_t ds_alloced_size;
	uint64_t ds_default_crypto_id;
	uint64_t ds_total_bytes_written;
	uint64_t ds_total_bytes_read;
	// j_dir_stats_val_t dir_stats;
	apfs_uuid_t fs_uuid;
	uint64_t sparse_bytes;
	uint32_t document_id;
	uint32_t rdev;
	std::string name;

	uint32_t optional_present_flags;

	enum PresentFlags {
		INO_HAS_SNAP_XID = 1,
		INO_HAS_DELTA_TREE_OID = 2,
		INO_HAS_DOCUMENT_ID = 4,
		INO_HAS_NAME = 8,
		INO_HAS_PREV_FSIZE = 16,
		INO_HAS_FINDER_INFO = 64,
		INO_HAS_DSTREAM = 128,
		INO_HAS_DIR_STATS = 512,
		INO_HAS_FS_UUID = 1024,
		INO_HAS_SPARSE_BYTES = 4096,
		INO_HAS_RDEV = 8192
	};
};

struct DirRec
{
	DirRec();
	DirRec(const DirRec &other);

	uint64_t parent_id;
	uint32_t hash;
	std::string name;

	uint64_t file_id;
	uint64_t date_added;

	uint64_t sibling_id;
	uint16_t flags;
	bool has_sibling_id;
};

struct XAttr
{
	XAttr();
	XAttr(const XAttr &other);

	uint16_t flags;
	uint16_t xdata_len;
	j_xattr_dstream_t xstrm;
};

class DStream
{
public:
	DStream(Volume& v);
	~DStream();

	int open(uint64_t private_id, const j_dstream_t& dstm);
	int pread(uint8_t* data, size_t size, uint64_t offset, size_t* nread_o);
	int close();

	int preadAligned(uint8_t* data, size_t size, uint64_t offset);

private:
	int lookupExtent(uint64_t offset);

	Volume& m_fs;
	uint32_t m_blksize;
	uint32_t m_blksize_mask;
	uint32_t m_blksize_sh;

	uint64_t m_private_id;
	j_dstream_t m_dstm;
	uint64_t m_cur_offset;
	prange_t m_cur_prange;
	uint64_t m_cur_crypto_id;

	uint8_t* m_buffer;
	uint64_t m_buffer_offs;
};

class Filesystem
{
public:
	Filesystem();
	~Filesystem();

	/*
	bool GetInode(Inode &res, uint64_t inode);

	bool ListDirectory(std::vector<DirRec> &dir, uint64_t inode);
	bool LookupName(DirRec &res, uint64_t parent_id, const char *name);
	bool ReadFile(void *data, uint64_t inode, uint64_t offs, size_t size);
	bool ListAttributes(std::vector<std::string> &names, uint64_t inode);
	bool GetAttribute(std::vector<uint8_t> &data, uint64_t inode, const char *name);
	bool GetAttributeInfo(XAttr &attr, uint64_t inode, const char *name);
	*/

	int lookupName(uint64_t parent_id, const char* name, uint64_t& child_id, uint64_t& child_ts);

	int lookupExtent(uint64_t id, uint64_t offset, prange_t& range);

private:


};
