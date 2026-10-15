# Metadata inventory tests

Run these commands from the repository root after installing the normal build dependencies:

```
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build
ctest --test-dir build --output-on-failure
```

CMake 4 needs `CMAKE_POLICY_VERSION_MINIMUM` because the project declares an older minimum version.
If GCC reports that `uint8_t` is undeclared in `PList.h`, add `'-DCMAKE_CXX_FLAGS=-include cstdint'` to the CMake command.
This compiler workaround supplies the header proposed in [upstream PR #205](https://github.com/sgan81/apfs-fuse/pull/205).
The tests use C++ and CMake without additional dependencies.
Set `BUILD_TESTING=OFF` to exclude the test executables from a build.

`Inventory.cpp` creates synthetic metadata in memory and writes test images under the build directory for the CLI tests.
Each image contains twelve 4096-byte blocks with structures from `ApfsLib/DiskStruct.h` and calculated Fletcher checksums.
The images contain no private disk data, file contents, credentials, or volume keys.
They provide metadata for inventory tests, not complete filesystems for mounting.

The fixture contains a container superblock, checkpoint map, object map, space manager, two volume superblocks, and snapshot metadata.
An empty volume slot separates volumes named `Data` and `System`, whose flags indicate encrypted and unencrypted storage respectively.
A keylocker with an invalid checksum causes normal initialization to fail, while metadata-only initialization skips the keylocker entirely.
The in-memory device records reads so the tests can detect unwanted keylocker or volume access.

The suite covers:

- Rejection of iteration without a root and removal of stale iterator state.
- Normal initialization with valid, absent, and invalid keylockers.
- Metadata inventory with the same invalid keylocker that prevents normal initialization.
- Rejection of volume access, snapshot mounting, encrypted reads, key retrieval, and key dumps in metadata mode.
- Rejection of invalid container magic and checksum failures in the container, checkpoint map, object map, and space manager.
- Partial results for missing or damaged volume and snapshot metadata, including a failure after the first snapshot.
- Rejection of short snapshot keys, missing or short values, and invalid snapshot names.
- Rejection of invalid volume superblock types and bounded output for unterminated volume names.
- Empty snapshot trees, encrypted-volume flags, argument errors, help output, and default invocation.
- Rejection of encrypted image headers without password prompts in metadata-only mode.

The `inventory` test exercises error handling with normal initialization.
The `metadata-only` test exercises the access restrictions and repeats integrity tests with the key manager disabled.

These synthetic tests do not establish compatibility with any particular Apple Silicon disk or explain hardware key-manager failures.
Hardware testing requires a separate read-only inventory of the target device.
