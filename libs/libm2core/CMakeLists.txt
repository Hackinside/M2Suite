add_library(libm2core STATIC
    src/ByteStream.cpp
    src/Iff.cpp
)
add_library(m2suite::libm2core ALIAS libm2core)

target_include_directories(libm2core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(libm2core PUBLIC cxx_std_17)
