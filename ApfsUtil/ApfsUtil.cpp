#include <ApfsLib/ApfsContainer.h>
#include <ApfsLib/Device.h>
#include <ApfsLib/GptPartitionMap.h>
#include <ApfsLib/ApfsVolume.h>

#include <cerrno>
#include <cinttypes>

static void print_apfs_uuid(const apfs_uuid_t &uuid)
{
	printf("%02X%02X%02X%02X-", uuid[0], uuid[1], uuid[2], uuid[3]);
	printf("%02X%02X-", uuid[4], uuid[5]);
	printf("%02X%02X-", uuid[6], uuid[7]);
	printf("%02X%02X-", uuid[8], uuid[9]);
	printf("%02X%02X%02X%02X%02X%02X", uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
}

static void print_role(uint16_t role)
{
	static const char *rolestr[] = {
		"System", "User", "Recovery", "VM", "Preboot", "Installer"
	};
	static const char *rolestr_enum[] = {
		"", "Data", "Baseband", "Update",
		"Xart", "Hardware", "Backup", "Reserved-7",
		"Reserved-8", "Enterprise", "Reserved-10", "Prelogin"
	};
	bool first = true;
	int k;

	if (role == 0)
		printf("No specific role");
	else if (role <= 0x20) {
		for (k = 0; k < 6; k++) {
			if (role & (1 << k)) {
				if (!first)
					printf(", ");
				printf("%s", rolestr[k]);
				first = false;
			}
		}
	} else if (role <= APFS_VOL_ROLE_PRELOGIN) {
		printf("%s", rolestr_enum[role >> APFS_VOLUME_ENUM_SHIFT]);
	}
	// printf(" (%d)", role);
}

static void print_filevault(uint64_t flags)
{
	if (flags == 1)
		printf("No");
	else
		printf("Yes");
}

int main(int argc, char *argv[])
{
	const char *devname = nullptr;
	Device *device = nullptr;
	ApfsContainer *container = nullptr;
	uint64_t offset;
	uint64_t size;
	apfs_superblock_t apsb;

	g_debug = 255;

	if (argc < 2)
	{
		printf("Syntax: %s [device]\n", argv[0]);
		return EINVAL;
	}

	devname = argv[1];

	device = Device::OpenDevice(devname);

	if (device) {
		offset = 0;
		size = device->GetSize();

		GptPartitionMap gpt;
		if (gpt.LoadAndVerify(*device)) {
			printf("Found partitions:\n");
			gpt.ListEntries();

			int partnum = gpt.FindFirstAPFSPartition();
			if (partnum >= 0) {
				printf("First APFS partition is %d\n", partnum);
				gpt.GetPartitionOffsetAndSize(partnum, offset, size);
			}
			printf("\n");
		}

		container = new ApfsContainer(device, offset, size);

		if (container->Init()) {
			// printf("Listing volumes:\n");
			for (int k = 0; k < NX_MAX_FILE_SYSTEMS; k++) {
				if (!container->GetVolumeInfo(k, apsb))
					continue;
				printf("Volume %d ", k);
				print_apfs_uuid(apsb.apfs_vol_uuid);
				printf("\n");
				printf("---------------------------------------------\n");

				printf("Role:               ");
				print_role(apsb.apfs_role);
				printf("\n");
				printf("Name:               %s", apsb.apfs_volname);
				if (apsb.apfs_incompatible_features & APFS_INCOMPAT_CASE_INSENSITIVE)
					printf(" (Case-insensitive)\n");
				else if (apsb.apfs_incompatible_features & APFS_INCOMPAT_NORMALIZATION_INSENSITIVE)
					printf(" (Case-sensitive)\n");
				else
					printf("\n");
				printf("Capacity Consumed:  %" PRIu64 " Bytes\n", apsb.apfs_fs_alloc_count * container->GetBlocksize());
				printf("FileVault:          ");
				print_filevault(apsb.apfs_fs_flags & APFS_FS_CRYPTOFLAGS);
				printf("\n");
				if (apsb.apfs_snap_meta_tree_oid) {
					printf("Snapshots:\n");
					BTree snap_tree(*container);
					BTreeIterator it;
					int err;
					union {
						uint8_t buf[JOBJ_MAX_KEY_SIZE];
						j_snap_metadata_key_t k;
					} smk;
					union {
						uint8_t buf[JOBJ_MAX_VALUE_SIZE];
						j_snap_metadata_val_t v;
					} smv;

					snap_tree.Init(apsb.apfs_snap_meta_tree_oid, apsb.apfs_o.o_xid, CompareSnapMetaKey, nullptr);
					err = it.initFirst(&snap_tree, smk.buf, JOBJ_MAX_KEY_SIZE, smv.buf, JOBJ_MAX_VALUE_SIZE);
					if (err == 0) {
						for (;;) {
							if ((smk.k.hdr.obj_id_and_type >> OBJ_TYPE_SHIFT) != APFS_TYPE_SNAP_METADATA) break;
							printf("    %" PRIu64 " : '%s'\n", smk.k.hdr.obj_id_and_type & OBJ_ID_MASK, smv.v.name);
							if (!it.next()) break;
						}
					}
				}
				printf("\n");
			}
		} else {
			printf("Unable to open APFS container\n");
		}

		delete container;

		device->Close();
		delete device;
	} else {
		printf("Error opening device.\n");
		return EIO;
	}

	return 0;
}
