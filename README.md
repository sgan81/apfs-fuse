# APFS FUSE Driver

This project is a read-only FUSE driver for the new Apple File System.

## Usage

### Compile the source code
The following libraries are needed:

* FUSE 2.6 or greater
* ICU

```
mkdir build
cd build
cmake ..
make
```
After compilation, the binaries are located in `bin`.

### Mount a drive
```
apfs-fuse <device> <mount-directory>
```
Supported options:
* `-d n`: If n > 0, enable debug output.
* `-o opts`: Comma-separated list of mount options.
* `-v n`: Instead of mounting the first volume in a container, mount volume n (starting at 0).

The device has to be the one containing the APFS container. If a container contains more than one volume,
the volume can be specified by the `-v` option.

### Unmount a drive
```
fusermount -u <mount-directory>
```

## Features
The following features are implemented:

* Can read macOS 10.13 case sensitive and insensitive volumes, as well as iOS 11 / macOS 10.12 volumes
* Transparent decompression of ZLib and LZVN
* Extended attributes

## Limitations
These things are not supported (yet):

* Encryption
* Transparent decompression of LZFSE
* Writing

## Final Words

**This driver is experimental**

Please be aware that this code is based on reverse engineering (looking at disk images in a hex-editor), since
Apple didn't document the disk format yet. The driver is read-only, so your data should be safe. However, there
may still be quite a few bugs left, including potential crashes. So use it at your own risk.
