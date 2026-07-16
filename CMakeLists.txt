cmake_minimum_required(VERSION 3.16)
project(audio_capture_relay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)

pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET
    libpulse
    libpulse-simple
)

pkg_check_modules(NCURSESW REQUIRED IMPORTED_TARGET ncursesw)

add_executable(audio_capture_relay
    src/main.cpp
)

target_link_libraries(audio_capture_relay PRIVATE
    PkgConfig::PULSE
    PkgConfig::NCURSESW
)

target_compile_options(audio_capture_relay PRIVATE
    -Wall -Wextra -Wpedantic
)
