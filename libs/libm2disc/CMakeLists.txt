add_library(libm2disc STATIC
    src/OperaDisc.cpp
)
add_library(m2suite::libm2disc ALIAS libm2disc)

target_include_directories(libm2disc PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(libm2disc PUBLIC cxx_std_17)
