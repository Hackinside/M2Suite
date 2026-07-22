add_library(libm2cel STATIC
    src/Cel.cpp
    src/Anim.cpp
    src/Imag.cpp
)
add_library(m2suite::libm2cel ALIAS libm2cel)

target_include_directories(libm2cel PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# m2texture for the shared Rgba8 pixel type.
target_link_libraries(libm2cel PUBLIC m2suite::libm2core m2suite::libm2texture)
target_compile_features(libm2cel PUBLIC cxx_std_17)
