#include <iostream>
#include <iomanip>

#include <ApfsLib/BlockDumper.h>
#include <ApfsLib/Device.h>
#include <ApfsLib/DiskStruct.h>
#include <ApfsLib/GptPartitionMap.h>
#include <ApfsLib/Util.h>

#include "Dumper.h"

Dumper::Dumper(Device& dev) : m_dev(dev)
{
	m_partbase = 0;
	m_partsize = 0;
	m_blocksize = 0;
	m_is_encrypted = false;
}

Dumper::~Dumper()
{
}

bool Dumper::Initialize()
{
	m_partbase = 0;
	m_partsize = 0;
	m_blocksize = 0;

	GptPartitionMap pmap;

	if (pmap.LoadAndVerify(m_dev))
	{
		int partid = pmap.FindFirstAPFSPartition();

		if (partid >= 0)
		{
			std::cout << "Dumping EFI partition" << std::endl;
			pmap.GetPartitionOffsetAndSize(partid, m_partbase, m_partsize);
		}
	}

	if (m_partsize == 0)
		m_partsize = m_dev.GetSize();

	if (m_partsize == 0)
		return false;

	std::vector<uint8_t> nx_data;
	const nx_superblock_t *nx;

	nx_data.resize(0x1000);
	m_dev.Read(nx_data.data(), m_partbase, 0x1000);

	nx = reinterpret_cast<const nx_superblock_t *>(nx_data.data());

	if (nx->nx_magic != NX_MAGIC)
	{
		std::cerr << "Could not load NX superblock. Invalid magic number." << std::endl;
		return false;
	}

	m_blocksize = nx->nx_block_size;

	if (m_blocksize != 0x1000)
	{
		nx_data.resize(m_blocksize);
		nx = reinterpret_cast<const nx_superblock_t *>(nx_data.data());
		m_dev.Read(nx_data.data(), m_partbase, m_blocksize);
	}

	if (!VerifyBlock(nx_data.data(), m_blocksize))
	{
		std::cerr << "NX superblock checksum incorrect!" << std::endl;
		return false;
	}

	return true;
}

bool Dumper::DumpContainer(std::ostream &os)
{
	BlockDumper bd(os, m_blocksize);

	std::vector<uint8_t> nx_data;
	std::vector<uint8_t> cpm_data;
	std::vector<uint8_t> sm_data;
	std::vector<uint8_t> cxb_data;
	std::vector<uint8_t> bmp_data;
	std::vector<uint8_t> blk_data;

	const nx_superblock_t *nx = nullptr;
	const checkpoint_map_phys_t *cpm = nullptr;
	const spaceman_phys_t *sm = nullptr;
	const chunk_info_block_t *cib = nullptr;
	const cib_addr_block_t *cab = nullptr;

	uint64_t paddr;

	uint64_t block_count;
	uint32_t block_size;
	uint32_t blocks_per_chunk;
	uint32_t chunks_per_cib;
	uint32_t cibs_per_cab;
	uint32_t cib_count;
	uint32_t cab_count;
	const le<uint64_t> *cxb_oid;

	uint32_t cab_id;
	uint32_t cib_id;
	uint32_t chunk_id;
	uint32_t blk_id;
	size_t n;

	std::vector<uint64_t> cib_oid_list;

	// Get NX superblock

	if (!Read(nx_data, 0, 1))
		return false;

	bd.DumpNode(nx_data.data(), 0);

	if (!VerifyBlock(nx_data.data(), nx_data.size()))
		return false;

	nx = reinterpret_cast<const nx_superblock_t *>(nx_data.data());

	// TODO: Scan xp_desc for most recent nxsb

	// Get Check Point Info Block

	if (!Read(cpm_data, nx->nx_xp_desc_base + nx->nx_xp_desc_index, 1))
		return false;

	bd.DumpNode(cpm_data.data(), cpm_data.size());

	if (!VerifyBlock(cpm_data.data(), cpm_data.size()))
		return false;

	cpm = reinterpret_cast<const checkpoint_map_phys_t *>(cpm_data.data());

	paddr = cpm_lookup(cpm, nx->nx_spaceman_oid);

	if (paddr == 0)
		return false;

	// Get Spaceman

	if (!Read(sm_data, paddr, 1))
		return false;

	bd.DumpNode(sm_data.data(), sm_data.size());

	if (!VerifyBlock(sm_data.data(), sm_data.size()))
		return false;

	sm = reinterpret_cast<const spaceman_phys_t *>(sm_data.data());

	os << "Now dumping blocks according to bitmap ..." << std::endl;

	block_size = sm->sm_block_size;
	blocks_per_chunk = sm->sm_blocks_per_chunk;
	chunks_per_cib = sm->sm_chunks_per_cib;
	cibs_per_cab = sm->sm_cibs_per_cab;

	block_count = sm->sm_dev[SD_MAIN].sm_block_count;
	cib_count = sm->sm_dev[SD_MAIN].sm_cib_count;
	cab_count = sm->sm_dev[SD_MAIN].sm_cab_count;
	cxb_oid = reinterpret_cast<const le<uint64_t> *>(sm_data.data() + sm->sm_dev[SD_MAIN].sm_addr_offset);

	paddr = 0;

	std::cout << std::hex;

#if 0
	static const uint8_t vek[0x20] = { 0 /* enter VEK here */ };
	m_is_encrypted = true;
	m_aes.SetKey(vek, vek + 0x10);
#endif

	cib_oid_list.reserve(cib_count);

	if (cab_count > 0)
	{
		for (cab_id = 0; cab_id < cab_count; cab_id++)
		{
			if (!Read(cxb_data, cxb_oid[cab_id], 1))
				return false;
			if (!VerifyBlock(cxb_data.data(), cxb_data.size()))
				return false;

			cab = reinterpret_cast<const cib_addr_block_t *>(cxb_data.data());

			for (n = 0; n < cab->cab_cib_count; n++)
				cib_oid_list.push_back(cab->cab_cib_addr[n]);
		}
	}
	else
	{
		for (n = 0; n < cib_count; n++)
			cib_oid_list.push_back(cxb_oid[n]);
	}

	for (cib_id = 0; cib_id < cib_count; cib_id++)
	{
		std::cout << "cib " << cib_id << std::endl;

		if (!Read(cxb_data, cib_oid_list[cib_id], 1))
			return false;
		if (!VerifyBlock(cxb_data.data(), cxb_data.size()))
			return false;

		cib = reinterpret_cast<const chunk_info_block_t *>(cxb_data.data());

		for (chunk_id = 0; chunk_id < cib->cib_chunk_info_count; chunk_id++)
		{
			if (g_abort)
				return false;

			std::cout << "  chunk " << chunk_id << " avail=" << cib->cib_chunk_info[chunk_id].ci_free_count.get() << " paddr=" << paddr << std::endl;

			if (cib->cib_chunk_info[chunk_id].ci_bitmap_addr == 0)
			{
				paddr += blocks_per_chunk;
				continue;
			}

			if (!Read(bmp_data, cib->cib_chunk_info[chunk_id].ci_bitmap_addr, 1))
				return false;

			for (blk_id = 0; blk_id < blocks_per_chunk && paddr < block_count; blk_id++)
			{
				if (bmp_data[blk_id >> 3] & (1 << (blk_id & 7)))
				{
					Read(blk_data, paddr, 1);
					if (VerifyBlock(blk_data.data(), m_blocksize))
						bd.DumpNode(blk_data.data(), paddr);
					else if (m_is_encrypted)
					{
						Decrypt(blk_data.data(), blk_data.size(), paddr);
						if (VerifyBlock(blk_data.data(), m_blocksize))
							bd.DumpNode(blk_data.data(), paddr);
					}
				}

				++paddr;
			}
		}
	}

	return true;
}

bool Dumper::DumpBlockList(std::ostream& os)
{
	using namespace std;

	constexpr size_t BLOCKSIZE = 0x1000; // Should make this dynamic ...

	uint64_t bid;
	uint8_t block[BLOCKSIZE];
	const obj_phys_t * const o = reinterpret_cast<const obj_phys_t *>(block);
	const btree_node_phys_t * const bt = reinterpret_cast<const btree_node_phys_t *>(block);
	bool last_was_used = false;

	os << hex << uppercase << setfill('0');

	os << "[Block]  | oid      | xid      | type     | subtype  | Page | Levl | Entries  | Description" << endl;
	os << "---------+----------+----------+----------+----------+------+------+----------+---------------------------------" << endl;

	for (bid = 0; bid < (m_partsize / BLOCKSIZE) && !g_abort; bid++)
	{
		if ((bid & 0xFFF) == 0)
		{
			std::cout << '.';
			std::cout.flush();
		}

		Read(block, bid, 1);

		if (IsEmptyBlock(block, BLOCKSIZE))
		{
			if (last_was_used)
				os << "---------+----------+----------+----------+----------+------+------+----------+ Empty" << endl;
			last_was_used = false;
			continue;
		}

		if (VerifyBlock(block, BLOCKSIZE))
		{
			os << setw(8) << bid << " | ";
			os << setw(8) << o->o_oid << " | ";
			os << setw(8) << o->o_xid << " | ";
			os << setw(8) << o->o_type << " | ";
			os << setw(8) << o->o_subtype << " | ";
			os << setw(4) << bt->btn_flags << " | ";
			os << setw(4) << bt->btn_level << " | ";
			os << setw(8) << bt->btn_nkeys << " | ";
			os << BlockDumper::GetNodeType(o->o_type, o->o_subtype);
			if ((o->o_type & OBJECT_TYPE_MASK) == OBJECT_TYPE_BTREE)
				os << " [Root]";
			os << endl;
			last_was_used = true;
		}
		else
		{
			os << setw(8) << bid;
			os << " |          |          |          |          |      |      |          | Data" << endl;
			last_was_used = true;
		}
	}

	os << endl;

	return true;
}

bool Dumper::Read(void* data, uint64_t paddr, uint64_t cnt)
{
	return m_dev.Read(data, paddr * m_blocksize + m_partbase, cnt * m_blocksize);
}

bool Dumper::Read(std::vector<uint8_t>& data, uint64_t paddr, uint64_t cnt)
{
	if (data.size() != cnt * m_blocksize)
		data.resize(cnt * m_blocksize);

	return m_dev.Read(data.data(), paddr * m_blocksize + m_partbase, cnt * m_blocksize);
}

void Dumper::Decrypt(uint8_t* data, size_t size, uint64_t paddr)
{
	uint64_t cs_factor = m_blocksize / 0x200;
	uint64_t uno = paddr * cs_factor;
	size_t k;

	for (k = 0; k < size; k += 0x200)
	{
		m_aes.Decrypt(data + k, data + k, 0x200, uno);
		uno++;
	}
}

uint64_t Dumper::cpm_lookup(const checkpoint_map_phys_t* cpm, uint64_t oid)
{
	uint32_t k;
	uint32_t cnt;

	cnt = cpm->cpm_count;

	for (k = 0; k < cnt; k++)
	{
		if (cpm->cpm_map[k].cpm_oid == oid)
			return cpm->cpm_map[k].cpm_paddr;
	}

	return 0;
}
