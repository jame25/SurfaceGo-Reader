cmake_minimum_required(VERSION 3.21)
project(surfacego-reader VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Concurrent)
find_package(PkgConfig REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(POPPLER REQUIRED IMPORTED_TARGET poppler-qt6)
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse-simple)

# eSpeak NG (phonemizer used by Piper voices)
find_path(ESPEAK_INCLUDE_DIR espeak-ng/speak_lib.h REQUIRED)
find_library(ESPEAK_LIBRARY NAMES espeak-ng REQUIRED)

# ONNX Runtime (runs the Piper .onnx voice models). Arch: pacman -S onnxruntime-cpu
pkg_check_modules(ONNXRUNTIME QUIET IMPORTED_TARGET libonnxruntime)
if(NOT ONNXRUNTIME_FOUND)
    find_path(ORT_INCLUDE_DIR onnxruntime_cxx_api.h
        PATH_SUFFIXES onnxruntime onnxruntime/core/session REQUIRED)
    find_library(ORT_LIBRARY NAMES onnxruntime REQUIRED)
    add_library(ort_fallback INTERFACE)
    target_include_directories(ort_fallback INTERFACE ${ORT_INCLUDE_DIR})
    target_link_libraries(ort_fallback INTERFACE ${ORT_LIBRARY})
    set(ORT_TARGET ort_fallback)
else()
    set(ORT_TARGET PkgConfig::ONNXRUNTIME)
endif()

qt_standard_project_setup(REQUIRES 6.5)
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

qt_add_executable(surfacego-reader
    src/main.cpp
    src/appsettings.h src/appsettings.cpp
    src/book.h src/book.cpp
    src/zipreader.h src/zipreader.cpp
    src/htmltext.h src/htmltext.cpp
    src/epubloader.h src/epubloader.cpp
    src/pdfloader.h src/pdfloader.cpp
    src/phonemizer.h src/phonemizer.cpp
    src/pipervoice.h src/pipervoice.cpp
    src/wordtiming.h src/wordtiming.cpp
    src/segment.h
    src/synthworker.h src/synthworker.cpp
    src/audiooutput.h src/audiooutput.cpp
    src/librarymodel.h src/librarymodel.cpp
    src/voicemodel.h src/voicemodel.cpp
    src/paragraphmodel.h src/paragraphmodel.cpp
    src/reader.h src/reader.cpp
)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(surfacego-reader
    URI SGReader
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/LibraryPage.qml
        qml/ReaderPage.qml
        qml/SettingsPage.qml
        qml/FolderPicker.qml
        qml/TouchButton.qml
        qml/PillButton.qml
        qml/VoicePanel.qml
        qml/Theme.qml
    RESOURCES
        qml/icons/back.svg qml/icons/book.svg qml/icons/folder.svg qml/icons/follow.svg qml/icons/locate.svg qml/icons/next.svg qml/icons/pause.svg qml/icons/play.svg qml/icons/previous.svg qml/icons/refresh.svg qml/icons/settings.svg qml/icons/sort.svg qml/icons/speed.svg qml/icons/stop.svg qml/icons/toc.svg qml/icons/up.svg qml/icons/voice.svg qml/icons/wrap.svg
)

target_compile_definitions(surfacego-reader PRIVATE
    APP_VERSION="${PROJECT_VERSION}")

target_include_directories(surfacego-reader PRIVATE ${ESPEAK_INCLUDE_DIR})

target_link_libraries(surfacego-reader PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent
    PkgConfig::POPPLER
    PkgConfig::PULSE
    ${ESPEAK_LIBRARY}
    ${ORT_TARGET}
    ZLIB::ZLIB
    Threads::Threads)

include(GNUInstallDirs)
install(TARGETS surfacego-reader RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/surfacego-reader.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/surfacego-reader.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

# Small command-line test driver for the non-GUI pipeline (not installed).
option(SGREADER_BUILD_TESTS "Build the pipeline test tool" ON)
if(SGREADER_BUILD_TESTS)
    qt_add_executable(sgreader-selftest
        tests/selftest.cpp
        src/book.cpp src/zipreader.cpp src/htmltext.cpp src/epubloader.cpp src/pdfloader.cpp
        src/phonemizer.cpp src/pipervoice.cpp src/wordtiming.cpp)
    target_include_directories(sgreader-selftest PRIVATE src ${ESPEAK_INCLUDE_DIR})
    target_link_libraries(sgreader-selftest PRIVATE Qt6::Core Qt6::Gui PkgConfig::POPPLER
        ${ESPEAK_LIBRARY} ${ORT_TARGET} ZLIB::ZLIB)
endif()
