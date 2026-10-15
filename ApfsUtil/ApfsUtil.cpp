#include <ApfsLib/ApfsContainer.h>
#include <ApfsLib/Device.h>
#include <ApfsLib/GptPartitionMap.h>

#include <cinttypes>
#include <cstdio>
#include <cstring>

enum ExitStatus { Success = 0, Failure = 1, ArgumentError = 2, PartialInventory = 3 };

static void usage(FILE *out, const char *program)
{
	fprintf(out, "Usage: %s [--metadata-only] [--] <device>\n"
		"List APFS volumes and snapshots.\n"
		"  --metadata-only  Skip the key manager and inventory readable metadata only.\n"
		"                   Accept devices and raw images without decryption or password prompts.\n"
		"  -h, --help       Show this help.\n"
		"Exit status: 0 success, 1 device/container failure, 2 argument error,\n"
		"             3 incomplete volume or snapshot inventory.\n", program);
}

static bool print_snapshots(ApfsContainer &container, const apfs_superblock_t &apsb)
{
	BTree snap_tree(container);
	BTreeIterator it;
	BTreeEntry bte;
	const j_snap_metadata_key_t *sme_k;
	const j_snap_metadata_val_t *sme_v;

	if (!snap_tree.Init(apsb.apfs_snap_meta_tree_oid, apsb.apfs_o.o_xid) || !snap_tree.GetIteratorBegin(it))
		return false;

	for (;;) {
		if (!it.GetEntry(bte)) break;
		if (bte.key_len < sizeof(j_snap_metadata_key_t))
			return false;
		sme_k = reinterpret_cast<const j_snap_metadata_key_t*>(bte.key);
		if ((sme_k->hdr.obj_id_and_type >> OBJ_TYPE_SHIFT) != APFS_TYPE_SNAP_METADATA) break;
		if (!bte.val || bte.val_len < sizeof(j_snap_metadata_val_t))
			return false;
		sme_v = reinterpret_cast<const j_snap_metadata_val_t*>(bte.val);
		if (sme_v->name_len == 0 || sme_v->name_len > bte.val_len - sizeof(j_snap_metadata_val_t) ||
			sme_v->name[sme_v->name_len - 1] != 0)
			return false;
		printf("    %" PRIu64 " : '%s'\n", sme_k->hdr.obj_id_and_type & OBJ_ID_MASK, sme_v->name);
		if (!it.next()) break;
	}
	return !it.HasError();
}

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
	bool metadata_only = false;
	bool options = true;
	bool help = false;
	int status = Success;

	g_debug = 0;

	for (int k = 1; k < argc; k++) {
		if (options && !strcmp(argv[k], "--"))
			options = false;
		else if (options && !strcmp(argv[k], "--metadata-only") && !metadata_only)
			metadata_only = true;
		else if (options && (!strcmp(argv[k], "--help") || !strcmp(argv[k], "-h")))
			help = true;
		else if ((options && argv[k][0] == '-') || devname || argv[k][0] == '\0') {
			fprintf(stderr, "Invalid argument: %s\n", argv[k]);
			usage(stderr, argv[0]);
			return ArgumentError;
		} else
			devname = argv[k];
	}

	if (help) {
		usage(stdout, argv[0]);
		return Success;
	}
	if (!devname) {
		usage(stderr, argv[0]);
		return ArgumentError;
	}

	device = Device::OpenDevice(devname, metadata_only ? Device::ImageHandling::Raw : Device::ImageHandling::ByExtension);

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

		if (container->Init(0, metadata_only ? ApfsContainer::VolumeAccessMode::MetadataOnly : ApfsContainer::VolumeAccessMode::Full)) {
			if (metadata_only)
				printf("Metadata-only inventory: key manager skipped.\n");
			// printf("Listing volumes:\n");
			for (int k = 0; k < NX_MAX_FILE_SYSTEMS; k++) {
				if (!container->HasVolume(k))
					continue;
				if (!container->GetVolumeInfo(k, apsb)) {
					fprintf(stderr, "Volume %d: metadata unavailable or unreadable.\n", k);
					status = PartialInventory;
					continue;
				}
				printf("Volume %d ", k);
				print_apfs_uuid(apsb.apfs_vol_uuid);
				printf("\n");
				printf("---------------------------------------------\n");

				printf("Role:               ");
				print_role(apsb.apfs_role);
				printf("\n");
				printf("Name:               %.*s", static_cast<int>(sizeof(apsb.apfs_volname)), apsb.apfs_volname);
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
				if (apsb.apfs_snap_meta_tree_oid || apsb.apfs_num_snapshots) {
					printf("Snapshots:\n");
					if (!print_snapshots(*container, apsb)) {
						fprintf(stderr, "Volume %d: snapshot metadata unavailable or unreadable.\n", k);
						status = PartialInventory;
					}
				}
				printf("\n");
			}
		} else {
			fprintf(stderr, "Unable to open APFS container\n");
			status = Failure;
		}

		delete container;

		device->Close();
		delete device;
	} else {
		fprintf(stderr, "Error opening device.\n");
		return Failure;
	}


	return status;
}
