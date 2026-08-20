/*
 * apfs-vek-from-pid.cpp — locate a raw APFS XTS volume key retained by an
 * authorized recovery process.  No process bytes are printed or persisted;
 * only a VEK that decrypts and checksum-validates a known filesystem-tree
 * root is emitted to stdout.
 */

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

#include "ApfsLib/ApfsContainer.h"
#include "ApfsLib/ApfsNodeMapperBTree.h"
#include "ApfsLib/Device.h"
#include "ApfsLib/Util.h"
#include "Crypto/AesXts.h"

struct Region {
    uint64_t start;
    uint64_t end;
    int priority;
};

static void secure_clear(void *ptr, size_t len)
{
    volatile unsigned char *p = static_cast<volatile unsigned char *>(ptr);
    while (len-- > 0)
        *p++ = 0;
}

static bool load_regions(pid_t pid, std::vector<Region> &regions)
{
    std::ifstream maps("/proc/" + std::to_string(pid) + "/maps");
    if (!maps) return false;

    std::string line;
    while (std::getline(maps, line)) {
        unsigned long long start = 0, end = 0;
        char perms[5] = {0};
        int consumed = 0;
        if (std::sscanf(line.c_str(), "%llx-%llx %4s %*s %*s %*s %n",
                        &start, &end, perms, &consumed) < 3)
            continue;
        if (perms[0] != 'r' || perms[1] != 'w' || end <= start)
            continue;

        std::string name = (consumed > 0 && static_cast<size_t>(consumed) < line.size())
                         ? line.substr(static_cast<size_t>(consumed)) : std::string();
        while (!name.empty() && name.front() == ' ') name.erase(name.begin());
        if (!name.empty() && name.front() != '[' &&
            name.find("ufs-explorer") == std::string::npos)
            continue;

        int priority = 2;
        if (name.find("[heap]") != std::string::npos) priority = 0;
        else if (name.find("ufs-explorer") != std::string::npos) priority = 0;
        else if ((end - start) <= 16ULL * 1024ULL * 1024ULL) priority = 1;
        regions.push_back({static_cast<uint64_t>(start),
                           static_cast<uint64_t>(end), priority});
    }
    std::stable_sort(regions.begin(), regions.end(),
                     [](const Region &a, const Region &b) {
                         return a.priority < b.priority;
                     });
    return !regions.empty();
}

static bool key_looks_possible(const uint8_t *key)
{
    bool seen[256] = {false};
    int distinct = 0;
    for (size_t i = 0; i < 32; i++) {
        if (!seen[key[i]]) {
            seen[key[i]] = true;
            distinct++;
        }
    }
    return distinct >= 18 && std::memcmp(key, key + 16, 16) != 0;
}

static bool validate_vek(const uint8_t *key,
                         const uint8_t *ciphertext,
                         uint32_t block_size,
                         uint64_t paddr,
                         std::vector<uint8_t> &plaintext)
{
    AesXts xts;
    xts.SetKey(key, key + 16);
    const uint64_t sectors_per_block = block_size / 512;
    const uint64_t first_sector = paddr * sectors_per_block;

    uint8_t prefix[64];
    xts.Decrypt(prefix, ciphertext, sizeof(prefix), first_sector);
    uint32_t object_type = 0;
    std::memcpy(&object_type, prefix + 24, sizeof(object_type));
    if ((object_type & OBJECT_TYPE_MASK) != OBJECT_TYPE_BTREE_NODE)
        return false;

    uint64_t oid = 0, xid = 0;
    std::memcpy(&oid, prefix + 8, sizeof(oid));
    std::memcpy(&xid, prefix + 16, sizeof(xid));
    if (oid == 0 || xid == 0 || xid > (1ULL << 56))
        return false;

    plaintext.resize(block_size);
    for (uint32_t off = 0; off < block_size; off += 512)
        xts.Decrypt(plaintext.data() + off, ciphertext + off, 512,
                    first_sector + off / 512);
    return VerifyBlock(plaintext.data(), plaintext.size());
}

int main(int argc, char **argv)
{
    if (argc < 4 || argc > 5) {
        std::cerr << "usage: apfs-vek-from-pid PID_OR_SESSION_FILE DEVICE VOLUME_INDEX [MAX_MIB]\n";
        return EINVAL;
    }
    const bool file_mode = argv[1][0] == '/';
    pid_t pid = file_mode ? 0 : static_cast<pid_t>(std::strtol(argv[1], nullptr, 10));
    unsigned long volume_index = std::strtoul(argv[3], nullptr, 10);
    uint64_t max_bytes = (argc == 5 ? std::strtoull(argv[4], nullptr, 10) : 256)
                       * 1024ULL * 1024ULL;
    if ((!file_mode && pid <= 0) || volume_index >= NX_MAX_FILE_SYSTEMS ||
        max_bytes == 0)
        return EINVAL;

    Device *device = Device::OpenDevice(argv[2]);
    if (!device) return EIO;
    ApfsContainer container(device, 0, device->GetSize());
    apfs_superblock_t apsb = {};
    omap_res_t root = {};
    ApfsNodeMapperBTree mapper(container);
    int result = EIO;

    if (!container.Init() ||
        !container.GetVolumeInfo(static_cast<unsigned int>(volume_index), apsb) ||
        !mapper.Init(apsb.apfs_omap_oid, apsb.apfs_o.o_xid) ||
        !mapper.Lookup(root, apsb.apfs_root_tree_oid, apsb.apfs_o.o_xid) ||
        !(root.flags & OMAP_VAL_ENCRYPTED)) {
        std::cerr << "could not resolve an encrypted filesystem-tree root\n";
        goto cleanup;
    }

    {
        const uint32_t block_size = container.GetBlocksize();
        std::vector<uint8_t> ciphertext(block_size);
        std::vector<uint8_t> plaintext;
        if (!container.ReadBlocks(ciphertext.data(), root.paddr, 1)) {
            std::cerr << "could not read encrypted validation block\n";
            goto cleanup;
        }

        std::vector<Region> regions;
        std::string source_path = file_mode
            ? std::string(argv[1])
            : "/proc/" + std::to_string(pid) + "/mem";
        int mem_fd = open(source_path.c_str(), O_RDONLY | O_CLOEXEC);
        if (mem_fd < 0) {
            std::cerr << "could not open memory source: " << std::strerror(errno) << "\n";
            result = EACCES;
            goto cleanup;
        }
        if (file_mode) {
            off_t end = lseek(mem_fd, 0, SEEK_END);
            if (end <= 0) {
                close(mem_fd);
                std::cerr << "session file is empty or not seekable\n";
                goto cleanup;
            }
            regions.push_back({0, static_cast<uint64_t>(end), 0});
        } else if (!load_regions(pid, regions)) {
            close(mem_fd);
            std::cerr << "could not enumerate process mappings\n";
            result = EACCES;
            goto cleanup;
        }

        const size_t chunk_size = 4 * 1024 * 1024;
        const size_t overlap = 32;
        std::vector<uint8_t> buffer(chunk_size);
        uint64_t scanned = 0;
        uint64_t tested = 0;
        bool found = false;

        for (const Region &region : regions) {
            if (scanned >= max_bytes || found) break;
            for (uint64_t pos = region.start;
                 pos < region.end && scanned < max_bytes && !found;) {
                size_t want = static_cast<size_t>(std::min<uint64_t>(
                    chunk_size, std::min<uint64_t>(region.end - pos,
                                                   max_bytes - scanned)));
                ssize_t got = pread(mem_fd, buffer.data(), want,
                                    static_cast<off_t>(pos));
                if (got > 32) {
                    for (size_t off = 0; off + 32 <= static_cast<size_t>(got);
                         off++) {
                        const uint8_t *candidate = buffer.data() + off;
                        if (!key_looks_possible(candidate)) continue;
                        tested++;
                        if (validate_vek(candidate, ciphertext.data(), block_size,
                                         root.paddr, plaintext)) {
                            std::cerr << "validated a raw VEK from process memory\n";
                            std::cout.write(reinterpret_cast<const char *>(candidate), 32);
                            std::cout.flush();
                            found = std::cout.good();
                            result = found ? 0 : EIO;
                            break;
                        }
                    }
                }
                scanned += want;
                pos += want > overlap ? want - overlap : want;
                if ((scanned / (64 * 1024 * 1024)) !=
                    ((scanned - want) / (64 * 1024 * 1024)))
                    std::cerr << "scanned " << (scanned / (1024 * 1024))
                              << " MiB of private memory\n";
            }
        }
        close(mem_fd);
        secure_clear(buffer.data(), buffer.size());
        secure_clear(plaintext.data(), plaintext.size());
        if (!found) {
            std::cerr << "no raw VEK matched after testing " << tested
                      << " aligned candidates\n";
            result = EACCES;
        }
    }

cleanup:
    device->Close();
    delete device;
    return result;
}
