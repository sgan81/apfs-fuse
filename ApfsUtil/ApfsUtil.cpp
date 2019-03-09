#include <ApfsLib/ApfsContainer.h>
#include <ApfsLib/Device.h>
#include <ApfsLib/GptPartitionMap.h>

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
		"System", "User", "Recovery", "VM", "Preboot", "Installer", "Data", "Baseband",
		"0x0100", "0x0200", "0x0400", "0x0800", "0x1000", "0x2000", "0x4000", "0x8000"
	};
	bool first = true;
	int k;

	if (role == 0)
		printf("No specific role");
	else {
		for (k = 0; k < 16; k++) {
			if (role & (1 << k)) {
				if (!first)
					printf(", ");
				printf("%s", rolestr[k]);
				first = false;
			}
		}
	}
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
	int volcnt;
	apfs_superblock_t apsb;

	g_debug = 0;

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
			if (partnum > 0) {
				printf("First APFS partition is %d\n", partnum);
				gpt.GetPartitionOffsetAndSize(partnum, offset, size);
			}
			printf("\n");
		}

		container = new ApfsContainer(device, offset, size);

		if (container->Init()) {
			// printf("Listing volumes:\n");
			volcnt = container->GetVolumeCnt();
			for (int k = 0; k < volcnt; k++) {
				container->GetVolumeInfo(k, apsb);
				printf("Volume %d ", k);
				print_apfs_uuid(apsb.apfs_vol_uuid);
				printf("\n");
				printf("---------------------------------------------\n");

				printf("Role:               ");
				print_role(apsb.apfs_role.get());
				printf("\n");
				printf("Name:               %s", apsb.apfs_volname);
				if (apsb.apfs_incompatible_features.get() & APFS_INCOMPAT_CASE_INSENSITIVE)
					printf(" (Case-insensitive)\n");
				else if (apsb.apfs_incompatible_features.get() & APFS_INCOMPAT_NORMALIZATION_INSENSITIVE)
					printf(" (Case-sensitive)\n");
				else
					printf("\n");
				printf("Capacity Consumed:  %" PRIu64 " Bytes\n", apsb.apfs_fs_alloc_count.get() * container->GetBlocksize());
				printf("FileVault:          ");
				print_filevault(apsb.apfs_fs_flags.get() & APFS_FS_CRYPTOFLAGS);
				printf("\n");
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
