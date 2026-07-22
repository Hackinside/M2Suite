add_library(libm2texture STATIC
    src/Texture.cpp
    src/TextureEncoder.cpp
)
add_library(m2suite::libm2texture ALIAS libm2texture)

target_include_directories(libm2texture PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(libm2texture PUBLIC m2suite::libm2core)
target_compile_features(libm2texture PUBLIC cxx_std_17)
