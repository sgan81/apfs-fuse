/*
 *	This file is part of apfs-fuse, a read-only implementation of APFS
 *	(Apple File System) for FUSE.
 *	Copyright (C) 2023 Simon Gander
 *
 *	Apfs-fuse is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Apfs-fuse is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with apfs-fuse.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "Spaceman.h"
#include "BlockDumper.h"
#include "ObjCache.h"
#include "Container.h"

Spaceman::Spaceman()
{
	sm_phys = nullptr;
}

Spaceman::~Spaceman()
{
}

int Spaceman::init(const void* params)
{
	(void)params;
	sm_phys = reinterpret_cast<const spaceman_phys_t*>(data());
	return 0;
}

void Spaceman::dump(BlockDumper& d)
{
	uint64_t ip_oid;
	uint32_t k;
	std::vector<uint8_t> blk;
	Container& nx = *oc().nx();
	uint32_t blocksize = nx.GetBlocksize();

	d.SetBlockSize(size());
	d.DumpNode(data(), oid());
	d.SetBlockSize(blocksize);

	blk.resize(blocksize);

#if 0
	for (k = 0; k < sm_phys->sm_ip_bm_block_count; k++)
	{
		ip_oid = sm_phys->sm_ip_bm_base + k;

		d.st() << "Dumping IP Bitmap block " << k << std::endl;

		oc().nx()->ReadBlocks(blk.data(), ip_oid);
		d.DumpNode(blk.data(), ip_oid);

		d.st() << std::endl;
	}
#endif

	// TODO dump free queue trees

	for (k = 0; k < SFQ_COUNT; k++) {
		// sm_phys->sm_fq[k].sfq_tree_oid;
		// ...
	}

	// m_omap.dump(bd); // TODO
	// m_omap_tree.dump(bd);
	// m_fq_tree_mgr.dump(bd);
	// m_fq_tree_vol.dump(bd);

	for (k = 0; k < SD_COUNT; k++) {
		const spaceman_device_t &sm_dev = sm_phys->sm_dev[SD_MAIN];
		const le_uint64_t *cxb_oid = reinterpret_cast<const le_uint64_t *>(data() + sm_dev.sm_addr_offset);
		uint32_t cib_cnt = sm_dev.sm_cib_count;
		uint32_t cab_cnt = sm_dev.sm_cab_count;

		if (cib_cnt == 0)
			continue;

		uint32_t cib_id;
		uint32_t cab_id;

		std::vector<uint64_t> cib_oid_list;
		std::vector<uint8_t> cib_data(blocksize);

		cib_oid_list.reserve(cib_cnt);

		if (cab_cnt != 0)
		{
			for (cab_id = 0; cab_id < cab_cnt; cab_id++)
			{
				nx.ReadAndVerifyHeaderBlock(blk.data(), cxb_oid[cab_id]);
				d.DumpNode(blk.data(), cxb_oid[cab_id]);

				const cib_addr_block_t *cab = reinterpret_cast<cib_addr_block_t *>(blk.data());

				for (cib_id = 0; cib_id < cab->cab_cib_count; cib_id++)
					cib_oid_list.push_back(cab->cab_cib_addr[cib_id]);
			}
		}
		else
		{
			for (cib_id = 0; cib_id < cib_cnt; cib_id++)
				cib_oid_list.push_back(cxb_oid[cib_id]);
		}

		for (cib_id = 0; cib_id < cib_cnt; cib_id++)
		{
			nx.ReadAndVerifyHeaderBlock(blk.data(), cib_oid_list[cib_id]);
			d.DumpNode(blk.data(), cib_oid_list[cib_id]);
		}
	}
}
