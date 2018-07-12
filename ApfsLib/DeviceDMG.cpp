/*
This file is part of apfs-fuse, a read-only implementation of APFS
(Apple File System) for FUSE.
Copyright (C) 2017 Simon Gander

Apfs-fuse is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

Apfs-fuse is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with apfs-fuse.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <cstring>
#include <vector>
#include <iostream>
#include <iomanip>

#include "DeviceDMG.h"
#include "Util.h"
#include "PList.h"
#include "TripleDes.h"
#include "Crypto.h"

#pragma pack(push, 1)

struct KolyHeader
{
	char         signature[4];
	be<uint32_t> version;
	be<uint32_t> headersize;
	be<uint32_t> flags;
	be<uint64_t> running_data_fork_offset;
	be<uint64_t> data_fork_offset;
	be<uint64_t> data_fork_length;
	be<uint64_t> rsrc_fork_offset;
	be<uint64_t> rsrc_fork_length;
	be<uint32_t> segment_number;
	be<uint32_t> segment_count;
	uint8_t      segment_id[0x10];
	// 0x50
	be<uint32_t> data_fork_checksum_type;
	be<uint32_t> data_fork_unknown;
	be<uint32_t> data_fork_checksum_data;
	uint8_t      unk_05C[0x7C];
	// 0xD8
	be<uint64_t> xml_offset;
	be<uint64_t> xml_length;
	// ???
	// uint64_t codesign_offset;
	// uint64_t codesign_length;
	uint8_t      unk_0E8[0x78];
	// 0x160
	be<uint32_t> master_checksum_type;
	be<uint32_t> master_checksum_unknown;
	be<uint32_t> master_checksum_data;
	uint8_t      unk_16C[0x7C];
	// 0x1E8
	be<uint32_t> image_variant;
	be<uint64_t> sector_count;
	// 0x1F4
	uint8_t      unk_1F4[12];
};

static_assert(sizeof(KolyHeader) == 0x200, "Wrong Koly Header Size");

struct MishHeader
{
	char         signature[4];
	be<uint32_t> unk;
	be<uint64_t> sector_start;
	be<uint64_t> sector_count;
	be<uint64_t> unk_18;
	be<uint32_t> unk_20;
	be<uint32_t> part_id;
	be<uint8_t>  unk_28[0x18];
	be<uint32_t> checksum_type;
	be<uint32_t> checksum_unk;
	be<uint32_t> checksum_data;
	be<uint8_t>  unk_4C[0x7C];
	be<uint32_t> entry_count;
};

static_assert(sizeof(MishHeader) == 0xCC, "Wrong Mish Header Size");

struct MishEntry
{
	be<uint32_t> method;
	be<uint32_t> comment;
	be<uint64_t> sector_start;
	be<uint64_t> sector_count;
	be<uint64_t> dmg_offset;
	be<uint64_t> dmg_length;
};

static_assert(sizeof(MishEntry) == 0x28, "Wrong Mish Entry Size");

struct DmgCryptHeaderV1
{
	uint8_t uuid[0x10];
	be<uint32_t> block_size;
	be<uint32_t> unk_14;
	be<uint32_t> unk_18;
	be<uint32_t> unk_1C;
	be<uint32_t> unk_20;
	be<uint32_t> unk_24;
	be<uint32_t> kdf_algorithm;
	be<uint32_t> kdf_prng_algorithm;
	be<uint32_t> kdf_iteration_count;
	be<uint32_t> kdf_salt_len;
	uint8_t kdf_salt[0x20];
	be<uint32_t> unk_58;
	be<uint32_t> unk_5C;
	be<uint32_t> unk_60;
	be<uint32_t> unk_64;
	uint8_t unwrap_iv[0x20];
	be<uint32_t> wrapped_aes_key_len;
	uint8_t wrapped_aes_key[0x100];
	be<uint32_t> unk_18C;
	be<uint32_t> unk_190;
	uint8_t unk_194[0x20];
	be<uint32_t> wrapped_hmac_sha1_key_len;
	uint8_t wrapped_hmac_sha1_key[0x100];
	be<uint32_t> unk_2B8;
	be<uint32_t> unk_2BC;
	uint8_t unk_2C0[0x20];
	be<uint32_t> wrapped_integrity_key_len;
	uint8_t wrapped_integrity_key[0x100];
	be<uint32_t> unk_3E8_len;
	uint8_t unk_3E8[0x100];
	be<uint64_t> decrypted_data_length;
	be<uint32_t> unk_4F0;
	char signature[8];
};

static_assert(sizeof(DmgCryptHeaderV1) == 0x4FC, "DmgCryptHeaderV1 invalid length.");

struct DmgCryptHeaderV2
{
	char signature[8];
	be<uint32_t> maybe_version;
	be<uint32_t> unk_0C;
	be<uint32_t> unk_10;
	be<uint32_t> unk_14;
	be<uint32_t> key_bits;
	be<uint32_t> unk_1C;
	be<uint32_t> unk_20;
	uint8_t uuid[0x10];
	be<uint32_t> block_size;
	be<uint64_t> encrypted_data_length;
	be<uint64_t> encrypted_data_offset;
	be<uint32_t> no_of_keys;
};

struct DmgKeyPointer
{
	be<uint32_t> key_type;
	be<uint64_t> key_offset;
	be<uint64_t> key_length;
};

struct DmgKeyData
{
	be<uint32_t> kdf_algorithm;
	be<uint32_t> prng_algorithm;
	be<uint32_t> iteration_count;
	be<uint32_t> salt_len;
	uint8_t salt[0x20];
	be<uint32_t> blob_enc_iv_size;
	uint8_t blob_enc_iv[0x20];
	be<uint32_t> blob_enc_key_bits;
	be<uint32_t> blob_enc_algorithm;
	be<uint32_t> blob_unk_5C;
	be<uint32_t> blob_enc_mode;
	be<uint32_t> encr_key_blob_size;
	uint8_t encr_key_blob[0x200];
};

#pragma pack(pop)

DeviceDMG::DmgSection::DmgSection()
{
	method = 0;
	comment = 0;
	disk_offset = 0;
	disk_length = 0;
	dmg_offset = 0;
	dmg_length = 0;
	cache = nullptr;
}

DeviceDMG::DmgSection::~DmgSection()
{
	delete[] cache;
}

DeviceDMG::DeviceDMG() : m_crc(true)
{
	m_size = 0;

	m_is_raw = false;
	m_is_encrypted = false;
	m_crypt_offset = 0;
	m_crypt_size = 0;
	m_crypt_blocksize = 0;
}

DeviceDMG::~DeviceDMG()
{
	Close();
}

bool DeviceDMG::Open(const char * name)
{
	Close();

	m_dmg.open(name, std::ios::binary);

	if (!m_dmg.is_open())
		return false;

	KolyHeader koly;
	char signature[8];

	m_dmg.seekg(0, std::ios::end);

	m_is_raw = false;
	m_is_encrypted = false;
	m_crypt_offset = 0;
	m_crypt_size = m_dmg.tellg();

	m_dmg.seekg(-8, std::ios::end);
	m_dmg.read(signature, 8);

	if (!memcmp(signature, "cdsaencr", 8))
	{
		m_dmg.close();
		fprintf(stderr, "Encrypted DMG V1 detected. This is not supported.\n");
		return false;
		// TODO
	}

	m_dmg.seekg(0);
	m_dmg.read(signature, 8);

	if (!memcmp(signature, "encrcdsa", 8))
	{
		m_is_encrypted = true;

		if (!SetupEncryptionV2(m_dmg))
		{
			m_dmg.close();
			fprintf(stderr, "Error setting up decryption V2.\n");
			return false;
		}
	}

	ReadInternal(m_crypt_size - 0x200, &koly, sizeof(koly));

	if (memcmp(koly.signature, "koly", 4))
	{
		if (m_is_encrypted)
		{
			m_is_raw = true;
			m_size = m_crypt_size;
			return true;
		}
		else
		{
			m_dmg.close();
			return false; // R/W Image ...
		}
	}

#ifdef DMG_DEBUG
	m_dbg.open("mish.txt");
	m_dbg.flags(m_dbg.hex | m_dbg.uppercase);
	m_dbg.fill('0');

	m_dbg << "Signature      : " << std::setw(8) << koly.signature << std::endl;
	m_dbg << "Version        : " << std::setw(8) << koly.version << std::endl;
	m_dbg << "Header size    : " << std::setw(8) << koly.headersize << std::endl;
	m_dbg << "Flags          : " << std::setw(8) << koly.flags << std::endl;
	m_dbg << "Running DF Off : " << std::setw(16) << koly.running_data_fork_offset << std::endl;
	m_dbg << "Data Fork Offs : " << std::setw(16) << koly.data_fork_offset << std::endl;
	m_dbg << "Data Fork Len  : " << std::setw(16) << koly.data_fork_length << std::endl;
	m_dbg << "Rsrc Fork Off  : " << std::setw(16) << koly.rsrc_fork_offset << std::endl;
	m_dbg << "Rsrc Fork Len  : " << std::setw(16) << koly.rsrc_fork_length << std::endl;
	m_dbg << "Segment Number : " << std::setw(8) << koly.segment_number << std::endl;
	m_dbg << "Segment Count  : " << std::setw(8) << koly.segment_count << std::endl;
	m_dbg << "..." << std::endl;
	m_dbg << "XML Offset     : " << std::setw(16) << koly.xml_offset << std::endl;
	m_dbg << "XML Length     : " << std::setw(16) << koly.xml_length << std::endl;
	m_dbg << "..." << std::endl;
	DumpHex(m_dbg, reinterpret_cast<const uint8_t *>(&koly), sizeof(koly), 0x20);
	m_dbg << std::endl << std::endl;
#endif

	m_size = koly.sector_count * 0x200;

	if (koly.xml_offset != 0)
	{
		if (g_debug & Dbg_Info)
			printf("Loading DMG using XML plist.\n");

		if (!ProcessHeaderXML(koly.xml_offset, koly.xml_length))
		{
			fprintf(stderr, "Error parsing propertiy list.\n");
			m_dmg.close();
			return false;
		}
	}
	else if (koly.rsrc_fork_offset != 0)
	{
		m_dmg.close();
		m_size = 0;
		fprintf(stderr, "DMG using old resource fork format not supported.\n");
		return false;
	}
	else
	{
		m_dmg.close();
		m_size = 0;
		return false;
	}

#ifdef DMG_DEBUG
	m_dbg.close();
#endif

	return true;
}

void DeviceDMG::Close()
{
	m_dmg.close();
	m_size = 0;
	m_sections.clear();
	m_is_encrypted = false;
	m_is_raw = false;
}

bool DeviceDMG::Read(void * data, uint64_t offs, uint64_t len)
{
	if (m_is_raw)
	{
		ReadInternal(offs, data, len);
		return true;
		// TODO: Error handling ...
	}

	// Binary search start sector in m_sections
	// Get data if necessary
	// Decompress: cache data

	size_t entry_idx = m_sections.size();
	size_t rd_offs;
	size_t rd_size;
	char *bdata = reinterpret_cast<char *>(data);
	bool compressed = false;

	ptrdiff_t beg = 0;
	ptrdiff_t end = m_sections.size() - 1;
	ptrdiff_t mid;

	while (beg <= end)
	{
		mid = (beg + end) / 2;

		if (offs >= m_sections[mid].disk_offset && offs < (m_sections[mid].disk_offset + m_sections[mid].disk_length))
		{
			entry_idx = mid;
			break;
		}
		else if (offs < m_sections[mid].disk_offset)
			end = mid - 1;
		else
			beg = mid + 1;
	}

	if (entry_idx == m_sections.size())
		return false;

	while (len > 0)
	{
		if (entry_idx >= m_sections.size())
			return false;

		DmgSection &sect = m_sections[entry_idx];

		rd_offs = offs - sect.disk_offset;
		rd_size = len;
		if (rd_offs + rd_size > sect.disk_length)
			rd_size = sect.disk_length - rd_offs;

		compressed = false;

		switch (sect.method)
		{
		case 1: // raw
			ReadInternal(rd_offs + sect.dmg_offset, bdata, rd_size);
			break;
		case 2: // ignore
			memset(bdata, 0, rd_size);
			break;
		case 0x80000004: // adc
		case 0x80000005: // zlib
		case 0x80000006: // bzip2
		case 0x80000007: // lzfse
			compressed = true;
			break;
		default:
			return false;
		}

		if (compressed)
		{
			if (sect.cache == nullptr)
			{
				uint8_t *compr_buf = new uint8_t[sect.dmg_length];

				sect.cache = new uint8_t[sect.disk_length];

				ReadInternal(sect.dmg_offset, compr_buf, sect.dmg_length);

				switch (sect.method)
				{
				case 0x80000004:
					DecompressADC(sect.cache, sect.disk_length, compr_buf, sect.dmg_length);
					break;
				case 0x80000005:
					DecompressZLib(sect.cache, sect.disk_length, compr_buf, sect.dmg_length);
					break;
				case 0x80000006:
					DecompressBZ2(sect.cache, sect.disk_length, compr_buf, sect.dmg_length);
					break;
				case 0x80000007:
					DecompressLZFSE(sect.cache, sect.disk_length, compr_buf, sect.dmg_length);
					break;
				default:
					return false;
					break;
				}

				delete[] compr_buf;

				memcpy(bdata, sect.cache + rd_offs, rd_size);

#if 1 // TODO: figure out some intelligent cache mechanism ...
				delete[] sect.cache;
				sect.cache = nullptr;
#endif
			}
		}


		bdata += rd_size;
		offs += rd_size;
		len -= rd_size;
		entry_idx++;
	}

	return true;
}

uint64_t DeviceDMG::GetSize() const
{
	return m_size;
}

bool DeviceDMG::ProcessHeaderXML(uint64_t off, uint64_t size)
{
	std::vector<char> xmldata;

	xmldata.resize(size, 0);

	ReadInternal(off, xmldata.data(), size);

	PListXmlParser parser(xmldata.data(), xmldata.size());
	const PLDict *plist = parser.Parse()->toDict();

	if (!plist)
		return false;

	const PLDict *rsrc_fork = plist->get("resource-fork")->toDict();

	if (!rsrc_fork)
		return false;

	const PLArray *blkx = rsrc_fork->get("blkx")->toArray();

	if (!blkx)
		return false;

	const PLDict *entry;
	const PLData *mish;
	size_t k;

	for (k = 0; k < blkx->size(); k++)
	{
		entry = blkx->get(k)->toDict();

		if (!entry)
			return false;

		mish = entry->get("Data")->toData();

		if (!mish)
			return false;

		ProcessMish(mish->data(), mish->size());
	}

	return true;
}

void DeviceDMG::ProcessMish(const uint8_t * data, size_t size)
{
	(void)size;

	const MishHeader *mish = reinterpret_cast<const MishHeader *>(data);
	const MishEntry *entry = reinterpret_cast<const MishEntry *>(data + 0xCC);
	DmgSection section;

	uint64_t partition_start;
	uint32_t cnt;
	uint32_t k;

	if (memcmp(mish->signature, "mish", 4))
		return;

#ifdef DMG_DEBUG
	DumpHex(m_dbg, data, 0xCC, 0x20);

	m_dbg << std::setw(8) << mish->signature << ' ';
	m_dbg << std::setw(8) << mish->unk << ' ';
	m_dbg << std::setw(16) << mish->sector_start << ' ';
	m_dbg << std::setw(16) << mish->sector_count << ' ';
	m_dbg << std::setw(16) << mish->unk_18 << ' ';
	m_dbg << std::setw(8) << mish->unk_20 << ' ';
	m_dbg << std::setw(8) << mish->part_id << " - ";
	m_dbg << std::setw(8) << mish->checksum_data << std::endl;
#endif

	partition_start = mish->sector_start;

	cnt = mish->entry_count;

	for (k = 0; k < cnt; k++)
	{
#ifdef DMG_DEBUG
		m_dbg << std::setw(8) << entry[k].method << ' ';
		m_dbg << std::setw(8) << entry[k].comment << ' ';
		m_dbg << std::setw(16) << entry[k].sector_start << ' ';
		m_dbg << std::setw(16) << entry[k].sector_count << ' ';
		m_dbg << std::setw(16) << entry[k].dmg_offset << ' ';
		m_dbg << std::setw(16) << entry[k].dmg_length << std::endl;
#endif

		section.method = entry[k].method;
		section.comment = entry[k].comment;
		section.disk_offset = (entry[k].sector_start + partition_start) * 0x200;
		section.disk_length = entry[k].sector_count * 0x200;
		section.dmg_offset = entry[k].dmg_offset;
		section.dmg_length = entry[k].dmg_length;
		section.cache = nullptr;

		if (section.method != 0xFFFFFFFF && section.method != 0x7FFFFFFE)
			m_sections.push_back(section);
	}

#ifdef DMG_DEBUG
	m_dbg << std::endl;
#endif
}

void DeviceDMG::ReadRaw(void* data, size_t size, off_t off)
{
	m_dmg.seekg(off);
	m_dmg.read(reinterpret_cast<char *>(data), size);
}
