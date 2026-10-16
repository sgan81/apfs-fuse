#include <ApfsLib/ApfsContainer.h>
#include <ApfsLib/Util.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>

static void require(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << std::endl;
		std::exit(1);
	}
}

// Synthetic metadata only. There are no file trees, credentials, or volume keys.
class Fixture : public Device
{
public:
	static constexpr size_t block_size = 4096;
	std::vector<uint8_t> bytes;
	std::vector<size_t> reads;

	Fixture() : bytes(12 * block_size)
	{
		auto &nx = object<nx_superblock_t>(0);
		header(0, OBJECT_TYPE_NX_SUPERBLOCK);
		nx.nx_magic = NX_MAGIC;
		nx.nx_block_size = block_size;
		nx.nx_block_count = 12;
		nx.nx_xp_desc_base = 1;
		nx.nx_xp_desc_blocks = 2;
		nx.nx_xp_desc_len = 2;
		nx.nx_spaceman_oid = 500;
		nx.nx_omap_oid = 3;
		nx.nx_max_file_systems = NX_MAX_FILE_SYSTEMS;
		nx.nx_fs_oid[0] = 1000;
		nx.nx_fs_oid[2] = 1001; // Leave an empty slot between the two volumes.
		nx.nx_keylocker.pr_start_addr = 9;
		nx.nx_keylocker.pr_block_count = 1;
		checksum(0);
		memcpy(bytes.data() + 2 * block_size, bytes.data(), block_size);

		header(1, OBJECT_TYPE_CHECKPOINT_MAP);
		auto &cp = object<checkpoint_map_phys_t>(1);
		cp.cpm_flags = CHECKPOINT_MAP_LAST;
		cp.cpm_count = 1;
		cp.cpm_map[0].cpm_type = OBJECT_TYPE_SPACEMAN;
		cp.cpm_map[0].cpm_size = block_size;
		cp.cpm_map[0].cpm_oid = 500;
		cp.cpm_map[0].cpm_paddr = 5;
		checksum(1);

		header(3, OBJECT_TYPE_OMAP);
		object<omap_phys_t>(3).om_tree_oid = 4;
		checksum(3);
		header(4, OBJECT_TYPE_BTREE, OBJECT_TYPE_OMAP);
		auto &node = object<btree_node_phys_t>(4);
		node.btn_flags = BTNODE_ROOT | BTNODE_LEAF | BTNODE_FIXED_KV_SIZE;
		node.btn_nkeys = 2;
		node.btn_table_space.len = 2 * sizeof(kvoff_t);
		for (unsigned k = 0; k < 2; k++) {
			auto &entry = object<kvoff_t>(4, sizeof(node) + k * sizeof(kvoff_t));
			entry.k = k * sizeof(omap_key_t);
			entry.v = (k + 1) * sizeof(omap_val_t);
			auto &key = object<omap_key_t>(4, sizeof(node) + node.btn_table_space.len + entry.k);
			key.ok_oid = 1000 + k;
			key.ok_xid = 1;
			auto &val = object<omap_val_t>(4, block_size - sizeof(btree_info_t) - entry.v);
			val.ov_size = block_size;
			val.ov_paddr = 6 + k;
		}
		auto &info = object<btree_info_t>(4, block_size - sizeof(btree_info_t));
		info.bt_fixed.bt_node_size = block_size;
		info.bt_fixed.bt_key_size = sizeof(omap_key_t);
		info.bt_fixed.bt_val_size = sizeof(omap_val_t);
		info.bt_key_count = 2;
		info.bt_node_count = 1;
		checksum(4);
		header(5, OBJECT_TYPE_SPACEMAN);
		object<obj_phys_t>(5).o_oid = 500;
		checksum(5);

		for (unsigned k = 0; k < 2; k++) {
			header(6 + k, OBJECT_TYPE_FS);
			auto &vol = object<apfs_superblock_t>(6 + k);
			vol.apfs_o.o_oid = 1000 + k;
			vol.apfs_magic = APFS_MAGIC;
			vol.apfs_fs_flags = k ? APFS_FS_UNENCRYPTED : 0;
			vol.apfs_role = k ? APFS_VOL_ROLE_SYSTEM : APFS_VOL_ROLE_DATA;
			vol.apfs_vol_uuid[15] = k + 1;
			strcpy(reinterpret_cast<char *>(vol.apfs_volname), k ? "System" : "Data");
			vol.apfs_snap_meta_tree_oid = k ? 0 : 8;
			vol.apfs_num_snapshots = k ? 0 : 1;
			checksum(6 + k);
		}
		snapshot(8, true, 42);
		header(9, APFS_KEYBAG_OBJ);
		auto &bag = object<media_keybag_t>(9);
		bag.mk_locker.kl_version = 2;
		bag.mk_locker.kl_nbytes = sizeof(kb_locker_t);
		checksum(9);
	}

	template<typename T> T &object(size_t block, size_t offset = 0)
	{
		return *reinterpret_cast<T *>(bytes.data() + block * block_size + offset);
	}

	void header(size_t block, uint32_t type, uint32_t subtype = 0)
	{
		auto &obj = object<obj_phys_t>(block);
		obj.o_oid = block;
		obj.o_xid = 1;
		obj.o_type = type;
		obj.o_subtype = subtype;
	}

	void checksum(size_t block)
	{
		uint32_t *data = &object<uint32_t>(block);
		const uint64_t sum = Fletcher64(data + 2, block_size / 4 - 2, 0);
		const uint64_t lo = sum & 0xFFFFFFFF;
		const uint64_t hi = sum >> 32;
		data[0] = 0xFFFFFFFF - (lo + hi) % 0xFFFFFFFF;
		data[1] = 0xFFFFFFFF - (lo + data[0]) % 0xFFFFFFFF;
		require(VerifyBlock(data, block_size), "fixture checksum");
	}

	void snapshot(size_t block, bool root, uint64_t xid)
	{
		memset(bytes.data() + block * block_size, 0, block_size);
		header(block, root ? OBJECT_TYPE_BTREE : OBJECT_TYPE_BTREE_NODE, OBJECT_TYPE_SNAPMETATREE);
		auto &node = object<btree_node_phys_t>(block);
		node.btn_flags = BTNODE_LEAF | (root ? BTNODE_ROOT : 0);
		node.btn_nkeys = 1;
		node.btn_table_space.len = sizeof(kvloc_t);
		auto &entry = object<kvloc_t>(block, sizeof(node));
		entry.k.len = sizeof(j_snap_metadata_key_t);
		entry.v.len = sizeof(j_snap_metadata_val_t) + sizeof("test-snapshot");
		entry.v.off = entry.v.len;
		object<j_snap_metadata_key_t>(block, sizeof(node) + sizeof(entry)).hdr.obj_id_and_type =
			APFS_TYPE_ID(APFS_TYPE_SNAP_METADATA, xid);
		const size_t end = block_size - (root ? sizeof(btree_info_t) : 0);
		auto &value = object<j_snap_metadata_val_t>(block, end - entry.v.off);
		value.name_len = sizeof("test-snapshot");
		memcpy(value.name, "test-snapshot", value.name_len);
		if (root) {
			auto &info = object<btree_info_t>(block, end);
			info.bt_fixed.bt_node_size = block_size;
			info.bt_key_count = 1;
			info.bt_node_count = 1;
		}
		checksum(block);
	}

	void branched_snapshots()
	{
		snapshot(10, false, 42);
		snapshot(11, false, 43);
		memset(bytes.data() + 8 * block_size, 0, block_size);
		header(8, OBJECT_TYPE_BTREE, OBJECT_TYPE_SNAPMETATREE);
		auto &node = object<btree_node_phys_t>(8);
		node.btn_flags = BTNODE_ROOT;
		node.btn_level = 1;
		node.btn_nkeys = 2;
		node.btn_table_space.len = 2 * sizeof(kvloc_t);
		for (unsigned k = 0; k < 2; k++) {
			auto &entry = object<kvloc_t>(8, sizeof(node) + k * sizeof(kvloc_t));
			entry.k.off = k * sizeof(j_snap_metadata_key_t);
			entry.k.len = sizeof(j_snap_metadata_key_t);
			entry.v.len = sizeof(oid_t);
			entry.v.off = (k + 1) * sizeof(oid_t);
			object<j_snap_metadata_key_t>(8, sizeof(node) + node.btn_table_space.len + entry.k.off).hdr.obj_id_and_type =
				APFS_TYPE_ID(APFS_TYPE_SNAP_METADATA, (42 + k));
			object<le_oid_t>(8, block_size - sizeof(btree_info_t) - entry.v.off) = 10 + k;
		}
		auto &info = object<btree_info_t>(8, block_size - sizeof(btree_info_t));
		info.bt_fixed.bt_node_size = block_size;
		info.bt_key_count = 2;
		info.bt_node_count = 3;
		checksum(8);
		object<apfs_superblock_t>(6).apfs_num_snapshots = 2;
		checksum(6);
	}

	void write(const std::string &path)
	{
		std::ofstream file(path, std::ios::binary);
		file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
		require(file.good(), "write fixture");
	}

	bool Open(const char *) override { return true; }
	void Close() override {}
	uint64_t GetSize() const override { return bytes.size(); }
	bool Read(void *data, uint64_t offset, uint64_t length) override
	{
		reads.push_back(offset / block_size);
		if (offset > bytes.size() || length > bytes.size() - offset)
			return false;
		memcpy(data, bytes.data() + offset, length);
		return true;
	}
};

int main(int argc, char **argv)
{
	require(argc == 2, "fixture output directory argument");
	const std::string dir = std::string(argv[1]) + "/";
	Fixture key_failure;
	key_failure.bytes[9 * Fixture::block_size] ^= 1;
	key_failure.write(dir + "key-failure.img");
	Fixture valid;
	valid.write(dir + "valid.img");
	Fixture no_keys;
	for (size_t block : {0, 2}) {
		no_keys.object<nx_superblock_t>(block).nx_keylocker = {};
		no_keys.checksum(block);
	}
	no_keys.write(dir + "no-keylocker.img");

	for (size_t block : {0, 1, 3, 4, 5}) {
		Fixture image;
		image.bytes[block * Fixture::block_size] ^= 1;
		ApfsContainer container(&image, 0, image.GetSize());
		require(!container.Init(), "container checksum failure remains fatal");
		image.write(dir + "bad-block-" + std::to_string(block) + ".img");
	}
	Fixture invalid;
	invalid.object<nx_superblock_t>(0).nx_magic = 0;
	invalid.checksum(0);
	invalid.write(dir + "bad-magic.img");
	Fixture volume;
	volume.bytes[6 * Fixture::block_size] ^= 1;
	volume.write(dir + "bad-volume.img");
	Fixture volume_magic;
	volume_magic.object<apfs_superblock_t>(6).apfs_magic = 0;
	volume_magic.checksum(6);
	volume_magic.write(dir + "bad-volume-magic.img");
	Fixture volume_type;
	volume_type.object<apfs_superblock_t>(6).apfs_o.o_type = OBJECT_TYPE_OMAP;
	volume_type.checksum(6);
	volume_type.write(dir + "bad-volume-type.img");
	Fixture volume_name;
	auto &named_volume = volume_name.object<apfs_superblock_t>(6);
	memset(named_volume.apfs_volname, 'x', sizeof(named_volume.apfs_volname));
	named_volume.apfs_next_doc_id = 0x41414141;
	volume_name.checksum(6);
	volume_name.write(dir + "unterminated-volume-name.img");
	Fixture missing_volume;
	missing_volume.object<omap_key_t>(4, sizeof(btree_node_phys_t) + 2 * sizeof(kvoff_t)).ok_oid = 999;
	missing_volume.checksum(4);
	missing_volume.write(dir + "missing-volume.img");
	Fixture snapshots;
	snapshots.bytes[8 * Fixture::block_size] ^= 1;
	snapshots.write(dir + "bad-snapshots.img");
	Fixture missing_snapshots;
	missing_snapshots.object<apfs_superblock_t>(6).apfs_snap_meta_tree_oid = 0;
	missing_snapshots.object<apfs_superblock_t>(6).apfs_num_snapshots = 1;
	missing_snapshots.checksum(6);
	missing_snapshots.write(dir + "missing-snapshots.img");
	Fixture branched;
	branched.branched_snapshots();
	branched.write(dir + "branched.img");
	branched.bytes[11 * Fixture::block_size] ^= 1;
	branched.write(dir + "bad-later-snapshot.img");
	branched.bytes[10 * Fixture::block_size] ^= 1;
	branched.write(dir + "bad-first-snapshot.img");
	Fixture empty;
	empty.object<btree_node_phys_t>(8).btn_nkeys = 0;
	empty.object<btree_info_t>(8, Fixture::block_size - sizeof(btree_info_t)).bt_key_count = 0;
	empty.object<apfs_superblock_t>(6).apfs_num_snapshots = 0;
	empty.checksum(6);
	empty.checksum(8);
	empty.write(dir + "empty-snapshots.img");
	Fixture short_name;
	auto &entry = short_name.object<kvloc_t>(8, sizeof(btree_node_phys_t));
	short_name.object<j_snap_metadata_val_t>(8, Fixture::block_size - sizeof(btree_info_t) - entry.v.off).name_len = 500;
	short_name.checksum(8);
	short_name.write(dir + "bad-snapshot-name.img");
	for (const std::string kind : {"short-key", "short-value", "missing-value", "empty-name", "unterminated-name"}) {
		Fixture malformed;
		auto &record = malformed.object<kvloc_t>(8, sizeof(btree_node_phys_t));
		auto &value = malformed.object<j_snap_metadata_val_t>(8, Fixture::block_size - sizeof(btree_info_t) - record.v.off);
		if (kind == "short-key")
			record.k.len = sizeof(j_snap_metadata_key_t) - 1;
		else if (kind == "short-value")
			record.v.len = sizeof(j_snap_metadata_val_t) - 1;
		else if (kind == "missing-value")
			record.v.off = BTOFF_INVALID;
		else if (kind == "empty-name")
			value.name_len = 0;
		else
			value.name[value.name_len - 1] = 'x';
		malformed.checksum(8);
		malformed.write(dir + "snapshot-" + kind + ".img");
	}
	std::cout << "Inventory tests passed.\n";
	return 0;
}
