/*
This file is part of apfs-fuse, a read-only implementation of APFS
(Apple File System) for FUSE.
Copyright (C) 2018 Jief Luce, base on the work of Tor Arne Vestbø.

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
#include "DeviceDMG.h"
#include "DeviceSparsebundle.h"


#include <stdio.h>
#include <unistd.h>
#include <inttypes.h> // for strtoumax
#include <fcntl.h> // open, read, close...

uint64_t xml_get_integer(const char* filename, const char* elem)
{
	FILE *f = fopen(filename, "rb");
	if ( fseek(f, 0, SEEK_END) != 0 ) return 0;
	size_t fsize = ftell(f);
	if ( fseek(f, 0, SEEK_SET) != 0 ) return 0;  //same as rewind(f);

	char token[fsize + 1];
	if ( fread(token, 1, fsize, f) != fsize ) return 0;
	fclose(f);
	token[fsize] = 0;

	char* bufferPtr = token;

	while (bufferPtr)
	{
		char* posKey = strstr(bufferPtr, "<key>");
		if ( !posKey ) return 0;
		posKey += strlen("<key>");
		while ( *posKey == ' ' || *posKey == '\r' || *posKey == '\n' || *posKey == '\t' ) posKey++;

		char* posKeyEnd = strstr(bufferPtr, "</key>");
		if ( !posKeyEnd ) return 0;
		bufferPtr = posKeyEnd + strlen("</key>");
		while ( *(posKey-1) == ' ' || *(posKey-1) == '\r' || *(posKey-1) == '\n' || *(posKey-1) == '\t' ) posKey--;
		*posKeyEnd = 0;

		if ( strcmp(posKey, elem) == 0 )
		{
			char* posValue = bufferPtr;
			while ( *posValue == ' ' || *posValue == '\r' || *posValue == '\n' || *posValue == '\t' ) posValue++;
			if ( strncmp(posValue, "<integer>", strlen("<integer>")) != 0 ) return 0;
			posValue += strlen("<integer>");
			while ( *posValue == ' ' || *posValue == '\r' || *posValue == '\n' || *posValue == '\t' ) posValue++;

			char* posValueEnd = strstr(bufferPtr, "</integer>");
			if ( !posValueEnd ) return 0;
			bufferPtr = posValueEnd + strlen("</integer>"); // for just after
			while ( *(posValueEnd-1) == ' ' || *(posValueEnd-1) == '\r' || *(posValueEnd-1) == '\n' || *(posValueEnd-1) == '\t' ) posValueEnd--;
			*posValueEnd = 0;

			uint64_t rv = strtoumax(posValue, 0, 10);
			return rv;

		}
	}
	return 0;
}

DeviceSparsebundle::DeviceSparsebundle()
{
	m_path = NULL;
	m_band_path = NULL;
}

DeviceSparsebundle::~DeviceSparsebundle()
{
	Close();
}

bool DeviceSparsebundle::Open(const char* name)
{
  char printf_zerobuf[1];
  int printf_size;

	Close();

    if (!name) {
    	errno = ENOENT;
        return false;
    }

    m_path = realpath(name, NULL);
    if (!m_path) {
    	errno = ENOENT;
        return false;
    }
    size_t m_band_path_len = strlen(m_path) + 7 + 20 + 1;  // 7 for "/bands/" and 20 is the max nb of digits of 64 bits unsigned int.
    m_band_path = (char*)malloc(m_band_path_len);
    m_band_path_band_number_start = m_band_path + sprintf(m_band_path, "%s/bands/", m_path);

    m_blocksize = 512;
    m_band_size = 0;
    m_opened_file_fd = -1;
    m_opened_file_band_number = -1;

	// Read plist
    printf_size = snprintf(printf_zerobuf, 0, "%s/Info.plist", m_path);
    char plist_path[printf_size+1];
    snprintf(plist_path, printf_size+1, "%s/Info.plist", m_path);
	m_band_size = xml_get_integer(plist_path, "band-size");
	m_size = xml_get_integer(plist_path, "size");
	if ( m_band_size == 0 ) {
		fprintf(stderr, "Cannot get band-size from plist '%s'", plist_path);
		free(m_path);
		errno = ENXIO;
        return false;
	}
	if ( m_size == 0 ) {
		fprintf(stderr, "Cannot get size from plist '%s'", plist_path);
		free(m_path);
		errno = ENXIO;
        return false;
	}

	printf("Initialized %s, band size %zu, total size %" PRId64 "\n", m_path, m_band_size, m_size);

	// Check token file for encryption
	printf_size = snprintf(printf_zerobuf, 0, "%s/token", name);
	char token_filename[printf_size+1];
	snprintf(token_filename, printf_size+1, "%s/token", name);

	std::ifstream tokenStream;
	tokenStream.open(token_filename, std::ios::binary);
	if ( tokenStream.is_open() )
	{
		char signature[8];
		tokenStream.seekg(0);
		tokenStream.read(signature, 8);

		if (!memcmp(signature, "encrcdsa", 8))
		{
			m_is_encrypted = true;

			if (!SetupEncryptionV2(tokenStream))
			{
				tokenStream.close();
				fprintf(stderr, "Error setting up decryption V2.\n");
				return false;
			}
			m_crypt_offset = 0; // because token is separated from data, offset is always 0.
		}
	}
	return true;
}

void DeviceSparsebundle::Close()
{
	if ( m_path ) {
		free(m_path);
		m_path = NULL;
		if ( m_opened_file_fd != -1 ) close(m_opened_file_fd);
	}
}

void DeviceSparsebundle::ReadRaw(void* buffer, size_t nbytes, off_t offset)
{
    if (offset < 0)
        throw new DeviceException("DeviceSparsebundle::ReadRaw offset < 0");
    if (offset >= m_size)
        throw new DeviceException("DeviceSparsebundle::ReadRaw offset >= size");

    if ( offset + (off_t)nbytes > m_size) {
        nbytes = m_size - offset;
    }

    size_t bytes_read = 0;
    while (bytes_read < nbytes) {
        off_t band_number = (offset + bytes_read) / m_band_size;
        off_t band_offset = (offset + bytes_read) % m_band_size;

        size_t to_read = nbytes - bytes_read;
        if ( to_read > m_band_size - band_offset )  to_read = m_band_size - band_offset;

        // Caching opened file desciptor to avoid open and close.
        if ( m_opened_file_band_number != band_number )
        {
        	if ( m_opened_file_fd != -1 ) close(m_opened_file_fd);
			sprintf(m_band_path_band_number_start, "%" PRIx64, band_number);
        	m_opened_file_fd = open(m_band_path, O_RDONLY);
        	if ( m_opened_file_fd == -1 ) {
        		m_opened_file_band_number = -1;
        		throw new DeviceException("DeviceSparsebundle::ReadRaw cannot open band %lld (at '%s')", band_number, m_band_path);
        	}
        	m_opened_file_band_number = band_number;
        }

        ssize_t nb_read = pread(m_opened_file_fd, ((uint8_t*)buffer)+bytes_read, to_read, band_offset);
        if (nb_read < 0) {
            throw new DeviceException("DeviceSparsebundle::ReadRaw read at offset %lld returns %zd", band_offset, read);
        }

        if (size_t(nb_read) < to_read) { // nb_read is > 0 so cast is safe
            ssize_t to_pad = to_read - nb_read;
            if ( to_pad+bytes_read+nb_read > nbytes ) {
            	exit(1);
            }
            memset(((uint8_t*)buffer)+bytes_read+nb_read, 0, to_pad);
            nb_read += to_pad;
        }

        bytes_read += nb_read;
    }

	if (bytes_read != nbytes) throw new DeviceException("DeviceSparsebundle::ReadRaw read only %zd bytes instead of %zd", read, nbytes);
}

bool DeviceSparsebundle::Read(void *data, uint64_t offs, uint64_t len)
{
	ReadInternal(offs, data, len);
	return true;
}

uint64_t DeviceSparsebundle::GetSize() const
{
	return m_size;
}
