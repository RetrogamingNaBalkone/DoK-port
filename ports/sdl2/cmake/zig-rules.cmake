# Applied after the platform defaults, also inside CMake's compiler probes.
set(CMAKE_CXX_VERBOSE_LINK_FLAG "") # Zig's COFF linker rejects GNU ld -v.
set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> ar qc <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_CXX_ARCHIVE_APPEND "<CMAKE_AR> ar q <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_CXX_ARCHIVE_FINISH "<CMAKE_RANLIB> ranlib <TARGET>")
