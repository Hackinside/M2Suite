add_library(libm2model STATIC
    src/AitdPak.cpp
    src/AitdBody.cpp
    src/AitdPalette.cpp
    src/AitdRender.cpp
    src/AitdImage.cpp
    third_party/pak_explode.cpp
)
add_library(m2suite::libm2model ALIAS libm2model)

target_include_directories(libm2model PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(libm2model PUBLIC cxx_std_17)

# Mark Adler's explode code (vendored, pre-C89-ish): silence its warnings.
if (MSVC)
    set_source_files_properties(third_party/pak_explode.cpp PROPERTIES COMPILE_OPTIONS "/w")
endif()
