#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>

#include "ApfsLib/Device.h"
#include "ApfsLib/DiskStruct.h"
#include "ApfsLib/Util.h"

struct FoundXid {
    uint64_t xid;
    uint64_t paddr;
    uint32_t desc_index;
    uint32_t desc_len;
};

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "usage: apfs-xid-list DEVICE\n";
        return 2;
    }

    std::unique_ptr<Device> dev(Device::OpenDevice(argv[1]));
    if (!dev) {
        std::cerr << "cannot open device\n";
        return 1;
    }

    std::vector<uint8_t> block(4096);
    if (!dev->Read(block.data(), 0, block.size())) {
        std::cerr << "cannot read container block zero\n";
        return 1;
    }

    const auto *nx0 = reinterpret_cast<const nx_superblock_t *>(block.data());
    if (nx0->nx_magic != NX_MAGIC || nx0->nx_block_size < 4096 ||
        nx0->nx_block_size > 65536) {
        std::cerr << "not an APFS container at offset zero\n";
        return 1;
    }

    const uint32_t block_size = nx0->nx_block_size;
    block.resize(block_size);
    if (!dev->Read(block.data(), 0, block.size()) ||
        !VerifyBlock(block.data(), block.size())) {
        std::cerr << "invalid APFS container superblock\n";
        return 1;
    }

    nx_superblock_t current{};
    std::memcpy(&current, block.data(), sizeof(current));
    std::vector<FoundXid> found;

    const uint64_t first = current.nx_xp_desc_base;
    const uint64_t count = current.nx_xp_desc_blocks;
    for (uint64_t paddr = first; paddr < first + count; ++paddr) {
        if (!dev->Read(block.data(), paddr * block_size, block.size()) ||
            !VerifyBlock(block.data(), block.size())) {
            continue;
        }
        const auto *nx = reinterpret_cast<const nx_superblock_t *>(block.data());
        if ((nx->nx_o.o_type & OBJECT_TYPE_MASK) != OBJECT_TYPE_NX_SUPERBLOCK ||
            nx->nx_magic != NX_MAGIC) {
            continue;
        }
        found.push_back({static_cast<uint64_t>(nx->nx_o.o_xid), paddr,
                         static_cast<uint32_t>(nx->nx_xp_desc_index),
                         static_cast<uint32_t>(nx->nx_xp_desc_len)});
    }

    std::sort(found.begin(), found.end(),
              [](const FoundXid &a, const FoundXid &b) { return a.xid < b.xid; });
    found.erase(std::unique(found.begin(), found.end(),
                            [](const FoundXid &a, const FoundXid &b) {
                                return a.xid == b.xid;
                            }),
                found.end());

    std::cout << "block_size=" << block_size << " descriptor_base=" << first
              << " descriptor_blocks=" << count << " valid_superblocks="
              << found.size() << "\n";
    for (const auto &entry : found) {
        std::cout << "xid=" << entry.xid << " paddr=" << entry.paddr
                  << " desc_index=" << entry.desc_index
                  << " desc_len=" << entry.desc_len << "\n";
    }
    return found.empty() ? 1 : 0;
}
