
# APFS FUSE Driver for Linux

This project is a read-only FUSE driver for the new Apple File System. It also supports software
encrypted volumes and fusion drives. Firmlinks are not supported yet.

Be aware that not all compression methods are supported yet (only the ones I have encountered so far).
Thus, the driver may return compressed files instead of uncompressed ones. Although most of the time it
should just report an error.

## Changelog

| Date | Comment |
|------|---------|
| 2020-07-08 | Added support for mounting snapshots and sealed volumes |
| 2018-04-20 | Added support for mounting DMGs |
| 2018-04-14 | Added support for partition tables (GPT only) |
| 2018-04-10 | Fixed and extended FileVault encryption support |
| 2018-03-28 | Added support for FileVault encryption |
| 2017-10-25 | Added support for encryption |
| 2017-10-14 | Initial version |

## Usage

### Compile the source code
The following libraries are needed (including the -dev/-devel packages):

* FUSE 2.6 or greater (on 32-bit systems, FUSE 3.0 or greater)
* zlib
* bzip2
* libattr (on some Linux distributions)

Development tools:
* cmake
* gcc-c++ (or clang++)
* git (for cloning)

Example for Linux:
```
sudo apt update
sudo apt install fuse libfuse3-dev bzip2 libbz2-dev cmake gcc-c++ git libattr1-dev zlib1g-dev
```
If you get an error at the above step complaining about "c+" not found or similar, it seems your distro does not like installing c++ using regular apt, so we use apt-get with quotes:
```
sudo apt-get install "c++"
```
Of course these commands depend on the Linux distribution.

Lastly, if running apfs-fuse directly in terminal returns command not recognized, do this inside the apfs-fuse/build folder:
```
sudo cp -a . /usr/local/bin
```

Clone the repository:
```
git clone https://github.com/sgan81/apfs-fuse.git
cd apfs-fuse
git submodule init
git submodule update
```
The driver uses Apple's lzfse library and includes it as a submodule.

Compile the driver:
```
mkdir build
cd build
cmake ..
ccmake . # Only if you want to change build options
make
```

Note that the driver uses FUSE 3.0 by default (required on 32-bit systems). If
you want do compile using FUSE 2.6, use `ccmake .` to change the option
`USE_FUSE3` to `OFF`.

### Mount a drive
```
apfs-fuse <device> <mount-directory>
```
#### Supported options:
* `-d n`: If n > 0, enable debug output (see below for details).
* `-f device`: Specify secondary device for Fusion drive.
* `-o opts`: Comma-separated list of mount options.
* `-l`: Lax mode: when unexpected data is encountered, try to continue, even if this means
  returning potentially incorrect data.
* `-v n`: Instead of mounting the first volume in a container, mount volume n (starting at 0).
* `-r recovery_key`: Mount an encrypted volume by supplying a password or Personal Recovery Key (PRK).
* `-s n`: Find the container at offset n inside the device. This is useful when using an image file
  instead of a disk device, and therefore partitions are not exposed.
* `-p n`: Find the container at partition n inside the device.

If you are using an image file containing partitions, the driver will now detect if there is a valid GPT
partition table. If there is, it will look for the first APFS partition and use that one for the container.
If your drive contains more than one APFS container, you can specify the partition/container id with the
`-p` option.

The device has to be the one containing the APFS container. If a container contains more than one volume,
the volume can be specified by the `-v` option.

If a volume is encrypted, the apfs-fuse command will prompt for a password, unless a password or PRK is
specified on the command line. The PRK can also be used as password.

It is also possible to directly mount DMG files. The driver will automatically detect if a dmg
has to be mounted and take appropriate action. If a dmg is encrypted, you will be asked for the password.
Note that dmg support is currently a bit slow (especially when compressed), but it should work properly.

The debug flags are now a combination of bits. So to enable specific output, you just add the numbers
mentioned below together, and use the result as parameter for -d.

* 1 Display more information about errors
* 2 Display additional generic information
* 4 Display information about directory-related operations
* 8 Display information about on-the-fly compression
* 16 Display information about cryptographic operations (caution, displays keys as well)

#### Mount options (-o ...)
In addition to the mount options supported by fuse, the following mount options are supported:
* uid=n: Pretend that all files have UID n.
* gid=n: Pretend that all files have GID n.
* vol=n: Same as -v, specify the volume number to mount if you don't want volume 0.
* blksize=n: Set the physical block size (default: 512 bytes).
* pass=...: Specify volume passphrase (same as -r).
* xid=...: Try to mount older XID. May be useful if the container is corrupt.
* snap=...: Mount snapshot with given XID. Use apfsutil to display snapshot ids.

The blksize parameter is required for proper partition table parsing on some newer
macs. However the current driver should be able to detect the block size automatically.

If you mount a volume as root and want some user to be able to access it, use:
```
apfs-fuse -o uid=<uid>,gid=<gid>,allow_other /dev/<device> <mount-path>
```

If you want to mount a device as user, add yourself to the disk group. This might not be too safe though,
as it allows any application to read and write anywhere on a drive.

### Unmount a drive
As root:
```
umount <mount-directory>
```

As user:
```
fusermount -u <mount-directory>
```

## Features
The following features are implemented:

* Can read macOS 10.13 case sensitive and insensitive volumes, as well as iOS 11 / macOS 10.12 volumes
* Transparent decompression of ZLib and LZVN
* Symlinks
* Hardlinks (it seems ...)
* Extended attributes
* Software encryption (at least full-disk encryption)
* Automatic detection of GPT partition tables
* Direct mounting of DMG images (supports zlib/adc compression and encryption)

## Limitations
These things are not supported (yet):

* Transparent decompression of LZFSE
* Writing
* Hardware-encrypted volumes (internal drives of Macs with T2 chip)

## Debugging

Since the driver is still experimental and based on analysis of a limited set of drives / volumes, crashes
may unfortunately still happen. If a crash happens, providing me with useful information can be very helpful.

One of the most important pieces of information is the location where the program crashed. You can find that
out by debugging the tool. In order to debug the program, do the following:

Change the main CMakeLists.txt as follows: In the line `set(CMAKE_BUILD_TYPE Release)`,
replace `Release` with `Debug`.

Rebuild everything.

Run it under `gdb`. Like this:
```
gdb apfs-fuse

(In gdb):
set args <options> <device> <mount-directory>
run

(And if/when it crashes):
backtrace

(When you're finished):
quit
```
And then send the output of `backtrace` to me. Adding `-d 1` to options might help as well, but be aware that
it will generate a lot of output on the text console.

### Some tools that might be useful
#### apfs-dump-quick

If you encounter problems with some file, it may be that I overlooked something during reverse engineering. In that
case, you can use the `apfs-dump-quick` command to dump the management structures of the whole drive. It can be run
as follows:
```
apfs-dump-quick <drive> <logfile.txt>
```
The tool will dump the most current version of the disk structures into a logfile. This file can become quite big, like
a few 100 MB. So to limit the amount of information to report, look for the name of the file in the log. Try to find a
line starting with `File` and containing the filename. The number immediately after `File` is the ID. Find all lines
having this ID, and include them in your bug report.
#### apfs-dump
There is another command available:
```
apfs-dump <drive> <logfile.txt>
```
This tool was the one I originally used for reverse engineering. It will scan the whole volume for clusters having
correct checksums (and thus being part of some management structure), and then it will try to dump them. This will
take a very long time to run on big volumes, and create huge log files. So using the quick version will be much faster.
#### apfsutil
```
apfsutil <device>
```
This is a new tool that just displays some information from a container. For now, it lists the volumes a container
contains, and snapshots if there are some. This tool might be extended in the future.
