find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

qt_standard_project_setup()

set(CMAKE_AUTORCC ON)

# --- Auto-incrementing build number ------------------------------------
# A custom target with no output is always considered out-of-date, so the
# increment script runs on every build; it bumps build_number.txt and
# regenerates BuildInfo.h in the build tree. m2suite-shell depends on it,
# and Version.cpp (the only TU that includes BuildInfo.h) recompiles when
# the number changes — see cmake/increment_build.cmake.
set(M2SUITE_BUILD_INFO_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${M2SUITE_BUILD_INFO_DIR}")
add_custom_target(m2suite-bump-build ALL
    COMMAND ${CMAKE_COMMAND}
            -DCOUNTER_FILE=${CMAKE_CURRENT_SOURCE_DIR}/build_number.txt
            -DOUTPUT_HEADER=${M2SUITE_BUILD_INFO_DIR}/BuildInfo.h
            -P ${CMAKE_SOURCE_DIR}/cmake/increment_build.cmake
    BYPRODUCTS ${M2SUITE_BUILD_INFO_DIR}/BuildInfo.h
    COMMENT "Incrementing M2Suite build number"
    VERBATIM
)

add_executable(m2suite-shell
    resources/m2suite.qrc
    resources/m2suite.rc
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/FileTypes.cpp
    src/FileTypes.h
    src/ImageViewport.cpp
    src/ModelViewport.cpp
    src/ModelViewport.h
    src/ImageViewport.h
    src/WaveformView.cpp
    src/WaveformView.h
    src/Version.cpp
    src/Version.h
)
add_dependencies(m2suite-shell m2suite-bump-build)
target_include_directories(m2suite-shell PRIVATE "${M2SUITE_BUILD_INFO_DIR}")

target_link_libraries(m2suite-shell PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    m2suite::libm2core
    m2suite::libm2texture
    m2suite::libm2cel
    m2suite::libm2audio
    m2suite::libm2stream
    m2suite::libm2disasm
    m2suite::libm2disc
    m2suite::libm2model
)

set_target_properties(m2suite-shell PROPERTIES
    WIN32_EXECUTABLE ON
)

# vcpkg's applocal step copies Qt's core DLLs beside the exe but not Qt
# *plugins*, and vcpkg's qtbase doesn't ship windeployqt — without
# platforms/qwindows.dll the app dies at startup with "no Qt platform
# plugin could be initialized". Deploy the platform plugin (and the
# Windows style plugin so widgets look native) ourselves.
if (WIN32 AND TARGET Qt6::QWindowsIntegrationPlugin)
    add_custom_command(TARGET m2suite-shell POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:m2suite-shell>/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>"
                "$<TARGET_FILE_DIR:m2suite-shell>/platforms/"
        COMMENT "Deploying Qt platform plugin (qwindows)"
    )
endif()
# Multimedia backend plugins — without multimedia/ffmpegmediaplugin.dll
# beside the exe, QMediaPlayer has NO backend and MPEG playback fails
# silently ("No QtMultimedia backends found" in m2suite.log).
foreach(mmplugin QFFmpegMediaPlugin QWindowsMediaPlugin)
    if (WIN32 AND TARGET Qt6::${mmplugin})
        add_custom_command(TARGET m2suite-shell POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_FILE_DIR:m2suite-shell>/multimedia"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:Qt6::${mmplugin}>"
                    "$<TARGET_FILE_DIR:m2suite-shell>/multimedia/"
            COMMENT "Deploying Qt multimedia plugin (${mmplugin})"
        )
    endif()
endforeach()

# Plugins load their own transitive DLLs at runtime; vcpkg's applocal step
# only walks the exe's direct imports, so copy them explicitly or the
# plugin fails to load (ffmpeg backend, or qjpeg needing jpeg62.dll — which
# is why upload_bg.jpg reported "missing image plugin").
if (WIN32)
    file(GLOB M2SUITE_RUNTIME_DLLS
         "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/bin/av*.dll"
         "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/bin/sw*.dll"
         "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/bin/jpeg*.dll"
         "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/bin/turbojpeg*.dll")
    if (M2SUITE_RUNTIME_DLLS)
        add_custom_command(TARGET m2suite-shell POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${M2SUITE_RUNTIME_DLLS}
                    "$<TARGET_FILE_DIR:m2suite-shell>"
            COMMENT "Deploying plugin runtime DLLs (ffmpeg, jpeg)"
        )
    endif()
endif()

# Image-format plugins (JPEG/GIF/ICO) so QImage can open Oldsmobile's
# GIF/JPG content; PNG support is built into Qt6Gui.
foreach(imgplugin QJpegPlugin QGifPlugin QICOPlugin)
    if (WIN32 AND TARGET Qt6::${imgplugin})
        add_custom_command(TARGET m2suite-shell POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_FILE_DIR:m2suite-shell>/imageformats"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:Qt6::${imgplugin}>"
                    "$<TARGET_FILE_DIR:m2suite-shell>/imageformats/"
            COMMENT "Deploying Qt image plugin (${imgplugin})"
        )
    endif()
endforeach()

if (WIN32 AND TARGET Qt6::QModernWindowsStylePlugin)
    add_custom_command(TARGET m2suite-shell POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:m2suite-shell>/styles"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::QModernWindowsStylePlugin>"
                "$<TARGET_FILE_DIR:m2suite-shell>/styles/"
        COMMENT "Deploying Qt Windows style plugin"
    )
endif()
