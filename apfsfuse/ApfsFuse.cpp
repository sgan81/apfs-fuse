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

#define FUSE_USE_VERSION 26

#include <fuse/fuse.h>
#include <fuse/fuse_lowlevel.h>

#include <getopt.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <attr/xattr.h>

#include <ApfsLib/ApfsContainer.h>
#include <ApfsLib/ApfsVolume.h>
#include <ApfsLib/ApfsDir.h>
#include <ApfsLib/Decmpfs.h>
#include <ApfsLib/DeviceLinux.h>

#include <cassert>
#include <cstring>
#include <cstddef>

#include <iostream>



static struct fuse_lowlevel_ops ops;

static DeviceLinux g_disk;
static ApfsContainer *g_container = nullptr;
static ApfsVolume *g_volume = nullptr;

struct Directory
{
	Directory() {}
	~Directory() {}

	std::vector<char> dirbuf;
};

struct File
{
	File() {}
	~File() {}

	bool IsCompressed() const { return (ino.ino.flags & 0x20) != 0; }

	ApfsDir::Inode ino;
	std::vector<uint8_t> decomp_data;
};

static bool apfs_stat_internal(fuse_ino_t ino, struct stat &st)
{
	ApfsDir dir(*g_volume);
	ApfsDir::Inode rec;
	bool rc = false;

	memset(&st, 0, sizeof(st));

	if (ino == 1)
	{
		st.st_ino = ino;
		st.st_mode = S_IFDIR | 0755;
		st.st_nlink = 2;
		return true;
	}

	rc = dir.GetInode(rec, ino);

	if (!rc)
	{
		return false;
	}
	else
	{
		constexpr uint64_t div_nsec = 1000000000;

		// st_dev?
		st.st_ino = ino;
		st.st_mode = rec.ino.mode;
		// st.st_nlink = rec.ino.refcnt;
		st.st_nlink = 1;
		// st_uid
		// st_gid
		// st_rdev?

		if (S_ISREG(st.st_mode))
		{
			if (rec.ino.flags & 0x20) // Compressed
			{
				std::vector<uint8_t> data;
				rc = dir.GetAttribute(data, ino, "com.apple.decmpfs");
				if (rc)
				{
					const CompressionHeader *decmpfs = reinterpret_cast<const CompressionHeader *>(data.data());

					if (IsDecompAlgoSupported(decmpfs->algo))
					{
						st.st_size = decmpfs->size;
						// st.st_blocks = data.size() / 512;
						if (g_debug > 0)
							std::cout << "Compressed size " << decmpfs->size << " bytes." << std::endl;
					}
					else if (IsDecompAlgoInRsrc(decmpfs->algo))
					{
						rc = dir.GetAttribute(data, ino, "com.apple.ResourceFork");

						if (!rc)
							st.st_size = 0;
						else
							// Compressed size
							st.st_size = data.size();
					}
					else
					{
						st.st_size = data.size();
						std::cerr << "Unknown compression algorithm " << decmpfs->algo << std::endl;
						if (!g_lax)
							return false;
					}
				}
				else
				{
					std::cerr << "Flag 0x20 set but no com.apple.decmpfs attribute!!!" << std::endl;
					if (!g_lax)
						return false;
				}
			}
			else
			{
				st.st_size = rec.sizes.size;
				// st_blksize
				// st.st_blocks = rec.sizes.size_on_disk / 512;
			}
		}
		else if (S_ISDIR(st.st_mode))
		{
			st.st_size = rec.ino.refcnt;
		}

		// What about this?
		// st.st_birthtime.tv_sec = rec.ino.birthtime / div_nsec;
		// st.st_birthtime.tv_nsec = rec.ino.birthtime % div_nsec;

		st.st_mtim.tv_sec = rec.ino.mtime / div_nsec;
		st.st_mtim.tv_nsec = rec.ino.mtime % div_nsec;
		st.st_ctim.tv_sec = rec.ino.ctime / div_nsec;
		st.st_ctim.tv_nsec = rec.ino.ctime % div_nsec;
		st.st_atim.tv_sec = rec.ino.atime / div_nsec;
		st.st_atim.tv_nsec = rec.ino.atime % div_nsec;

		return true;
	}
}

/*
static void apfs_bmap(fuse_req_t req, fuse_ino_t ino, size_t blocksize, uint64_t idx)
{
	(void)req;
	(void)ino;
	(void)blocksize;
	(void)idx;

	// fuse_reply_bmap(req, idx);
}
*/

/*
static void apfs_destroy(void *userdata)
{
	(void)userdata;
}
*/

static void apfs_getattr(fuse_req_t req, fuse_ino_t ino, fuse_file_info *fi)
{
	(void)fi;

	ApfsDir dir(*g_volume);
	ApfsDir::Inode rec;
	bool rc = false;
	struct stat st;

	if (g_debug > 0)
		std::cout << "apfs_getattr: ino=" << ino << " => ";

	rc = apfs_stat_internal(ino, st);

	if (g_debug > 0)
		std::cout << (rc ? "OK" : "FAIL") << std::endl;

	if (rc)
		fuse_reply_attr(req, &st, 1.0);
	else
		fuse_reply_err(req, ENOENT);
}

static void apfs_getxattr(fuse_req_t req, fuse_ino_t ino, const char *name, size_t size)
{
	ApfsDir dir(*g_volume);
	bool rc = false;
	std::vector<uint8_t> data;

	if (g_debug > 0)
		std::cout << "apfs_getxattr: " << std::hex << ino << " " << name << " => ";

	rc = dir.GetAttribute(data, ino, name);

	if (g_debug > 0)
		std::cout << (rc ? "OK" : "FAIL") << std::endl;

	if (!rc)
		fuse_reply_err(req, ENOATTR);
	else
	{
		if (size == 0)
			fuse_reply_xattr(req, data.size()); // xattr size
		else
			fuse_reply_buf(req, reinterpret_cast<const char *>(data.data()), std::min(data.size(), size));
	}
}

static void apfs_listxattr(fuse_req_t req, fuse_ino_t ino, size_t size)
{
	ApfsDir dir(*g_volume);
	bool rc = false;
	std::vector<std::string> names;
	std::string reply;

	rc = dir.ListAttributes(names, ino);

	if (g_debug > 0)
		std::cout << "apfs_listxattr:" << std::endl;

	if (rc)
	{
		for (size_t k = 0; k < names.size(); k++)
		{
			if (g_debug > 0)
				std::cout << names[k] << std::endl;
			reply.append(names[k]);
			reply.push_back(0);
		}
	}

	if (size == 0)
		fuse_reply_xattr(req, reply.size());
	else if (size < reply.size())
		fuse_reply_err(req, ERANGE);
	else
		fuse_reply_buf(req, reply.c_str(), reply.size());
}

static void apfs_lookup(fuse_req_t req, fuse_ino_t ino, const char *name)
{
	if (g_debug > 0)
		std::cout << "apfs_lookup: ino=" << ino << " name=" << name << " => ";

	ApfsDir dir(*g_volume);
	ApfsDir::Name res;
	bool rc;

	rc = dir.LookupName(res, ino, name);

	if (g_debug > 0)
		std::cout << (rc ? "OK" : "FAIL") << std::endl;

	if (!rc)
	{
		fuse_reply_err(req, ENOENT);
	}
	else
	{
		fuse_entry_param e;

		e.ino = res.inode_id;
		e.attr_timeout = 1.0;
		e.entry_timeout = 1.0;

		rc = apfs_stat_internal(res.inode_id, e.attr);

		if (g_debug > 0)
			std::cout << "    apfs_stat_internal => " << (rc ? "OK" : "FAIL") << std::endl;

		fuse_reply_entry(req, &e);
	}
}

static void apfs_open(fuse_req_t req, fuse_ino_t ino, fuse_file_info *fi)
{
	if (g_debug > 0)
		std::cout << std::hex << "apfs_open: " << ino << std::endl;

	bool rc;

	if ((fi->flags & 3) != O_RDONLY)
		fuse_reply_err(req, EACCES);
	else
	{
		File *f = new File();
		ApfsDir dir(*g_volume);

		rc = dir.GetInode(f->ino, ino);

		if (!rc)
		{
			std::cerr << "Couldn't get inode " << ino << std::endl;
			fuse_reply_err(req, ENOENT);
			delete f;
			return;
		}

		if (f->IsCompressed())
		{
			std::vector<uint8_t> attr;

			rc = dir.GetAttribute(attr, ino, "com.apple.decmpfs");

			if (!rc)
			{
				std::cerr << "Couldn't get attribute com.apple.decmpfs for " << ino << std::endl;
				fuse_reply_err(req, ENOENT);
				delete f;
				return;
			}

			if (g_debug > 0) {
				std::cout << "Inode info: size=" << f->ino.sizes.size
				          << ", size_on_disk=" << f->ino.sizes.size_on_disk << "\n";
			}
			rc = DecompressFile(dir, ino, f->decomp_data, attr);
			// In strict mode, do not return uncompressed data.
			if (!rc && !g_lax) {
				fuse_reply_err(req, EIO);
				delete f;
				return;
			}
		}

		fi->fh = reinterpret_cast<uint64_t>(f);

		fuse_reply_open(req, fi);
	}
}

static void apfs_opendir(fuse_req_t req, fuse_ino_t ino, fuse_file_info *fi)
{
	if (g_debug > 0)
		std::cout << std::hex << "apfs_opendir: " << ino << std::endl;

	Directory *dir = new Directory();

	fi->fh = reinterpret_cast<uint64_t>(dir);

	fuse_reply_open(req, fi);
}

static void apfs_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi)
{
	ApfsDir dir(*g_volume);
	File *file = reinterpret_cast<File *>(fi->fh);

	if (g_debug > 0)
		printf("apfs_read: ino=%016lX size=%016lX off=%016lX\n", ino, size, off);

	if (!file->IsCompressed())
	{
		// bool rc;
		std::vector<char> buf(size, 0);

		// rc =
		dir.ReadFile(buf.data(), file->ino.ino.object_id, off, size);

		// std::cerr << "apfs_read: fuse_reply_buf(req, " << reinterpret_cast<uint64_t>(buf.data()) << ", " << size << ")" << std::endl;

		fuse_reply_buf(req, buf.data(), buf.size());
	}
	else
	{
		if (static_cast<size_t>(off) >= file->decomp_data.size())
			size = 0;
		else if (off + size > file->decomp_data.size())
			size = file->decomp_data.size() - off;

		// std::cerr << "apfs_read: fuse_reply_buf(req, " << reinterpret_cast<uint64_t>(file->decomp_data.data()) + off << ", " << size << ")" << std::endl;

		fuse_reply_buf(req, reinterpret_cast<const char *>(file->decomp_data.data()) + off, size);
	}
}

static void dirbuf_add(fuse_req_t req, std::vector<char> &dirbuf, const char *name, fuse_ino_t ino)
{
	struct stat st;
	size_t oldsize;

	memset(&st, 0, sizeof(st));
	oldsize = dirbuf.size();
	dirbuf.resize(oldsize + fuse_add_direntry(req, nullptr, 0, name, nullptr, 0));
	st.st_ino = ino;
	fuse_add_direntry(req, &dirbuf[oldsize], dirbuf.size() - oldsize, name, &st, dirbuf.size());
}

#define min(x, y) ((x) < (y) ? (x) : (y))

static int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize, off_t off, size_t maxsize)
{
	if (static_cast<size_t>(off) < bufsize)
		return fuse_reply_buf(req, buf + off, min(bufsize - off, maxsize));
	else
		return fuse_reply_buf(req, NULL, 0);
}


static void apfs_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi)
{
	ApfsDir dir(*g_volume);
	std::vector<ApfsDir::Name> dir_list;
	Directory *dirptr = reinterpret_cast<Directory *>(fi->fh);
	std::vector<char> &dirbuf = dirptr->dirbuf;
	bool rc;

	if (g_debug > 0)
		std::cout << "apfs_readdir: " << std::hex << ino << std::endl;

	if (dirbuf.size() == 0)
	{
		dirbuf.reserve(0x1000);

#if 0 // Not needed
		if (ino != 1)
		{
			ApfsDir::Inode dirrec;

			rc = dir.GetInode(dirrec, ino);

			if (!rc)
			{
				fuse_reply_err(req, ENOENT);
				return;
			}

			dirbuf_add(req, dirbuf, ".", ino);
			dirbuf_add(req, dirbuf, "..", dirrec.ino.parent_id);
		}
#endif
		rc = dir.ListDirectory(dir_list, ino);
		if (!rc)
		{
			fuse_reply_err(req, ENOENT);
			return;
		}

		for (size_t k = 0; k < dir_list.size(); k++)
			dirbuf_add(req, dirbuf, dir_list[k].name.c_str(), dir_list[k].inode_id);
	}

	reply_buf_limited(req, dirbuf.data(), dirbuf.size(), off, size);
}

static void apfs_readlink(fuse_req_t req, fuse_ino_t ino)
{
	ApfsDir dir(*g_volume);
	bool rc = false;
	std::vector<uint8_t> data;

	rc = dir.GetAttribute(data, ino, "com.apple.fs.symlink");
	if (!rc)
		fuse_reply_err(req, ENOENT);
	else
		fuse_reply_readlink(req, reinterpret_cast<const char *>(data.data()));
}

static void apfs_release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	if (g_debug > 0)
		std::cout << std::hex << "apfs_release " << ino << std::endl;

	File *file = reinterpret_cast<File *>(fi->fh);
	delete file;

	fuse_reply_err(req, 0);
}

static void apfs_releasedir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
	if (g_debug > 0)
		std::cout << std::hex << "apfs_releasedir " << ino << std::endl;

	Directory *dir = reinterpret_cast<Directory *>(fi->fh);
	delete dir;

	fuse_reply_err(req, 0);
}

/*
static void apfs_statfs(fuse_req_t req, fuse_ino_t ino)
{
	(void)req;
	(void)ino;
}
*/

struct apfs {
	char *disk;
	char *logfile;
	int vol_id;
};

void usage(const char *name)
{
	std::cout << name << " [-d level] [-o options] [-v volume-id] [-r passphrase] [-s offset] <device> <dir>" << std::endl;
}

bool add_option(std::string &optstr, const char *name, const char *value)
{
	if (!optstr.empty())
		optstr.push_back(',');
	optstr.append(name);
	if (value)
	{
		optstr.push_back('=');
		optstr.append(value);
	}

	return true;
}

int main(int argc, char *argv[])
{
	struct fuse_args args = FUSE_ARGS_INIT(0, nullptr);
	struct fuse_chan *ch;
	const char *mountpoint = nullptr;
	const char *dev_path = nullptr;
	int err = -1;
	int opt;
	std::string mount_options, passphrase;
	unsigned int volume_id = 0;
	size_t container_offset = 0;

	// static const char *dev_path = "/mnt/data/Projekte/VS17/Apfs/Data/ios_11_0_1.img";
	// static const char *dev_path = "/mnt/data/Projekte/VS17/Apfs/Data/apfs_2vol_test_rw.dmg";
	// static const char *dev_path = "/dev/sdd2";
	// static const char *dev_path = "/mnt/data/Projekte/VS17/Apfs/Data/apfs_clone_test.img";

	memset(&ops, 0, sizeof(ops));

	// ops.bmap = apfs_bmap;
	// ops.destroy = apfs_destroy;
	ops.getattr = apfs_getattr;
	ops.getxattr = apfs_getxattr;
	ops.listxattr = apfs_listxattr;
	ops.lookup = apfs_lookup;
	ops.open = apfs_open;
	ops.opendir = apfs_opendir;
	ops.read = apfs_read;
	ops.readdir = apfs_readdir;
	ops.readlink = apfs_readlink;
	ops.release = apfs_release;
	ops.releasedir = apfs_releasedir;
	// ops.statfs = apfs_statfs;

	while ((opt = getopt(argc, argv, "d:o:v:r:s:l")) != -1)
	{
		switch (opt)
		{
			case 'd':
				g_debug = strtoul(optarg, nullptr, 10);
				break;
			case 'o':
				add_option(mount_options, optarg, nullptr);
				break;
			case 'v':
				volume_id = strtoul(optarg, nullptr, 10);
				break;
			case 'r':
				passphrase = std::string(optarg);
				break;
			case 's':
				container_offset = strtoul(optarg, nullptr, 10);
				break;
			case 'l':
				g_lax = true;
				break;
			default:
				usage(argv[0]);
				return 1;
				break;
		}
	}

	if ((argc - optind) != 2)
	{
		usage(argv[0]);
		return 1;
	}

	dev_path = argv[optind];
	mountpoint = argv[optind + 1];

	add_option(mount_options, "ro", nullptr);

	if (!g_disk.Open(dev_path))
	{
		std::cerr << "Error opening device!" << std::endl;
		return 1;
	}
	if (container_offset >= g_disk.GetSize()) {
		std::cerr << "Invalid container offset specified" << std::endl;
		return 1;
	}
	g_container = new ApfsContainer(g_disk,
                                  container_offset,
																	g_disk.GetSize() - container_offset);
	g_container->Init();
	g_volume = g_container->GetVolume(volume_id, passphrase);
	if (!g_volume)
	{
		std::cerr << "Unable to get volume!" << std::endl;
		delete g_container;
		g_disk.Close();
		return 1;
	}

	add_option(mount_options, "fsname", dev_path);
	add_option(mount_options, "allow_other", nullptr);

	fuse_opt_add_arg(&args, "apfs-fuse");
	fuse_opt_add_arg(&args, "-o");
	fuse_opt_add_arg(&args, mount_options.c_str());

	if ((ch = fuse_mount(mountpoint, &args)) != NULL)
	{
		struct fuse_session *se;

		se = fuse_lowlevel_new(&args, &ops, sizeof(ops), NULL);
		if (se != NULL) {
			if (fuse_set_signal_handlers(se) != -1) {
				fuse_session_add_chan(se, ch);
				err = fuse_session_loop(se);
				fuse_remove_signal_handlers(se);
				fuse_session_remove_chan(ch);
			}
			fuse_session_destroy(se);
		}
		fuse_unmount(mountpoint, ch);
	}
	fuse_opt_free_args(&args);

	delete g_volume;
	delete g_container;
	g_disk.Close();

	return err ? 1 : 0;
}
