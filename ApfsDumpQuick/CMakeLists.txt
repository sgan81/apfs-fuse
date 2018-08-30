add_executable(apfs-dump-quick ApfsDumpQuick.cpp)
target_link_libraries(apfs-dump-quick ApfsLib z bz2 lzfse)
