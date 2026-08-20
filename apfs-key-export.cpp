/*
 * apfs-key-export.cpp — derive an APFS volume key for a private pipe.
 *
 * The password is read from stdin and the 32-byte XTS volume encryption key
 * is written as raw bytes to stdout.  Human-readable diagnostics go only to
 * stderr so the output can be connected directly to a recovery process.
 */

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "ApfsLib/ApfsContainer.h"
#include "ApfsLib/Device.h"

static void secure_clear(void *ptr, size_t len)
{
    volatile unsigned char *p = static_cast<volatile unsigned char *>(ptr);
    while (len-- > 0)
        *p++ = 0;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::cerr << "usage: apfs-key-export DEVICE VOLUME_INDEX\n";
        return EINVAL;
    }

    char *index_end = nullptr;
    unsigned long index = std::strtoul(argv[2], &index_end, 10);
    if (!index_end || *index_end != '\0' || index >= NX_MAX_FILE_SYSTEMS) {
        std::cerr << "invalid APFS volume index\n";
        return EINVAL;
    }

    std::string password;
    if (!std::getline(std::cin, password) || password.empty()) {
        std::cerr << "could not read a password from stdin\n";
        return EINVAL;
    }
    if (!password.empty() && password.back() == '\r')
        password.pop_back();

    Device *device = Device::OpenDevice(argv[1]);
    if (!device) {
        secure_clear(&password[0], password.size());
        std::cerr << "could not open APFS device\n";
        return EIO;
    }

    int result = EIO;
    uint8_t vek[32] = {0};
    ApfsContainer container(device, 0, device->GetSize());
    apfs_superblock_t volume_info = {};

    if (!container.Init()) {
        std::cerr << "could not initialize APFS container\n";
    } else if (!container.GetVolumeInfo(static_cast<unsigned int>(index),
                                         volume_info)) {
        std::cerr << "could not resolve APFS volume index\n";
    } else if (!container.GetVolumeKey(vek, volume_info.apfs_vol_uuid,
                                        password.c_str())) {
        std::cerr << "password did not unlock the selected APFS volume\n";
        result = EACCES;
    } else {
        std::cout.write(reinterpret_cast<const char *>(vek), sizeof(vek));
        std::cout.flush();
        result = std::cout.good() ? 0 : EIO;
    }

    secure_clear(vek, sizeof(vek));
    if (!password.empty())
        secure_clear(&password[0], password.size());
    device->Close();
    delete device;
    return result;
}
