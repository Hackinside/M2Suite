add_library(libm2stream STATIC
    src/Stream.cpp
    third_party/cinepak_decode.cpp
)
add_library(m2suite::libm2stream ALIAS libm2stream)

target_include_directories(libm2stream PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(libm2stream PUBLIC m2suite::libm2core)
target_compile_features(libm2stream PUBLIC cxx_std_17)

# third_party/cinepak_decode.cpp is Dr. Tim Ferguson's Cinepak decoder
# ("You may freely use this source code" with attribution — see the file
# header). Compiled as-is; suppress its stylistic warnings only.
if (MSVC)
    set_source_files_properties(third_party/cinepak_decode.cpp
        PROPERTIES COMPILE_OPTIONS "/w")
endif()
