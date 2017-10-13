add_executable(apfs-fuse ApfsFuse.cpp)
target_link_libraries(apfs-fuse ApfsLib fuse icuuc)
target_compile_definitions(apfs-fuse PRIVATE _FILE_OFFSET_BITS=64)
