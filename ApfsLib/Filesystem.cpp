#include <cstring>
#include <cassert>
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
