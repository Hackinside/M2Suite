add_library(libm2audio STATIC
    src/Aiff.cpp
    src/Wav.cpp
    src/Sdx2.cpp
    src/Encoder.cpp
)
add_library(m2suite::libm2audio ALIAS libm2audio)

target_include_directories(libm2audio PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(libm2audio PUBLIC m2suite::libm2core)
target_compile_features(libm2audio PUBLIC cxx_std_17)
