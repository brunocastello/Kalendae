# CLAUDE.md - Kalendae (Mac OS 9 Calendar Clone)

## Project Overview
This repository contains **Kalendae**, a native Classic Mac OS Calendar application designed for PowerPC hardware[cite: 13, 14, 15]. It mimics the modern multi-view interface of Apple Calendar while strictly adhering to the **Mac OS 9 Platinum Theme** and running **100% offline** via local `.ics` file imports and exports[cite: 13, 14, 15].

## Reference Specification
* **Detailed Blueprint & Architecture:** For complete engineering specifications, data flow diagrams, `.ics` parsing logic, and milestone estimates, see [PROJECT.md](./PROJECT.md).

## Build & Compilation Rules (GitHub Actions Only)
* **No Local Compilation:** This project is never compiled locally. All builds execute exclusively inside automated **GitHub Actions** workflows[cite: 13, 14, 15].
* **Toolchain:** The build environment relies on the prebuilt **Retro68** toolchain container (`ghcr.io/autc04/retro68`) running inside GitHub Actions. It uses Retro68's bundled open-source **Multiversal Interfaces** rather than Apple's Universal Interfaces, since Apple no longer distributes the latter and its license does not permit redistribution — meaning it cannot be auto-fetched in CI[cite: 13, 14, 15].
* **CMake & Make Configuration:** 
  * Configure: `cmake .. -DCMAKE_BUILD_TYPE=Release`[cite: 13, 14, 15]
  * Compile: `make`[cite: 13, 14, 15]
* **Target Output:** Standard Mac OS application binary with resource forks (`APPL` type, custom creator code) generated as a CI build artifact[cite: 13, 14, 15].

## Technical Guidelines & Constraints
1. **Target Environment:** Classic Mac OS (System 7–9) targeting PowerPC architectures using the Retro68 toolchain and the classic (pre-Carbon) Mac Toolbox APIs — Multiversal Interfaces do not implement Carbon[cite: 13, 14, 15].
2. **UI & Theme:** Implement authentic Mac OS 9 Platinum controls (Control Manager buttons/checkboxes, List Manager for tabular data) and Geneva/system typography. Drawing goes directly to each window's own port rather than through an offscreen `GWorld` -- nothing animates, so there's no flicker to double-buffer against, and a GWorld doubles memory pressure in the tiny classic-Mac partition for no benefit here[cite: 13, 14, 15].
3. **Data Management:** 
   * Zero external cloud services or network dependencies[cite: 13, 14, 15].
   * Parse standard `.ics` text files (`VCALENDAR` and `VEVENT` data structures)[cite: 13, 14, 15].
   * Store and read structured event preferences locally inside `System Folder:Preferences`[cite: 13, 14, 15].
4. **Core Subsystems:**
   * **Multi-View Engine:** Switchable Day, Week, Month, and Year view canvases[cite: 13, 14, 15].
   * **Recurrence Engine:** Local in-memory evaluation of `RRULE` repeat patterns[cite: 13, 14, 15].
   * **Notifications:** Integration with the Mac OS Notification Manager and Time Manager for background reminders and alerts[cite: 13, 14, 15].

## Roadmap

Living checklist -- check items off as they land, and add new ones as scope
is discovered. Update this section whenever a roadmap item is completed or
a new one is identified, so it stays trustworthy as the single source of
truth for "what's left."

### Done
- [x] CI build pipeline on Retro68 + Multiversal Interfaces, GitHub Actions only
- [x] Menu bar: Apple menu, File > Quit, View (Day/Week/Month/Year, Cmd-1..4)
- [x] Toolbar: Day/Week/Month/Year buttons, centered
- [x] Sidebar: calendar category list (real checkboxes, from `Calendar::GetCategories()`)
- [x] Mini-calendar navigator in the sidebar: prev/next month, click a day to jump to Day view
- [x] Month view: real List Manager grid with day numbers + colored event chips
- [x] Day view: full date header + event list (List Manager)
- [x] Week view: 7-day columns with event chips
- [x] Year view: 4x3 grid of mini-months
- [x] Window dragging, resizing (grow box + `GrowWindow`/`SizeWindow`), draggable sidebar splitter (iTunes-2-style, clamped min/max width)
- [x] Launches at 800x600 centered on the screen
- [x] Preferences persistence: window size/position and sidebar width survive relaunch (`System Folder:Preferences`)

### Next up (roughly priority order)
- [ ] **Event creation & editing UI** -- double-click a day/slot to open a Dialog Manager form (title, start/end date, category, location, notes). Highest priority: right now the app can only display the 3 hardcoded sample events: there's no way to add your own.
- [ ] **Event deletion** -- `Calendar::RemoveEvent` exists but nothing in the UI calls it.
- [ ] **iCalendar (.ics) import/export wired to the UI** -- `ICALParser::ParseFile`/`ExportCalendar` exist but are never called; needs File menu items + `StandardFile` (`StandardGetFile`/`StandardPutFile`) open/save dialogs.
- [ ] **Sidebar checkbox filtering** -- unchecking a calendar category is currently cosmetic only; it should hide that category's events from every view.
- [ ] **"Today" button** -- jump back to the current date (mockup has one next to the mini-calendar; we only have prev/next month arrows today).
- [ ] **Recurrence evaluation** -- `RecurrenceEngine::EvaluateRecurrence` exists but nothing expands a stored `RRULE` into the actual repeated instances shown in any view.
- [ ] **Reminders/Notifications** -- `Calendar::CheckForUpcomingReminders` is an empty stub; needs real Notification Manager + Time Manager integration.
- [ ] **Find/Search** -- mockup has a magnifying-glass button; no search exists yet.
- [ ] **Persist actual event data** across launches, not just window chrome -- events are hardcoded fresh in `Calendar::LoadFromPreferences` every run today; real persistence probably means writing/reading `.ics` to/from the Preferences folder, once import/export exists.