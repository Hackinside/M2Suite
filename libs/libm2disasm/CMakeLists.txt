add_library(libm2disasm STATIC
    src/Elf.cpp
    src/Pseudocode.cpp
    third_party/ppcd.cpp
)
add_library(m2suite::libm2disasm ALIAS libm2disasm)

target_include_directories(libm2disasm PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(libm2disasm PUBLIC m2suite::libm2core)
target_compile_features(libm2disasm PUBLIC cxx_std_17)

# third_party/ppcd.* is the ppcd PowerPC disassembler (CC0 public domain,
# https://github.com/ogamespec/ppcd). Compiled as-is with warnings off.
if (MSVC)
    # /Zc:strictStrings- : ppcd uses pre-C++11 `char*` tables initialized
    # from string literals; permit that for this vendored file only.
    set_source_files_properties(third_party/ppcd.cpp
        PROPERTIES COMPILE_OPTIONS "/w;/Zc:strictStrings-")
endif()
