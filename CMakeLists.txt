# CMakeLists.txt for Kalendae - Classic Mac OS 9 Calendar Application
#
# This project only builds via the Retro68 cross-compilation toolchain
# (see .github/workflows/build-macos9.yml). Configure with:
#   cmake .. -DCMAKE_BUILD_TYPE=Release \
#            -DCMAKE_TOOLCHAIN_FILE=<retro68-prefix>/powerpc-apple-macos/cmake/retroppc.toolchain.cmake

cmake_minimum_required(VERSION 3.10)
project(Kalendae CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_application(Kalendae
    src/main.cpp
    src/calendar.cpp
    src/ical_parser.cpp
    src/ui_renderer.cpp
    src/event.cpp
    src/recurrence.cpp
    Resources/Kalendae.r
    CREATOR "KALE"
)
