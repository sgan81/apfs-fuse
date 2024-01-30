#include <cstring>
#include <cassert>
#include <cinttypes>
#include "Filesystem.h"
#include "Util.h"

DStream::DStream(Volume& fs) : m_fs(fs)
{
	m_blksize = m_fs.getBlocksize();
	m_blksize_mask = m_blksize - 1;
	assert((m_blksize & (m_blksize_mask)) == 0);
	m_blksize_sh = log2(m_blksize);

	memset(&m_dstm, 0, sizeof(m_dstm));
	m_cur_offset = 0;
	m_cur_prange.pr_start_addr = 0;
	m_cur_prange.pr_block_count = 0;
	m_cur_crypto_id = 0;

	m_buffer = nullptr;
	m_buffer_offs = 0;
}

DStream::~DStream()
{
	delete[] m_buffer;
}

int DStream::open(uint64_t private_id, const j_dstream_t& dstm)
{
	close();

	m_private_id = private_id;
	m_dstm = dstm;

	return 0;
}

int DStream::pread(uint8_t* data, size_t size, uint64_t offset, size_t* nread_o)
{
	int err;
	uint64_t offset_a;
	uint64_t offset_lo;
	size_t chunk_size;
	size_t nread = 0;

	if (offset >= m_dstm.size)
		size = 0;
	else if ((offset + size) > m_dstm.size)
		size = m_dstm.size - offset;

	if ((offset & m_blksize_mask) != 0) {
		if (m_buffer == nullptr) {
			m_buffer = new uint8_t[m_blksize];
			m_buffer_offs = UINT64_MAX;
		}
		offset_a = offset & ~m_blksize_mask;
		offset_lo = offset & m_blksize_mask;
		chunk_size = m_blksize - offset_lo;
		if (chunk_size > size)
			chunk_size = size;
		if (m_buffer_offs != offset_a) {
			err = preadAligned(m_buffer, m_blksize, offset_a);
			if (err) return err;
		}
		memcpy(data, m_buffer + offset_lo, chunk_size);
		data += chunk_size;
		size -= chunk_size;
		offset += chunk_size;
		nread += chunk_size;
	}

	if (size > 0) {
		chunk_size = size & ~m_blksize_mask;
		err = preadAligned(data, chunk_size, offset);
		if (err) return err;
		data += chunk_size;
		size -= chunk_size;
		offset += chunk_size;
		nread += chunk_size;
	}

	if (size > 0) {
		err = preadAligned(m_buffer, m_blksize, offset);
		if (err) return err;
		memcpy(data, m_buffer, size);
		nread += size;
	}

	if (nread_o)
		*nread_o = nread;

	return 0;
}

int DStream::close()
{
	m_private_id = 0;
	memset(&m_dstm, 0, sizeof(m_dstm));
	m_cur_offset = 0;
	m_cur_prange.pr_start_addr = 0;
	m_cur_prange.pr_block_count = 0;
	m_cur_crypto_id = 0;
	delete[] m_buffer;
	m_buffer = nullptr;
	m_buffer_offs = 0;

	return 0;
}

int DStream::preadAligned(uint8_t* data, size_t size, uint64_t offset)
{
	int err;
	uint64_t chunk_size;
	uint64_t blk_offset;
	uint64_t blk_count;

	while (size > 0) {
		err = lookupExtent(offset);
		if (err) return err;

		chunk_size = m_cur_offset + (m_cur_prange.pr_block_count << m_blksize_sh) - offset;
		if (chunk_size > size)
			chunk_size = size;

		blk_offset = (offset - m_cur_offset) >> m_blksize_sh;
		blk_count = chunk_size >> m_blksize_sh;

		if (m_cur_prange.pr_start_addr == 0) {
			memset(data, 0, chunk_size);
		} else {
			err = m_fs.ReadBlocks(data, m_cur_prange.pr_start_addr + blk_offset, blk_count, m_cur_crypto_id + blk_offset);
			if (err) return err;
		}

		data += chunk_size;
		size -= chunk_size;
		offset += chunk_size;
	}

	return 0;
}

int DStream::lookupExtent(uint64_t offset)
{
	if (m_cur_prange.pr_block_count > 0 && offset >= m_cur_offset && offset < (m_cur_offset + (m_cur_prange.pr_block_count << m_blksize_sh)))
		return 0;

	if (m_fs.isSealed()) {
		fext_tree_key_t fk;
		fext_tree_val_t fv;
		uint16_t key_size = sizeof(j_file_extent_key_t);
		uint16_t val_size = sizeof(j_file_extent_val_t);

		m_cur_crypto_id = 0;
		fk.private_id = m_private_id;
		fk.logical_addr = offset;
		m_fs.fexttree().Lookup(&fk, sizeof(j_file_extent_key_t), key_size, &fv, val_size, BTree::FindMode::LE);
		if (fk.private_id != m_private_id) {
			m_cur_offset = 0;
			m_cur_prange.pr_start_addr = 0;
			m_cur_prange.pr_block_count = 0;
			return ENOENT;
		}
		m_cur_offset = fk.logical_addr;
		m_cur_prange.pr_start_addr = fv.phys_block_num;
		m_cur_prange.pr_block_count = fv.len_and_flags & J_FILE_EXTENT_LEN_MASK;
	} else {
		j_file_extent_key_t fk;
		j_file_extent_val_t fv;
		uint16_t key_size = sizeof(j_file_extent_key_t);
		uint16_t val_size = sizeof(j_file_extent_val_t);
		uint64_t key_id = APFS_TYPE_ID(APFS_TYPE_FILE_EXTENT, m_private_id);

		fk.hdr.obj_id_and_type = key_id;
		fk.logical_addr = offset;
		m_fs.fstree().Lookup(&fk, sizeof(j_file_extent_key_t), key_size, &fv, val_size, BTree::FindMode::LE);
		if (key_id != fk.hdr.obj_id_and_type) {
			m_cur_offset = 0;
			m_cur_prange.pr_start_addr = 0;
			m_cur_prange.pr_block_count = 0;
			m_cur_crypto_id = 0;
			return ENOENT;
		}
		m_cur_offset = fk.logical_addr;
		m_cur_prange.pr_start_addr = fv.phys_block_num;
		m_cur_prange.pr_block_count = fv.len_and_flags & J_FILE_EXTENT_LEN_MASK;
		m_cur_crypto_id = fv.crypto_id;
	}

	return 0;
}

Filesystem::Filesystem(Volume& vol) : m_vol(vol)
{

}

Filesystem::~Filesystem()
{

}

int Filesystem::getInode(Inode& res, uint64_t id)
{
	return ENOTSUP;
}

int Filesystem::listDirectory(std::vector<DirRec>& dir, uint64_t parent_id)
{
	return ENOTSUP;
}

int Filesystem::lookupName(DirRec& res, uint64_t parent_id, const char* name)
{
	int err;
	uint8_t key_buf[JOBJ_MAX_VALUE_SIZE];
	uint8_t val_buf[JOBJ_MAX_VALUE_SIZE];
	size_t name_len = strlen(name) + 1;
	uint16_t key_len;
	uint16_t val_len;
	uint16_t skey_len;
	uint32_t txt_fmt;

	if (name_len > 0x400)
		return false;

	res.parent_id = parent_id;
	res.name = name;

	txt_fmt = m_vol.getTextFormat();

	if (txt_fmt & (APFS_INCOMPAT_CASE_INSENSITIVE | APFS_INCOMPAT_NORMALIZATION_INSENSITIVE)) {
		j_drec_hashed_key_t *skey = reinterpret_cast<j_drec_hashed_key_t *>(key_buf);

		skey->hdr.obj_id_and_type = APFS_TYPE_ID(APFS_TYPE_DIR_REC, parent_id);
		skey->name_len_and_hash = HashFilename(reinterpret_cast<const uint8_t *>(name), static_cast<uint16_t>(name_len), (txt_fmt & APFS_INCOMPAT_CASE_INSENSITIVE) != 0);
		memcpy(skey->name, name, name_len);
		res.hash = skey->name_len_and_hash;

		skey_len = sizeof(j_drec_hashed_key_t) + (skey->name_len_and_hash & J_DREC_LEN_MASK);
		key_len = JOBJ_MAX_KEY_SIZE;
		val_len = JOBJ_MAX_VALUE_SIZE;

		err = m_vol.fstree().Lookup(skey, skey_len, key_len, val_buf, val_len, BTree::FindMode::EQ);
	} else {
		j_drec_key_t *skey = reinterpret_cast<j_drec_key_t *>(key_buf);
		skey->hdr.obj_id_and_type = parent_id | (static_cast<uint64_t>(APFS_TYPE_DIR_REC) << OBJ_TYPE_SHIFT);
		skey->name_len = static_cast<uint16_t>(name_len);
		memcpy(skey->name, name, name_len);
		res.hash = 0;

		skey_len = sizeof(j_drec_key_t) + skey->name_len;
		key_len = JOBJ_MAX_KEY_SIZE;
		val_len = JOBJ_MAX_VALUE_SIZE;

		err = m_vol.fstree().Lookup(skey, skey_len, key_len, val_buf, val_len, BTree::FindMode::EQ);
	}

	if (err) {
		log_error("lookup_name %s failed, err = %d\n", name, err);
		return false;
	}

	const j_drec_val_t *v = reinterpret_cast<const j_drec_val_t *>(val_buf);

	res.file_id = v->file_id;
	res.date_added = v->date_added;
	// v->unk ?
	// TODO: SIBLING-ID! XF!

	if (g_debug & Dbg_Dir)
		log_debug("lookup name %s -> %" PRIx64 "\n", name, res.file_id);

	return 0;
}

int Filesystem::fileOpen(File& file, uint64_t id)
{
	j_inode_key_t inode_key;
	union {
		uint8_t d[JOBJ_MAX_VALUE_SIZE];
		j_inode_val_t v;
	} inode_val;
	uint16_t key_len;
	uint16_t val_len;
	int err;
	bool compressed;

	inode_key.hdr.obj_id_and_type = APFS_TYPE_ID(APFS_TYPE_INODE, id);
	key_len = sizeof(j_inode_key_t);
	val_len = JOBJ_MAX_VALUE_SIZE;

	err = m_vol.fstree().Lookup(&inode_key, key_len, key_len, inode_val.d, val_len, BTree::FindMode::EQ);
	if (err) return err;

	compressed = (inode_val.v.internal_flags & APFS_UF_COMPRESSED) != 0;

	if (compressed) {

	} else {

	}

	return ENOTSUP;
}

int Filesystem::fileRead(File& file, void* data, uint64_t size, uint64_t offs)
{
	return ENOTSUP;
}

int Filesystem::fileClose(File& file)
{
	return ENOTSUP;
}

int Filesystem::listXattr(std::vector<std::string>& names, uint64_t id)
{
	return ENOTSUP;
}

int Filesystem::xattrOpen(XAttr& xattr, uint64_t id, const char* name)
{
	return ENOTSUP;
}

int Filesystem::xattrRead(XAttr& xattr, void* data, uint64_t size, uint64_t offs)
{
	return ENOTSUP;
}

int Filesystem::xattrClose(XAttr& xattr)
{
	return ENOTSUP;
}

int Filesystem::lookupName(uint64_t parent_id, const char* name, uint64_t& child_id, uint64_t& child_ts)
{
	return ENOTSUP;
}
