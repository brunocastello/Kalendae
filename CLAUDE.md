# CLAUDE.md — Kalendae (Mac OS 9 Calendar Clone)

## Project Goal
Build **Kalendae** as a near-perfect functional and visual clone of the classic Apple Calendar app (the clean Lion / Mavericks-era design), running natively on Mac OS 9 with the authentic **Platinum** theme.

- Exact same information architecture, views, sidebar behaviour, and interaction model as Apple Calendar.
- Zero iCloud, zero network, zero cloud sync.
- 100% offline. All data lives in local `.ics` files (Application Support) and Preferences.
- Strict native Mac OS 9 Platinum look and feel (no Aqua, no modern materials, no skeuomorphism beyond what Platinum already provides).

## Hard Constraints
- Target: Classic Mac OS 9 (PowerPC) using Retro68 + Multiversal Interfaces (pre-Carbon Toolbox only).
- Builds run **exclusively** on GitHub Actions (`ghcr.io/autc04/retro68`). No local compilation.
- 100% offline. No network calls of any kind.
- Data interchange only via standard local `.ics` (iCalendar) files.
- UI must feel like Apple Calendar transplanted into Platinum:
  - Same view modes (Day, Week, Month, Year)
  - Same sidebar with calendar list + checkboxes + mini-month
  - Same toolbar / view switcher behaviour
  - Same event creation / editing flow (double-click, inspector-style dialog)
  - Same keyboard shortcuts where feasible (Cmd-1…4 for views, etc.)
- Drawing goes directly to the window port (no GWorld double-buffering — nothing animates and classic-Mac memory is limited).
- Event data under `System Folder:Application Support:Kalendae:` (it is not a preference). Only app settings (window, view, calendars) go in `System Folder:Preferences:Kalendae Preferences`.
- Follow the sub-agent workflow strictly.

## Sub-agent Workflow (mandatory)
1. **Strong model** plans and writes the detailed plan down.
2. **Weaker models** implement discrete, well-scoped tasks.
3. **Weaker or stronger models** review each implementation.
4. **Strong model** performs an overall review of the whole diff before considering the work complete.

Every sub-agent task must receive:
- Exact goal
- Relevant existing files / code
- Success criteria
- List of files that may be touched

## Autonomous Execution Style
When implementing:
1. Modify code
2. `git add .` + `git commit -m "..."`  
3. `git push`
4. `gh run watch --exit-status`
5. On failure → `gh run view --log-failed`, fix, and loop

CI is the only source of truth. Do not run local builds.

## Current Status

### Done
- [x] CI pipeline (Retro68 + Multiversal Interfaces, GitHub Actions only)
- [x] Menu bar (Apple, File > Quit, View Day/Week/Month/Year + Cmd-1..4)
- [x] Toolbar view switcher
- [x] Sidebar with real category checkboxes
- [x] Mini-calendar navigator
- [x] Month / Day / Week / Year views (basic)
- [x] Window chrome, splitter, size/position persistence
- [x] Event creation from Month view (double-click day → dialog)

### Next up (priority order — fidelity first)
1. **Event creation + editing everywhere**  
   Double-click empty slot or existing event chip in Day / Week / Month to create or edit. Inspector-style dialog matching Apple Calendar fields (title, location, date/time, calendar/category, notes, alert, repeat).
2. **Event deletion** with confirmation.
3. **Sidebar filtering that actually works**  
   Unchecking a calendar must hide its events in every view.
4. **File → Import… / Export…**  
   Standard File dialogs + full round-trip `.ics` support.
5. **Real event persistence** across launches (write/read `.ics` in `Application Support:Kalendae`).
6. **“Today” button** that jumps to the current date in the active view.
7. **Recurrence**  
   Expand `RRULE` so repeating events appear correctly in all views.
8. **Reminders**  
   Notification Manager + Time Manager alerts.
9. **Search** (magnifying glass) if time remains.

## Fidelity Rules (Apple Calendar → Platinum)
- Layout proportions and information hierarchy should match the classic Apple Calendar as closely as Platinum controls allow.
- Sidebar on the left, mini-month at bottom of sidebar, view switcher at top.
- Event chips / blocks use category colours.
- Double-click / Return to edit, Delete key or menu to remove.
- No iCloud account UI, no shared calendars, no push notifications from the network.
- Keyboard shortcuts should mirror Apple Calendar where the Toolbox makes it practical.

## Technical Guidelines
- Classic (pre-Carbon) Mac Toolbox only.
- List Manager for grids and lists.
- Dialog Manager for the event inspector.
- Standard File Package for Import/Export.
- Notification Manager + Time Manager for reminders.
- Keep the partition size reasonable.
- Stay consistent with the rest of the brunocastello classic suite (creator code style, About box patterns from Gazette / Gateway, etc.).

## Reference Documents
- `PROJECT.md` / Apple Calendar Clone spec — architecture and original estimates
- Current source under `src/` and `include/`
- `Resources/Kalendae.r`
- Existing `CLAUDE-5.md` roadmap (now superseded by this file)

## Success Criteria
- A user familiar with Apple Calendar can open Kalendae on Mac OS 9 and immediately understand how to navigate views, create/edit/delete events, filter calendars, and import/export `.ics` files.
- All core interactions work without network.
- CI remains green after every change.
- The app looks and feels like Platinum Apple Calendar, not a generic classic app with a calendar stuck on top.

## Out of Scope
- Any iCloud / network / CalDAV / Exchange features
- Carbon or Mac OS X port (future work)
- Localization
- Animations or heavy GWorld usage
- Changing the Platinum visual language
