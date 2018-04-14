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

#undef WRITE_DECOMP

DeviceDMG::DmgSection::DmgSection()
{
	method = 0;
	last_access = 0;
	drive_offset = 0;
	drive_length = 0;
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

	m_dmg.seekg(-0x200, std::ios::end);
	m_dmg.read(reinterpret_cast<char *>(&koly), sizeof(koly));

	if (koly.signature != 'koly')
	{
		m_dmg.close();
		return false; // R/W Image ...
	}

	m_size = koly.sector_count * 0x200;

	std::ofstream tst;
	std::vector<char> xml;
	std::string xmlstr;

	if (koly.xml_offset == 0)
	{
		m_dmg.close();
		m_size = 0;
		return false;
	}

	m_dmg.seekg(koly.xml_offset.get());
	xml.resize(koly.xml_length);
	m_dmg.read(xml.data(), xml.size());

	xmlstr.assign(xml.data(), xml.size());

	tst.open("Image.xml", std::ios::binary);
	tst.write(xmlstr.c_str(), xmlstr.size());
	tst.close();

	const char *tmp = xmlstr.c_str();
	size_t off = 0;
	size_t start;
	size_t end;
	std::vector<uint8_t> mish;
	DmgSection section;
	uint64_t partition_start;

#ifdef WRITE_DECOMP
	std::vector<uint8_t> decomp_test;
	std::vector<uint8_t> compr_test;
	std::ofstream decomp;
	size_t out_size;

	decomp.open("Decomp.img", std::ios::binary);

	std::cout.flags(std::ios::hex | std::ios::uppercase);
	std::cout.fill('0');
#endif

	// tst.open("mish.dat", std::ios::binary);
	tst.open("mish.txt");
	tst.flags(tst.hex | tst.uppercase);
	tst.fill('0');

	tst << "Signature      : " << std::setw(8) << koly.signature << std::endl;
	tst << "Version        : " << std::setw(8) << koly.version << std::endl;
	tst << "Header size    : " << std::setw(8) << koly.headersize << std::endl;
	tst << "Flags          : " << std::setw(8) << koly.flags << std::endl;
	tst << "Running DF Off : " << std::setw(16) << koly.running_data_fork_offset << std::endl;
	tst << "Data Fork Offs : " << std::setw(16) << koly.data_fork_offset << std::endl;
	tst << "Data Fork Len  : " << std::setw(16) << koly.data_fork_length << std::endl;
	tst << "Rsrc Fork Off  : " << std::setw(16) << koly.rsrc_fork_offset << std::endl;
	tst << "Rsrc Fork Len  : " << std::setw(16) << koly.rsrc_fork_length << std::endl;
	tst << "Segment Number : " << std::setw(8) << koly.segment_number << std::endl;
	tst << "Segment Count  : " << std::setw(8) << koly.segment_count << std::endl;
	tst << "..." << std::endl;
	tst << "XML Offset     : " << std::setw(16) << koly.xml_offset << std::endl;
	tst << "XML Length     : " << std::setw(16) << koly.xml_length << std::endl;
	tst << "..." << std::endl;
	DumpHex(tst, reinterpret_cast<const uint8_t *>(&koly), sizeof(koly), 0x20);

	for (;;)
	{
		start = xmlstr.find("<data>", off);
		if (start == std::string::npos)
			break;
		end = xmlstr.find("</data>", start);
		if (end == std::string::npos)
			break;
		start += 6;

		Base64Decode(mish, tmp + start, end - start);

		const uint8_t *tmp2 = mish.data();

		const MishHeader *hdr = reinterpret_cast<const MishHeader *>(tmp2);
		const MishEntry *entry = reinterpret_cast<const MishEntry *>(tmp2 + 0xCC);

		off = end + 7;

		if (hdr->signature == 'mish')
		{
			DumpHex(tst, tmp2, 0xCC, 0x20);

			tst << std::setw(8) << hdr->signature << ' ';
			tst << std::setw(8) << hdr->unk << ' ';
			tst << std::setw(16) << hdr->sector_start << ' ';
			tst << std::setw(16) << hdr->sector_count << ' ';
			tst << std::setw(16) << hdr->unk_18 << ' ';
			tst << std::setw(8) << hdr->unk_20 << ' ';
			tst << std::setw(8) << hdr->part_id << " - ";
			tst << std::setw(8) << hdr->checksum_data << std::endl;

			partition_start = hdr->sector_start;

#ifdef WRITE_DECOMP
			m_crc.SetCRC(0xFFFFFFFF);

			std::cout << "Dumping Partition " << hdr->part_id << " ... ";
#endif

			uint32_t cnt = hdr->entry_count;
			for (uint32_t k = 0; k < cnt; k++)
			{
				tst << std::setw(8) << entry[k].method << ' ';
				tst << std::setw(8) << entry[k].unk << ' ';
				tst << std::setw(16) << entry[k].sector_start << ' ';
				tst << std::setw(16) << entry[k].sector_count << ' ';
				tst << std::setw(16) << entry[k].dmg_offset << ' ';
				tst << std::setw(16) << entry[k].dmg_length << std::endl;

				section.method = entry[k].method;
				section.last_access = 0;
				section.drive_offset = (entry[k].sector_start + partition_start) * 0x200;
				section.drive_length = entry[k].sector_count * 0x200;
				section.dmg_offset = entry[k].dmg_offset;
				section.dmg_length = entry[k].dmg_length;
				section.cache = nullptr;

				if (section.method != 0xFFFFFFFF)
					m_sections.push_back(section);

#ifdef WRITE_DECOMP
				switch (entry[k].method)
				{
				case 0x00000001: // raw
					compr_test.clear();
					compr_test.resize(entry[k].dmg_length);
					m_dmg.seekg(entry[k].dmg_offset.get());
					m_dmg.read(reinterpret_cast<char *>(compr_test.data()), compr_test.size());
					decomp.write(reinterpret_cast<const char *>(compr_test.data()), compr_test.size());
					m_crc.Calc(compr_test.data(), compr_test.size());
					break;

				case 0x00000002: // ignore
					out_size = entry[k].sector_count * 0x200;
					decomp_test.clear();
					decomp_test.resize(0x100000, 0);
					while (out_size > 0x100000)
					{
						decomp.write(reinterpret_cast<const char *>(decomp_test.data()), 0x100000);
						out_size -= 0x100000;
					}
					if (out_size > 0)
						decomp.write(reinterpret_cast<const char *>(decomp_test.data()), out_size);
					break;

				case 0x80000005: // zlib
					compr_test.clear();
					compr_test.resize(entry[k].dmg_length);
					m_dmg.seekg(entry[k].dmg_offset.get());
					m_dmg.read(reinterpret_cast<char *>(compr_test.data()), compr_test.size());
					decomp_test.clear();
					decomp_test.resize(entry[k].sector_count * 0x200);
					DecompressZLib(decomp_test.data(), decomp_test.size(), compr_test.data(), compr_test.size());
					decomp.write(reinterpret_cast<const char *>(decomp_test.data()), decomp_test.size());
					m_crc.Calc(decomp_test.data(), decomp_test.size());
					break;

				case 0xFFFFFFFF:
					break;

				default:
					std::cerr << "Unknown DMG compression method " << entry[k].method << std::endl;
					break;
				}

#endif
			}
#ifdef WRITE_DECOMP
			std::cout << std::setw(8) << (m_crc.GetCRC() ^ 0xFFFFFFFF) << std::endl;
#endif
		}
		// tst.write(reinterpret_cast<const char *>(mish.data()), mish.size());
		tst << std::endl;
	}

#ifdef WRITE_DECOMP
	decomp.close();
#endif
	tst.close();

	return true;
}

void DeviceDMG::Close()
{
	m_dmg.close();
	m_size = 0;
	m_sections.clear();
}

bool DeviceDMG::Read(void * data, uint64_t offs, uint64_t len)
{
	// Binary search start sector in m_sections
	// Get data if necessary
	// Decompress: cache data

	size_t k;
	size_t entry_idx = -1;
	size_t rd_offs;
	size_t rd_size;
	char *bdata = reinterpret_cast<char *>(data);

	// TODO: bsearch
	for (k = 0; k < m_sections.size(); k++)
	{
		if (offs >= m_sections[k].drive_offset && offs < (m_sections[k].drive_offset + m_sections[k].drive_length))
		{
			entry_idx = k;
			break;
		}
	}

	if (entry_idx == -1)
		return false;

	// TODO: reads over multiple sections ...
	while (len > 0)
	{
		if (entry_idx >= m_sections.size())
			return false;

		DmgSection &sect = m_sections[entry_idx];

		rd_offs = offs - sect.drive_offset;
		rd_size = len;
		if (rd_offs + rd_size > sect.drive_length)
			rd_size = sect.drive_length - rd_offs;

		switch (sect.method)
		{
		case 1: // raw
			m_dmg.seekg(rd_offs + sect.dmg_offset);
			m_dmg.read(bdata, rd_size);
			break;
		case 2: // ignore
			memset(bdata, 0, rd_size);
			break;
		case 0x80000005: // zlib
			if (sect.cache == nullptr)
			{
				uint8_t *compr_buf = new uint8_t[sect.dmg_length];

				sect.cache = new uint8_t[sect.drive_length];

				m_dmg.seekg(sect.dmg_offset);
				m_dmg.read(reinterpret_cast<char *>(compr_buf), sect.dmg_length);

				DecompressZLib(sect.cache, sect.drive_length, compr_buf, sect.dmg_length);

				delete[] compr_buf;
			}

			memcpy(bdata, sect.cache + rd_offs, rd_size);

#if 1 // TEST ... wieder aktivieren!
			{
				delete[] sect.cache;
				sect.cache = nullptr;
			}
#endif

			break;
		default:
			return false;
			break;
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

void DeviceDMG::Base64Decode(std::vector<uint8_t>& bin, const char * str, size_t size)
{
	int chcnt;
	uint32_t buf;
	size_t ip;
	char ch;
	uint32_t dec;

	bin.clear();
	bin.reserve(size * 4 / 3);

	chcnt = 0;
	buf = 0;

	for (ip = 0; ip < size; ip++)
	{
		ch = str[ip];

		if (ch >= 'A' && ch <= 'Z')
			dec = ch - 'A';
		else if (ch >= 'a' && ch <= 'z')
			dec = ch - 'a' + 0x1A;
		else if (ch >= '0' && ch <= '9')
			dec = ch - '0' + 0x34;
		else if (ch == '+')
			dec = 0x3E;
		else if (ch == '/')
			dec = 0x3F;
		else if (ch != '=')
			continue;
		else
			break;

		buf = (buf << 6) | dec;
		chcnt++;

		if (chcnt == 2)
			bin.push_back((buf >> 4) & 0xFF);
		else if (chcnt == 3)
			bin.push_back((buf >> 2) & 0xFF);
		else if (chcnt == 4)
		{
			bin.push_back((buf >> 0) & 0xFF);
			chcnt = 0;
		}
	}
}
