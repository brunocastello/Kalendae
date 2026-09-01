# CLAUDE.md - Kalendae (Mac OS 9 Calendar Clone)

## Project Overview
This repository contains **Kalendae**, a native Classic Mac OS Calendar application designed for PowerPC hardware[cite: 13, 14, 15]. It mimics the modern multi-view interface of Apple Calendar while strictly adhering to the **Mac OS 9 Platinum Theme** and running **100% offline** via local `.ics` file imports and exports[cite: 13, 14, 15].

## Reference Specification
* **Detailed Blueprint & Architecture:** For complete engineering specifications, data flow diagrams, `.ics` parsing logic, and milestone estimates, see [PROJECT.md](./PROJECT.md).

## Build & Compilation Rules (GitHub Actions Only)
* **No Local Compilation:** This project is never compiled locally. All builds execute exclusively inside automated **GitHub Actions** workflows[cite: 13, 14, 15].
* **Toolchain:** The build environment relies on **Retro68** and **Apple Universal Headers** running inside a containerized or automated CI pipeline[cite: 13, 14, 15].
* **CMake & Make Configuration:** 
  * Configure: `cmake .. -DCMAKE_BUILD_TYPE=Release`[cite: 13, 14, 15]
  * Compile: `make`[cite: 13, 14, 15]
* **Target Output:** Standard Mac OS application binary with resource forks (`APPL` type, custom creator code) generated as a CI build artifact[cite: 13, 14, 15].

## Technical Guidelines & Constraints
1. **Target Environment:** Classic Mac OS / Carbon targeting PowerPC architectures using the Retro68 toolchain[cite: 13, 14, 15].
2. **UI & Theme:** Implement authentic Mac OS 9 Platinum controls, Geneva typography, drop shadows, and anti-flicker rendering via QuickDraw Offscreen Graphics Worlds (`GWorld`)[cite: 13, 14, 15].
3. **Data Management:** 
   * Zero external cloud services or network dependencies[cite: 13, 14, 15].
   * Parse standard `.ics` text files (`VCALENDAR` and `VEVENT` data structures)[cite: 13, 14, 15].
   * Store and read structured event preferences locally inside `System Folder:Preferences`[cite: 13, 14, 15].
4. **Core Subsystems:**
   * **Multi-View Engine:** Switchable Day, Week, Month, and Year view canvases[cite: 13, 14, 15].
   * **Recurrence Engine:** Local in-memory evaluation of `RRULE` repeat patterns[cite: 13, 14, 15].
   * **Notifications:** Integration with the Mac OS Notification Manager and Time Manager for background reminders and alerts[cite: 13, 14, 15].