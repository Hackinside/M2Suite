add_executable(libm2core_tests libm2core_tests/test_bytestream_iff.cpp)
target_link_libraries(libm2core_tests PRIVATE m2suite::libm2core)
add_test(NAME libm2core_tests COMMAND libm2core_tests)

add_executable(libm2texture_tests libm2texture_tests/test_texture_roundtrip.cpp)
target_link_libraries(libm2texture_tests PRIVATE m2suite::libm2texture)
add_test(NAME libm2texture_tests
         COMMAND libm2texture_tests
                 ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/close.utf
                 ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/roofs.utf)

add_executable(libm2cel_tests libm2cel_tests/test_cel_decode.cpp)
target_link_libraries(libm2cel_tests PRIVATE m2suite::libm2cel)
add_test(NAME libm2cel_tests
         COMMAND libm2cel_tests
                 ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/Message.cel
                 ""
                 ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample.anim
                 ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample.imag)

add_executable(libm2audio_tests libm2audio_tests/test_aiff_decode.cpp)
target_link_libraries(libm2audio_tests PRIVATE m2suite::libm2audio)
add_test(NAME libm2audio_tests
         COMMAND libm2audio_tests ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample.aiff)

# Encoder round-trips: synthesises its own signal, so no fixture needed.
add_executable(libm2audio_encoder_tests libm2audio_tests/test_encoder.cpp)
target_link_libraries(libm2audio_encoder_tests PRIVATE m2suite::libm2audio)
add_test(NAME libm2audio_encoder_tests COMMAND libm2audio_encoder_tests)

add_executable(libm2disasm_tests libm2disasm_tests/test_elf_disasm.cpp)
target_link_libraries(libm2disasm_tests PRIVATE m2suite::libm2disasm)
add_test(NAME libm2disasm_tests
         COMMAND libm2disasm_tests ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample.elf)

add_executable(libm2disc_tests libm2disc_tests/test_opera_disc.cpp)
target_link_libraries(libm2disc_tests PRIVATE m2suite::libm2disc)

# Manual tool: needs a real PAK off a disc, so it is built but not run by
# ctest. test_aitd_synth below covers the same code with synthetic inputs.
add_executable(libm2model_tests libm2model_tests/test_aitd_pak.cpp)
target_link_libraries(libm2model_tests PRIVATE m2suite::libm2model)

add_executable(libm2model_synth_tests libm2model_tests/test_aitd_synth.cpp)
target_link_libraries(libm2model_synth_tests PRIVATE m2suite::libm2model)
add_test(NAME libm2model_synth_tests COMMAND libm2model_synth_tests)

add_executable(libm2stream_tests libm2stream_tests/test_stream_decode.cpp)
target_link_libraries(libm2stream_tests PRIVATE m2suite::libm2stream m2suite::libm2audio)
add_test(NAME libm2stream_tests
         COMMAND libm2stream_tests
                 ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample.str
                 ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sample.stream)
