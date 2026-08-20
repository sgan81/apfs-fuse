/*
 * apfs-key-from-pid.cpp — recover an APFS passphrase left in an authorized
 * recovery process without ever printing or persisting the passphrase.
 *
 * Readable strings from private writable mappings are tested in memory using
 * apfs-fuse's keybag implementation.  On success only the raw 32-byte VEK is
 * emitted to stdout, suitable for a private pipe into another recovery tool.
 */

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_set>
#include <unistd.h>
#include <vector>

#include "ApfsLib/ApfsContainer.h"
#include "ApfsLib/Device.h"

struct Region {
    uint64_t start;
    uint64_t end;
};

struct Candidate {
    std::string text;
    int score;
};

static void secure_clear(void *ptr, size_t len)
{
    volatile unsigned char *p = static_cast<volatile unsigned char *>(ptr);
    while (len-- > 0)
        *p++ = 0;
}

static int candidate_score(const std::string &s)
{
    if (s.size() < 8 || s.size() > 128)
        return -1;

    int letters = 0, digits = 0, spaces = 0, punctuation = 0;
    for (unsigned char c : s) {
        if (std::isalpha(c)) letters++;
        else if (std::isdigit(c)) digits++;
        else if (c == ' ') spaces++;
        else punctuation++;
    }
    if (letters < 4)
        return -1;

    int score = 0;
    if (s.size() >= 20) score += 60;
    if (s.size() >= 32) score += 40;
    score += std::min(spaces, 10) * 18;
    score += std::min(digits, 8) * 3;
    score += std::min(punctuation, 8) * 2;
    score -= static_cast<int>(std::abs(static_cast<long>(s.size()) - 63L) / 3);

    /* Program messages and paths are common in process memory; keep them as
     * fallback candidates but try passphrase-shaped strings first. */
    if (s.find('/') != std::string::npos) score -= 35;
    if (s.find("%s") != std::string::npos ||
        s.find("%d") != std::string::npos) score -= 50;
    if (s.find("http") != std::string::npos) score -= 50;
    return score;
}

static void add_candidate(std::unordered_set<std::string> &seen,
                          std::vector<Candidate> &out,
                          const std::string &raw)
{
    size_t first = 0;
    size_t last = raw.size();
    while (first < last && raw[first] == ' ') first++;
    while (last > first && raw[last - 1] == ' ') last--;

    const std::string variants[2] = { raw, raw.substr(first, last - first) };
    for (const std::string &s : variants) {
        int score = candidate_score(s);
        if (score >= 0 && seen.insert(s).second)
            out.push_back({s, score});
    }
}

static void collect_ascii(const uint8_t *data, size_t len,
                          std::unordered_set<std::string> &seen,
                          std::vector<Candidate> &out)
{
    std::string current;
    current.reserve(129);
    bool overflow = false;
    for (size_t i = 0; i <= len; i++) {
        int c = (i < len) ? data[i] : 0;
        if (c >= 0x20 && c <= 0x7e) {
            if (current.size() < 128) current.push_back(static_cast<char>(c));
            else overflow = true;
        } else {
            if (!overflow) add_candidate(seen, out, current);
            current.clear();
            overflow = false;
        }
    }
}

static void collect_utf16_ascii(const uint8_t *data, size_t len,
                                std::unordered_set<std::string> &seen,
                                std::vector<Candidate> &out)
{
    for (size_t parity = 0; parity < 2; parity++) {
        std::string current;
        current.reserve(129);
        bool overflow = false;
        for (size_t i = parity; i + 1 < len; i += 2) {
            uint8_t lo = data[i], hi = data[i + 1];
            if (hi == 0 && lo >= 0x20 && lo <= 0x7e) {
                if (current.size() < 128) current.push_back(static_cast<char>(lo));
                else overflow = true;
            } else {
                if (!overflow) add_candidate(seen, out, current);
                current.clear();
                overflow = false;
            }
        }
        if (!overflow) add_candidate(seen, out, current);
    }
}

static bool load_regions(pid_t pid, std::vector<Region> &regions)
{
    std::string path = "/proc/" + std::to_string(pid) + "/maps";
    std::ifstream maps(path);
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
        regions.push_back({static_cast<uint64_t>(start),
                           static_cast<uint64_t>(end)});
    }
    return !regions.empty();
}

int main(int argc, char **argv)
{
    if (argc < 4 || argc > 5) {
        std::cerr << "usage: apfs-key-from-pid PID DEVICE VOLUME_INDEX [MAX_CANDIDATES]\n";
        return EINVAL;
    }

    pid_t pid = static_cast<pid_t>(std::strtol(argv[1], nullptr, 10));
    unsigned long volume_index = std::strtoul(argv[3], nullptr, 10);
    size_t max_candidates = (argc == 5)
                          ? static_cast<size_t>(std::strtoull(argv[4], nullptr, 10))
                          : 5000;
    if (pid <= 0 || volume_index >= NX_MAX_FILE_SYSTEMS || max_candidates == 0)
        return EINVAL;

    std::vector<Region> regions;
    if (!load_regions(pid, regions)) {
        std::cerr << "could not enumerate readable process mappings\n";
        return EACCES;
    }

    std::string mem_path = "/proc/" + std::to_string(pid) + "/mem";
    int mem_fd = open(mem_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (mem_fd < 0) {
        std::cerr << "could not open process memory: " << std::strerror(errno) << "\n";
        return EACCES;
    }

    std::unordered_set<std::string> seen;
    std::vector<Candidate> candidates;
    const size_t chunk_size = 4 * 1024 * 1024;
    const size_t overlap = 256;
    std::vector<uint8_t> buffer(chunk_size + overlap);

    for (const Region &region : regions) {
        for (uint64_t pos = region.start; pos < region.end;) {
            size_t want = static_cast<size_t>(
                std::min<uint64_t>(chunk_size, region.end - pos));
            ssize_t got = pread(mem_fd, buffer.data(), want,
                                static_cast<off_t>(pos));
            if (got > 0) {
                collect_ascii(buffer.data(), static_cast<size_t>(got), seen, candidates);
                collect_utf16_ascii(buffer.data(), static_cast<size_t>(got), seen, candidates);
            }
            pos += want > overlap ? want - overlap : want;
        }
    }
    close(mem_fd);

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate &a, const Candidate &b) {
                  if (a.score != b.score) return a.score > b.score;
                  return a.text.size() > b.text.size();
              });
    if (candidates.size() > max_candidates)
        candidates.resize(max_candidates);
    std::cerr << "testing " << candidates.size()
              << " in-memory passphrase candidates\n";

    Device *device = Device::OpenDevice(argv[2]);
    if (!device) return EIO;
    ApfsContainer container(device, 0, device->GetSize());
    apfs_superblock_t volume_info = {};
    uint8_t vek[32] = {0};
    int result = EACCES;

    if (!container.Init() ||
        !container.GetVolumeInfo(static_cast<unsigned int>(volume_index), volume_info)) {
        std::cerr << "could not initialize the selected APFS volume\n";
        result = EIO;
    } else {
        for (size_t i = 0; i < candidates.size(); i++) {
            if (container.GetVolumeKey(vek, volume_info.apfs_vol_uuid,
                                       candidates[i].text.c_str())) {
                std::cerr << "matched an in-memory passphrase candidate\n";
                std::cout.write(reinterpret_cast<const char *>(vek), sizeof(vek));
                std::cout.flush();
                result = std::cout.good() ? 0 : EIO;
                break;
            }
            if ((i + 1) % 250 == 0)
                std::cerr << "tested " << (i + 1) << " candidates\n";
        }
    }

    secure_clear(vek, sizeof(vek));
    for (Candidate &candidate : candidates) {
        if (!candidate.text.empty())
            secure_clear(&candidate.text[0], candidate.text.size());
    }
    device->Close();
    delete device;
    return result;
}
