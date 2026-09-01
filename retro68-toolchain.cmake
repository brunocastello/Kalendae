# CMake toolchain file for Retro68 cross-compilation to PowerPC Mac OS 9

# Target system
set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR powerpc)

# Toolchain paths - these would need to be adjusted based on the Retro68 installation
set(CMAKE_C_COMPILER /usr/bin/retro68-gcc)
set(CMAKE_CXX_COMPILER /usr/bin/retro68-g++)

# Set compiler flags for Mac OS 9
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcpu=7450 -maltivec -fPIC -O2")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mcpu=7450 -maltivec -fPIC -O2")

# Set the target architecture to PowerPC
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -arch ppc")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -arch ppc")

# Set the application type to APPL with Mac OS 9 creator code
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-sectcreate,__INFO_PLIST,Info.plist")

# Find required frameworks (these may need to be adjusted for Retro68)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

# Carbon framework
find_library(CARBON_FRAMEWORK Carbon)

# Set the executable type
set_target_properties(Kalendae PROPERTIES
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/Resources/Info.plist"
)