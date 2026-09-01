# CMakeLists.txt for Kalendae - Classic Mac OS 9 Calendar Application

cmake_minimum_required(VERSION 3.10)
project(Kalendae VERSION 1.0.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set target platform to Mac OS 9 / PowerPC
set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR powerpc)

# Set compiler flags for Mac OS 9
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcpu=7450 -maltivec -fPIC -O2")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mcpu=7450 -maltivec -fPIC -O2")

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Source files
set(SOURCES
    src/main.cpp
    src/calendar.cpp
    src/ical_parser.cpp
    src/ui_renderer.cpp
    src/event.cpp
    src/recurrence.cpp
)

# Create the executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Set the application type and creator code for Mac OS 9
set_target_properties(${PROJECT_NAME} PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Resources/Info.plist"
)

# Link with Carbon framework
target_link_libraries(${PROJECT_NAME} "-framework Carbon")

# Set the executable type to APPL with custom creator code
set_property(TARGET ${PROJECT_NAME} PROPERTY XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "")
set_property(TARGET ${PROJECT_NAME} PROPERTY XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "")