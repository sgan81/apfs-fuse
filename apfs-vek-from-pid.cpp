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

struct ScheduleHit {
    uint64_t address;
    uint8_t key[16];
};

static void secure_clear(void *ptr, size_t len);

static const uint8_t aes_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static void expand_aes128(const uint8_t key[16], uint8_t expanded[176])
{
    static const uint8_t rcon[10] =
        {0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};
    std::memcpy(expanded, key, 16);
    size_t used = 16;
    size_t round = 0;
    uint8_t temp[4];
    while (used < 176) {
        std::memcpy(temp, expanded + used - 4, 4);
        if ((used % 16) == 0) {
            uint8_t first = temp[0];
            temp[0] = aes_sbox[temp[1]] ^ rcon[round++];
            temp[1] = aes_sbox[temp[2]];
            temp[2] = aes_sbox[temp[3]];
            temp[3] = aes_sbox[first];
        }
        for (size_t i = 0; i < 4; i++) {
            expanded[used] = expanded[used - 16] ^ temp[i];
            used++;
        }
    }
}

static uint8_t gf_mul(uint8_t a, uint8_t b)
{
    uint8_t result = 0;
    while (b) {
        if (b & 1) result ^= a;
        a = static_cast<uint8_t>((a << 1) ^ ((a & 0x80) ? 0x1b : 0));
        b >>= 1;
    }
    return result;
}

static void inv_mix_column(const uint8_t in[4], uint8_t out[4])
{
    static bool initialized = false;
    static uint8_t m9[256], m11[256], m13[256], m14[256];
    if (!initialized) {
        for (int i = 0; i < 256; i++) {
            m9[i] = gf_mul(static_cast<uint8_t>(i), 9);
            m11[i] = gf_mul(static_cast<uint8_t>(i), 11);
            m13[i] = gf_mul(static_cast<uint8_t>(i), 13);
            m14[i] = gf_mul(static_cast<uint8_t>(i), 14);
        }
        initialized = true;
    }
    out[0] = m14[in[0]] ^ m11[in[1]] ^ m13[in[2]] ^ m9[in[3]];
    out[1] = m9[in[0]] ^ m14[in[1]] ^ m11[in[2]] ^ m13[in[3]];
    out[2] = m13[in[0]] ^ m9[in[1]] ^ m14[in[2]] ^ m11[in[3]];
    out[3] = m11[in[0]] ^ m13[in[1]] ^ m9[in[2]] ^ m14[in[3]];
}

static bool detect_aes128_decrypt_schedule(const uint8_t *p, size_t available,
                                           bool word_swapped, uint8_t key[16])
{
    if (available < 176) return false;
    for (size_t i = 0; i < 16; i++) {
        size_t stored = 160 + (word_swapped
            ? (i & ~3U) + (3U - (i & 3U)) : i);
        key[i] = p[stored];
    }

    uint8_t enc_round1_col[4] = {
        static_cast<uint8_t>(key[0] ^ aes_sbox[key[13]] ^ 0x01),
        static_cast<uint8_t>(key[1] ^ aes_sbox[key[14]]),
        static_cast<uint8_t>(key[2] ^ aes_sbox[key[15]]),
        static_cast<uint8_t>(key[3] ^ aes_sbox[key[12]])
    };
    uint8_t dec_round9_col[4];
    inv_mix_column(enc_round1_col, dec_round9_col);
    for (size_t i = 0; i < 4; i++) {
        size_t stored = 144 + (word_swapped ? 3 - i : i);
        if (p[stored] != dec_round9_col[i]) return false;
    }

    uint8_t expanded[176];
    uint8_t decrypt[176];
    expand_aes128(key, expanded);
    std::memcpy(decrypt, expanded + 160, 16);
    for (size_t round = 1; round < 10; round++) {
        const uint8_t *enc = expanded + (10 - round) * 16;
        uint8_t *dec = decrypt + round * 16;
        for (size_t col = 0; col < 4; col++)
            inv_mix_column(enc + col * 4, dec + col * 4);
    }
    std::memcpy(decrypt + 160, expanded, 16);

    bool match = true;
    for (size_t i = 0; i < sizeof(decrypt); i++) {
        size_t stored = word_swapped ? (i & ~3U) + (3U - (i & 3U)) : i;
        if (p[stored] != decrypt[i]) {
            match = false;
            break;
        }
    }
    secure_clear(expanded, sizeof(expanded));
    secure_clear(decrypt, sizeof(decrypt));
    return match;
}

static bool detect_aes128_schedule(const uint8_t *p, size_t available,
                                   bool word_swapped, uint8_t key[16])
{
    if (available < 176) return false;
    for (size_t i = 0; i < 16; i++)
        key[i] = word_swapped ? p[(i & ~3U) + (3U - (i & 3U))] : p[i];

    uint8_t first_word[4] = {
        static_cast<uint8_t>(key[0] ^ aes_sbox[key[13]] ^ 0x01),
        static_cast<uint8_t>(key[1] ^ aes_sbox[key[14]]),
        static_cast<uint8_t>(key[2] ^ aes_sbox[key[15]]),
        static_cast<uint8_t>(key[3] ^ aes_sbox[key[12]])
    };
    for (size_t i = 0; i < 4; i++) {
        uint8_t observed = word_swapped ? p[16 + (3 - i)] : p[16 + i];
        if (observed != first_word[i]) return false;
    }

    uint8_t expanded[176];
    expand_aes128(key, expanded);
    for (size_t i = 0; i < sizeof(expanded); i++) {
        size_t stored = word_swapped ? (i & ~3U) + (3U - (i & 3U)) : i;
        if (p[stored] != expanded[i]) {
            secure_clear(expanded, sizeof(expanded));
            return false;
        }
    }
    secure_clear(expanded, sizeof(expanded));
    return true;
}

static void collect_schedules(const uint8_t *data, size_t len, uint64_t address,
                              std::vector<ScheduleHit> &hits)
{
    size_t first = static_cast<size_t>((4 - (address & 3)) & 3);
    for (size_t off = first; off + 176 <= len; off += 4) {
        uint8_t key[16];
        for (int swapped = 0; swapped < 2; swapped++) {
            for (int decrypt = 0; decrypt < 2; decrypt++) {
                bool matched = decrypt
                    ? detect_aes128_decrypt_schedule(data + off, len - off,
                                                     swapped != 0, key)
                    : detect_aes128_schedule(data + off, len - off,
                                             swapped != 0, key);
                if (!matched) continue;
                uint64_t hit_address = address + off;
                bool duplicate = false;
                for (const ScheduleHit &hit : hits)
                    if (hit.address == hit_address &&
                        std::memcmp(hit.key, key, 16) == 0)
                        duplicate = true;
                if (!duplicate) {
                    ScheduleHit hit = {hit_address, {0}};
                    std::memcpy(hit.key, key, 16);
                    hits.push_back(hit);
                }
            }
        }
        secure_clear(key, sizeof(key));
    }
}

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
        const size_t overlap = 192;
        std::vector<uint8_t> buffer(chunk_size);
        std::vector<ScheduleHit> schedule_hits;
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
                    collect_schedules(buffer.data(), static_cast<size_t>(got),
                                      pos, schedule_hits);
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

        if (!found && schedule_hits.size() >= 2) {
            std::cerr << "found " << schedule_hits.size()
                      << " AES-128 encryption schedules; testing XTS pairs\n";
            uint8_t candidate[32];

            /* Adjacent cipher contexts are by far the most likely layout. */
            for (int pass = 0; pass < 2 && !found; pass++) {
                for (size_t i = 0; i < schedule_hits.size() && !found; i++) {
                    for (size_t j = 0; j < schedule_hits.size() && !found; j++) {
                        if (i == j) continue;
                        uint64_t distance = schedule_hits[i].address > schedule_hits[j].address
                            ? schedule_hits[i].address - schedule_hits[j].address
                            : schedule_hits[j].address - schedule_hits[i].address;
                        if ((pass == 0 && distance > 8192) ||
                            (pass == 1 && distance <= 8192))
                            continue;
                        std::memcpy(candidate, schedule_hits[i].key, 16);
                        std::memcpy(candidate + 16, schedule_hits[j].key, 16);
                        if (validate_vek(candidate, ciphertext.data(), block_size,
                                         root.paddr, plaintext)) {
                            std::cerr << "validated VEK reconstructed from AES schedules\n";
                            std::cout.write(reinterpret_cast<const char *>(candidate), 32);
                            std::cout.flush();
                            found = std::cout.good();
                            result = found ? 0 : EIO;
                        }
                    }
                }
            }
            secure_clear(candidate, sizeof(candidate));
        } else if (!found) {
            std::cerr << "found " << schedule_hits.size()
                      << " AES-128 encryption schedules\n";
        }

        for (ScheduleHit &hit : schedule_hits)
            secure_clear(hit.key, sizeof(hit.key));
        secure_clear(buffer.data(), buffer.size());
        secure_clear(plaintext.data(), plaintext.size());
        if (!found) {
            std::cerr << "no VEK matched after testing " << tested
                      << " raw candidates and all schedule pairs\n";
            result = EACCES;
        }
    }

cleanup:
    device->Close();
    delete device;
    return result;
}
