# APFS FUSE Driver for Linux

This project is a read-only FUSE driver for the new Apple File System. Since Apple didn't yet document
the disk format of APFS, this driver should be considered experimental... It may not be able to read all
files, it may return wrong data, or it may simply crash. Use at your own risk. But since it's read-only,
at least the data on your apfs drive should be safe.

Be aware that not all compression methods are supported yet (only the ones I have encountered so far).
Thus, the driver may return compressed files instead of uncompressed ones ...

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
* `-r recovery_key`: Mount an encrypted volume by supplying a Personal Recovery Key.
* `-s n`: Find the container at offset n inside the device. This is useful when using an image file
  instead of a disk device, and therefore partitions are not exposed.

The device has to be the one containing the APFS container. If a container contains more than one volume,
the volume can be specified by the `-v` option.

If a volume is encrypted, the apfs-fuse command will prompt for a password.

### Unmount a drive
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
* Encryption (at least full-disk encryption)

## Limitations
These things are not supported (yet):

* Transparent decompression of LZFSE
* Writing

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

There is another command available:
```
apfs-dump <drive> <logfile.txt>
```
This tool was the one I originally used for reverse engineering. It will scan the whole volume for clusters having
correct checksums (and thus being part of some management structure), and then it will try to dump them. This will
take a very long time to run on big volumes, and create huge log files. So using the quick version will be much faster.
