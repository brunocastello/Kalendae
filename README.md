# Kalendae - Classic Mac OS 9 Calendar Application

A native Mac OS 9 Calendar application designed for PowerPC hardware that mimics the modern multi-view interface of Apple Calendar while strictly adhering to the Mac OS 9 Platinum Theme.

## Overview

Kalendae is a vintage Mac OS 9 application that provides a modern calendar experience within the constraints of Classic Mac OS. It features:

- **Multi-view Interface**: Day, Week, Month, and Year view modes
- **Platinum Theme**: Authentic Mac OS 9 styling with Geneva typography
- **100% Offline Operation**: No external dependencies or network services
- **iCalendar (.ics) Support**: Parse and export calendar data using standard `.ics` format
- **Recurrence Handling**: Local evaluation of `RRULE` repeat patterns
- **Notification Integration**: Uses Mac OS 9 Notification Manager for alerts

## Features

1. **Multi-View Rendering** using custom Offscreen Graphics Worlds (`GWorld`) to prevent visual flicker
2. **Color-Coded Categorization** for different calendar types
3. **Interactive Event Creation** with popover inspectors
4. **Local Preferences Storage** for calendar settings and events
5. **Recurrence Engine** for handling repeating events
6. **Notification System** for event reminders

## Technical Requirements

- Mac OS 9 Platinum Theme
- PowerPC hardware (no Intel support)
- Classic (pre-Carbon) Mac Toolbox APIs
- Retro68 toolchain for compilation

## Build Instructions

Builds run exclusively in GitHub Actions (see `.github/workflows/build-macos9.yml`), using the
prebuilt `ghcr.io/autc04/retro68` toolchain container — there is no supported local build. To
reproduce it yourself with Docker:

```bash
docker run --rm -v $(pwd):/root/Kalendae -w /root/Kalendae ghcr.io/autc04/retro68 /bin/bash -c '
  mkdir -p build && cd build &&
  cmake .. -DCMAKE_BUILD_TYPE=Release \
           -DCMAKE_TOOLCHAIN_FILE=/Retro68-build/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake &&
  make
'
```

### Build Artifacts

- `Kalendae.bin` - MacBinary-encoded application (single-fork transfer format)
- `Kalendae.APPL` - AppleSingle-encoded application
- `Kalendae.dsk` - Bootable floppy disk image containing the application
- `.ics` files for calendar data exchange
- Preferences stored in `System Folder:Preferences`

## File Structure

```
Kalendae/
├── CMakeLists.txt           # CMake build configuration (Retro68 add_application)
├── README.md                # This file
├── CLAUDE.md                # Project documentation
├── PROJECT.md               # Technical specification
├── .gitignore               # Git ignore rules
├── .github/
│   └── workflows/
│       └── build-macos9.yml  # GitHub Actions workflow
├── Resources/
│   └── Kalendae.r          # Rez resource file (SIZE resource)
├── src/
│   ├── main.cpp            # Main application entry point
│   ├── calendar.cpp        # Calendar management
│   ├── ical_parser.cpp     # iCalendar file parser
│   ├── ui_renderer.cpp     # UI rendering engine
│   ├── event.cpp           # Event handling
│   └── recurrence.cpp      # Recurrence rule evaluation
├── include/
│   ├── calendar.h          # Calendar header
│   ├── event.h             # Event header
│   ├── ical_parser.h       # iCalendar parser header
│   ├── ui_renderer.h       # UI renderer header
│   └── recurrence.h        # Recurrence engine header
└── LAYOUT.jpg              # UI layout reference (ignored by git)
```

## Development Notes

This project targets PowerPC Mac OS 9 systems using the Retro68 toolchain. The application is designed to be 100% offline with no external dependencies.

The code base implements the core architecture outlined in PROJECT.md, featuring:
- Multi-view calendar rendering (Day, Week, Month, Year)
- iCalendar format parsing and export
- Recurrence rule evaluation in memory
- Classic (pre-Carbon) Mac Toolbox APIs for Mac OS 9 compatibility
- QuickDraw offscreen rendering with GWorld for anti-flicker effects

## License

This project is intended as a demonstration of Classic Mac OS development principles. See LICENSE for more information.

## Contact

For questions about this project, refer to the project documentation.