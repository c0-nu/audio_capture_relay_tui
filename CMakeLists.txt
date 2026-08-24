cmake_minimum_required(VERSION 3.16)
project(audio_capture_relay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ACR_BUILD_APP "本体(PulseAudio / ncurses が要る)をビルドする" ON)
option(ACR_BUILD_TESTS "domain のテストをビルドする(本体とは独立)" OFF)

find_package(Threads REQUIRED)

# domain/ … 純粋な計算。標準ライブラリ以外に依存しない。
# ここを独立したライブラリにしてあるから、PulseAudio も ncurses も無い環境で
# テストだけ回せる。この性質を壊さないこと。
set(DOMAIN_SOURCES
    src/domain/drift_control.cpp
    src/domain/error_log.cpp
    src/domain/level_meter.cpp
    src/domain/output_mix.cpp
    src/domain/pcm_ring.cpp
    src/domain/device_match.cpp
    src/domain/splice.cpp
    src/domain/text_util.cpp
    src/domain/wave_history.cpp
    src/domain/waveform.cpp
)

add_library(acr_domain STATIC ${DOMAIN_SOURCES})
target_include_directories(acr_domain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(acr_domain PUBLIC Threads::Threads)
target_compile_options(acr_domain PRIVATE -Wall -Wextra -Wpedantic)

if(ACR_BUILD_APP)
    find_package(PkgConfig REQUIRED)

    pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET
        libpulse
        libpulse-simple
    )

    pkg_check_modules(NCURSESW REQUIRED IMPORTED_TARGET ncursesw)

    # adapters/ … 副作用(PulseAudio / ncurses / 標準出力)
    set(ADAPTER_SOURCES
        src/adapters/plain_status.cpp
        src/adapters/pulse_capture.cpp
        src/adapters/pulse_playback.cpp
        src/adapters/pulse_device_lister.cpp
        src/adapters/tui_ncurses.cpp
    )

    # app/ … CLI と組み立て
    set(APP_SOURCES
        src/app/options.cpp
        src/app/signal_handling.cpp
        src/app/device_cli.cpp
    )

    add_executable(audio_capture_relay
        src/main.cpp
        ${ADAPTER_SOURCES}
        ${APP_SOURCES}
    )

    target_link_libraries(audio_capture_relay PRIVATE
        acr_domain
        PkgConfig::PULSE
        PkgConfig::NCURSESW
    )

    target_compile_options(audio_capture_relay PRIVATE
        -Wall -Wextra -Wpedantic
    )
endif()

if(ACR_BUILD_TESTS)
    # システムに入っていれば使い、無ければ取ってくる。
    find_package(Catch2 3 QUIET)
    if(NOT Catch2_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG v3.7.1
        )
        FetchContent_MakeAvailable(Catch2)
        list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
    endif()

    add_executable(acr_tests
        tests/test_drift_control.cpp
        tests/test_error_log.cpp
        tests/test_level_meter.cpp
        tests/test_output_mix.cpp
        tests/test_pcm_ring.cpp
        tests/test_device_match.cpp
        tests/test_splice.cpp
        tests/test_text_util.cpp
        tests/test_waveform.cpp
    )

    target_link_libraries(acr_tests PRIVATE acr_domain Catch2::Catch2WithMain)
    target_compile_options(acr_tests PRIVATE -Wall -Wextra -Wpedantic)

    include(CTest)
    include(Catch)
    catch_discover_tests(acr_tests)
endif()
