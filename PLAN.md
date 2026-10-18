# PLAN.md — Kalendae implementation plan (Phase 0, strong-model planning)

Scope: the "Next up" list in CLAUDE.md, items 1–9. Reference is Apple
Calendar's information architecture and interaction model, rendered in
Platinum. Every task below is sized for one weaker-model implementer, is
followed by a review, and must leave CI green before the next one starts.

---

## 0. Findings from reading the current code (baseline: `d3ff84a`)

These shape the task order. Every claim was checked against the source.

| # | Finding | Where | Consequence |
|---|---------|-------|-------------|
| F1 | `Event` holds only a `Date` (y/m/d) with **no time of day**. | `include/event.h` | Day/Week hour grids, timed iCal `DTSTART`, and reminders all need times first. This is the foundation task. |
| F2 | `Event` has **no stable identity**. `operator==` compares title+start+end, and views get **copies** (`std::vector<Event>`). | `event.h`, `calendar.cpp` | Edit and delete can't target an event reliably. Needs an ID. |
| F3 | Likely root cause of the **Month double-click bug**: `LGetSelect(true, {0,-1}, …)`. Per Inside Macintosh, `next=TRUE` searches *starting with* (inclusive) `theCell`, and `{0,-1}` is outside `dataBounds`, so the call can return false every time and `HandleMonthClick` bails before reaching the dialog. | `src/ui_renderer.cpp:131-139` | Fixed for good by T1 (custom hit-map). A one-line `LLastClick` fix is available as a hotfix (T0). |
| F4 | `mMonthList`/`mDayList` are **disposed and recreated on every update event**, so the List Manager's own click/double-click state is reset by any redraw. List-cell hilite also inverts the whole cell, chips included. | `ui_renderer.cpp:395-448, 229-257` | List Manager can't do chip hit-testing anyway (see Decision D1). |
| F5 | Sidebar checkbox state lives **only in the controls**. `RebuildControls()` (resize/splitter) resets every box to checked, and no view reads it. | `main.cpp:256, 554, 884-887` | Filtering needs model-side visibility state. |
| F6 | `ICALParser` uses `std::ifstream`/`std::ofstream` with path strings, and `ExportCalendar` writes a **hard-coded sample event**. `ParseDate` ignores time, `TZID`, `VALUE=DATE` and `Z`. There is no line unfolding, no unescaping, no CR-only line endings, and no `UID`. | `src/ical_parser.cpp` | Rewrite it as a buffer-based parser plus File Manager I/O (FSSpec). Classic Mac files use CR line endings. |
| F7 | `RecurrenceEngine::AdvanceDate` does `day += n` with **no month rollover**, and nothing calls the engine. `Event::IsOnDate` ignores the RRULE. | `src/recurrence.cpp`, `event.cpp` | Rewrite it. Reuse the correct `AddDays` from `ui_renderer.cpp:38` by moving it to `Date`. |
| F8 | `Calendar()` hard-codes the current date to **2026-09-01**, and `LoadFromPreferences()` injects 3 sample events. | `calendar.cpp:13, 26-50` | "Today" and persistence both have to replace this. |
| F9 | The categories map is filled in two places (ctor and `InitializeDefaultCategories`), and the dialog's 4 radio buttons are **hard-coded in the DITL**. | `calendar.cpp`, `Kalendae.r` | The inspector should use a popup menu built from `GetCategories()`. |
| F10 | There is no Edit menu and no Event menu. The keyDown handler only processes Cmd-keys, so Delete and Return do nothing. | `main.cpp:732-753, 943-954` | Needs menu-bar work in T2/T3. |
| F11 | Prefs file validation is `count != sizeof(WindowPrefsData)`. | `main.cpp:403-410` | Any struct growth silently resets the user's window prefs once. Add a version field when extending it (T5). |
| F12 | `origin/main` is one commit ahead (`fc3f5ec`, README only), and the working tree has the user's uncommitted edits to CLAUDE.md/PROJECT.md plus an untracked AGENT.md. | git | Run `git pull --ff-only` before the first push. `git add .` in the loop **will sweep in those doc edits**; see Decision D4. |

---

## 0.5 Visual baseline (modern Apple Calendar, user screenshot 2026-10-18)

This is the reference for every view. Platinum replaces Aqua materials, but the
layout and hierarchy follow it.

- **Toolbar**: `+` (new event) on the left; Day | Week | Month | Year segmented
  control centred; 🔍 search on the right. The toolbar does not hold navigation.
- **Content title row**: bold month name then the year in plain text, both
  large and left-aligned ("**October** 2026"). Right-aligned on the same row are
  `[<] [Today] [>]`.
- **Month grid**: always 6 weeks.
  - Weekday names ("Sun".."Sat") are right-aligned over each column. Day numbers
    sit at the top-right of each cell, and the first of a month reads "1 Oct".
    Adjacent-month days are grey numbers on the same background.
  - Today is a white number on a red filled circle.
  - There are thin column and week lines only.
- **Chips**:
  - A timed event is a thin vertical bar in the calendar colour, then the title.
  - An all-day event is a rounded bar tinted with the calendar colour.
  - Titles are truncated with "…", and overflow reads "+N more".
- **Contextual menus** (user screenshots 2026-10-18), via right-click or control-click:
  - On an **empty slot**: New Event · New Reminder · Paste Event (disabled when
    the event clipboard is empty).
  - On an **event**: Get Info · Calendar ▸ (submenu listing the calendars, with
    the current one checked) · — · Delete · Cut · Copy · Paste · Duplicate · — ·
    Email Event · Email All Participants · Message All Participants.
  - Right-clicking an event also selects it, drawn with the selected look.
- **Sidebar**: one group header, with checkbox rows in the calendar colour
  underneath. Apple's local group is "On My Mac", which we use in place of iCloud.
  Below them is the mini-month: a `<  October 2026  >` header, S M T W T F S,
  grey adjacent-month days, and a red circle for today.

---

## 1. Architecture changes (shared by all tasks)

### 1.1 Data model (`include/event.h`, `src/event.cpp`)
- `Date` gains `static Date FromSerial(long)`, `long ToSerial() const` (days
  since 1904-01-01, which matches the Mac epoch), and `Date AddDays(int) const`.
  The static `AddDays` in `ui_renderer.cpp` is deleted in favour of these.
- `Event` gains:
  - `unsigned long mID`, a runtime-unique ID assigned by `Calendar::AddEvent`
    (monotonic counter). It is never persisted, and every UI selection uses it.
  - `std::string mUID`, the iCal UID. It is persisted and generated if missing
    (`<seconds>-<counter>@kalendae.local`).
  - `short mStartMinute, mEndMinute` (0..1440, minutes after midnight),
    ignored when `mAllDay`.
  - `short mAlertMinutes` (-1 = none, else minutes before start).
  - `std::vector<Date> mExDates` (recurrence exceptions).
  - `mDescription` is the Notes field (already exists).
- `operator==` compares `mID`.

### 1.2 Calendar (`include/calendar.h`, `src/calendar.cpp`)
- Owns `std::vector<Event>`, and edits go by ID:
  `Event* FindEvent(unsigned long id)`, `bool UpdateEvent(const Event&)`,
  `bool RemoveEvent(unsigned long id)`, `void AddExDate(unsigned long id, Date)`.
- Query API returns **occurrences**, not copies of base events:
  ```cpp
  struct Occurrence { unsigned long eventID; Date date; };   // + const Event* for convenience
  std::vector<Occurrence> GetOccurrencesForDate(const Date&) const;   // respects visibility + RRULE
  std::vector<Occurrence> GetOccurrencesInRange(const Date&, const Date&) const;
  ```
  Ordering is all-day events first, then by start minute, then by title.
- Category visibility: `std::map<std::string,bool> mCategoryVisible`,
  `SetCategoryVisible / IsCategoryVisible`. Queries skip hidden categories.
- A `bool mDirty` flag is set by every mutation. `main.cpp` saves when it's dirty.
- `Date Calendar::Today()` reads `GetDateTime` + `SecondsToDate`.

### 1.3 Rendering and hit-testing (`ui_renderer.*`), which fixes F3/F4
Every view registers what it draws in a **hit map** during render:
```cpp
struct HitRegion { Rect r; unsigned long eventID; Date date; short minute; };  // eventID 0 = empty slot
std::vector<HitRegion> mHits;          // cleared at start of each Render*View
bool UIRenderer::HitTest(Point local, HitRegion* out) const;  // topmost (last-added) wins
```
- **Month**: each day cell registers one empty-slot region (`minute = -1`,
  meaning all-day). Each chip registers an event region on top of it.
- **Week/Day**: these become Apple-style **time grids**: an all-day strip at
  the top and 24 hour rows with an hour gutter on the left. Empty
  half-hour slots register `minute = hh*60 + (0|30)`, and event blocks are
  sized by start/end minute. Overlapping events split the column width
  evenly. A vertical `scrollBarProc` control scrolls the grid (row height 22px,
  initial scroll to 08:00 or the first event).
- **Year**: each mini-month day registers a slot. Double-click jumps to Day view.
- `main.cpp` does double-click detection itself:
  `TickCount() - gLastClickTicks <= GetDblTime()` **and** the same hit target
  (same eventID, or same date+minute for empty slots). This is identical across
  all views and doesn't depend on List Manager state.
- The selected event (`gSelectedEventID`) draws with an inverted or
  darker chip, matching Apple's selected-event look.

### 1.4 Event inspector popover (DLOG/DITL 128, rewritten)
This replaces both the old New Event dialog and the centred-inspector idea. It
mirrors Apple Calendar's event popover. Mac OS 9 has no popover control, so the
inspector is built like this (decided 2026-10-18):
- **Window**: a borderless, shadowed dialog (`altDBoxProc`, so no title bar or
  close box). It sits beside its **anchor rect** (the clicked event chip/block,
  or the empty slot for a new event): 8px to the right of the anchor and
  vertically centred on it. If that would go off the right edge of
  `qd.screenBits.bounds`, it goes to the left. It is then clamped inside the
  screen, below the menu bar.
- **Behaviour**: it is driven by `ModalDialog` with a filter proc that copies the
  popover's transient feel:
  - mouseDown outside the dialog window = save and close (same as Return/OK);
  - Escape or ⌘-period = cancel;
  - Return/Enter = save, except when the Notes field is active, where Return
    inserts a line break;
  - Tab cycles the edit fields (standard Dialog Manager behaviour).
- **New events**: they open with the title "New Event" pre-selected, so typing
  replaces it, as in Apple Calendar. A new event that is closed with an unchanged
  or empty title is still created as "New Event", as in Apple. Cancel discards it.
- **Edits**: the event is loaded from `*FindEvent(id)` and saved with
  `UpdateEvent` (see T1 review note: never rebuild it from a blank `Event`).
- **The pointer tail** (the arrow pointing at the anchor) is *not* part of T2b.
  It needs a custom WDEF. See T2d.

The fields follow Apple Calendar's inspector order:

| Item | Control | Notes |
|---|---|---|
| Title | EditText (larger, bold-looking: system font) | Pre-filled "New Event" for new events. Empty on save → "New Event" for new, keep old title for edit |
| Location | EditText | |
| All-day | CheckBox | Disables/greys time fields when on |
| Starts: date | EditText `M/D/YYYY` | Validated on OK. Invalid → `StopAlert` + reselect field |
| Starts: time | EditText `H:MM AM/PM` or `HH:MM` | Parse both |
| Ends: date / time | EditText ×2 | End < start → clamp to start + 60 min |
| Calendar | **popup** (`CNTL` with `popupMenuProc`, MENU built at runtime from categories) | Replaces the 4 radios (F9) |
| Alert | popup: None, At time of event, 5 min, 15 min, 30 min, 1 hour, 1 day before | |
| Repeat | popup: None, Every Day, Every Week, Every Month, Every Year | Writes `RRULE:FREQ=…` |
| Notes | multi-line EditText (tall rect) | Str255 limit accepted for v1 |
| Delete | Button, hidden for new events | → confirmation alert (T3) |
| Cancel / OK | Buttons | OK is the default (bold ring via `SetDialogDefaultItem`) |

The signature is `short ShowEventInspector(Event& inOut, bool isNew, const Rect& anchorGlobal)`,
which returns `kInspectorSaved | kInspectorCancelled | kInspectorDeleted`.
`anchorGlobal` is the anchor rect in global coordinates.
The dialog code moves out of `main.cpp` into a new `src/event_dialog.cpp` +
`include/event_dialog.h` so `main.cpp` stops growing.

### 1.5 Menus (Apple Calendar–style)
- **File**: New Event (⌘N), —, Import… (⌘I… see D3), Export…, —, Quit (⌘Q)
- **Edit**: Undo (disabled), —, Cut/Copy/Paste (for dialogs), Clear → "Delete" (Delete key)
- **View**: Day ⌘1, Week ⌘2, Month ⌘3, Year ⌘4, —, Go to Today ⌘T, Previous ⌘[ , Next ⌘]
- **Event**: Edit Event (⌘E / Return), Delete Event

### 1.6 Persistence
- Events go to `System Folder:Application Support:Kalendae:Kalendae Events` (user decision 2026-10-18: event data is not a preference. The folder is created on demand, and if `FindFolder('asup')` fails the file falls back to Preferences). Type `TEXT`, creator
  `KALE`, iCalendar contents), using the same parser/writer as Import/Export,
  so there's a single code path.
- The save writes a temp file (`Kalendae Events~`) and then `FSpExchangeFiles`
  (or delete+rename as a fallback), so a crash mid-write can't destroy data.
- Saves happen when `mDirty` is set after each mutating action, and in `QuitApp`.
- Category visibility, the current view, and the prefs format version go
  into `Kalendae Preferences`, behind a `short version` header (F11).

---

## 2. Task breakdown (execute strictly in order)

Each task is one commit, or a small series, plus a CI-green push. "Touch" lists
are exhaustive. Implementers must not edit other files.

### T0 — Hotfix: Month double-click (optional, ~5 lines)
- **Goal**: unblock manual testing now. In `HandleMonthClick`, replace the
  `LGetSelect` search with `Cell cell = LLastClick(mMonthList);` plus a
  bounds check. If `LLastClick` isn't in Multiversal, fall back to
  `cell = {0,0}; LGetSelect(true, &cell, …)`.
- **Touch**: `src/ui_renderer.cpp`
- **Success**: CI green. User confirms that double-clicking a day opens the
  dialog on a build from this commit's artifact. T1 deletes this code anyway.

### T1 — Foundation: time-of-day, IDs, occurrence API, shared date helpers
- **Goal**: implement §1.1 and §1.2. Recurrence expansion is still a stub
  here (base date only). Visibility map exists, with everything visible by default.
  Update all existing call sites (`ui_renderer.cpp`, `main.cpp`) to the
  occurrence API with no visible behaviour change. Remove the duplicate
  category init (F9). Set the initial current date from `Calendar::Today()` (F8).
- **Touch**: `include/event.h`, `src/event.cpp`, `include/calendar.h`,
  `src/calendar.cpp`, `src/ui_renderer.cpp`, `src/main.cpp`
- **Success**: CI green. The app opens on today's month. The 3 sample events
  still render (re-dated relative to today). No `std::vector<Event>`
  copies are returned to views. `grep -n "AddDays" src/ui_renderer.cpp`
  shows only `Date::AddDays` uses.

### T2 — Hit map + time-grid Day/Week views + inspector (Next-up #1)
Split for implementers:
- **T2a — Renderer**: §1.3. The Month grid gets custom drawing (D1), and
  Day/Week become time grids with a scroll bar. Fill `mHits`, then remove
  `mMonthList`/`mDayList` and `HandleMonthClick`. Draw selection
  highlighting from a `SetSelectedEvent(id)` setter. Mark today's date
  (red-filled circle behind the day number, Apple-style).
  **Touch**: `include/ui_renderer.h`, `src/ui_renderer.cpp`, `src/main.cpp` (scroll-bar
  control creation/tracking only).
- **T2b — Inspector popover**: §1.4 (popover placement, outside-click save,
  Esc cancel, no tail). New `event_dialog.{h,cpp}`, DITL/DLOG 128 rewrite
  (`altDBoxProc`), CNTL/MENU resources for the 3 popups, add to `CMakeLists.txt`.
  The renderer exposes the hit region's rect so callers can pass it as the anchor.
  **Touch**: `include/event_dialog.h`, `src/event_dialog.cpp`,
  `Resources/Kalendae.r`, `CMakeLists.txt`, `src/main.cpp` (delete old
  `ShowNewEventDialog`).
- **T2c — Wiring**: mouse routing in `HandleWindowEvent` uses `HitTest`.
  A single click selects an event (or clears the selection on an empty slot). A double-click
  on an event opens the popover anchored to that chip/block (edit). A double-click
  on an empty area opens a new-event popover anchored to that slot. This includes
  the empty part of a day that already has events: chips sit on top of the day's
  slot region, so a click that misses every chip hits the slot. New events from
  Month are all-day; from Day/Week they are 1h at the half-hour slot.
  Double-click on a Year-view day switches to Day view on that date (T2a review #6).
  T2a already added the hit-test selection and double-click detection in
  `main.cpp` (`HandleContentClick`). Build on it and don't duplicate it.
  Return and ⌘E open the selected event's popover. File ▸ New Event (⌘N) creates
  one at the current date and the next whole hour, anchored to that slot if it is
  visible and otherwise centred in the window.
  **Touch**: `src/main.cpp`
- **Success (T2 overall)**: CI green. Create and edit work by
  double-click in Day, Week and Month. Edits persist in-session. Cancel
  changes nothing. The time grid scrolls. The Month double-click bug is gone
  (verified on a fresh artifact, per memory note).

### T2d — Popover pointer tail (polish, after T2 is reviewed)
- **Goal**: give the inspector popover Apple's callout tail, a small triangle
  pointing at the anchor rect on whichever side the popover was placed. Write a
  custom WDEF compiled into the app and registered at runtime. Use the
  standard Retro68 approach: a `WDEF` resource holding a stub that jumps to
  a `UniversalProcPtr` from `NewWindowDefUPP` (or `NewRoutineDescriptor`),
  installed at startup. Implement `wDraw`, `wHit`, `wCalcRgns` (structure region
  = rounded rect ∪ tail triangle, with a 1px Platinum-grey frame and a drop shadow),
  `wNew` and `wDispose`. The tail side and offset go through the window's refCon or a
  small struct passed via `SetWRefCon`.
- **Fallback**: if the stub approach is unreliable under Mac OS 9's Mixed
  Mode, keep `altDBoxProc` and drop T2d. Note why in this file.
- **Touch**: `src/popover_wdef.cpp` (new), `include/popover_wdef.h` (new),
  `src/event_dialog.cpp`, `Resources/Kalendae.r`, `CMakeLists.txt`
- **Success**: CI green. The popover shows a tail pointing at the clicked event in
  all three placements (right, left, clamped). There are no crashes when opening and closing
  it repeatedly, or when it is dragged off-screen by its owner window moving
  (the popover is modal, so it can't be). Hit-testing outside the shape still
  counts as "outside" for save-and-close.

### T3 — Deletion with confirmation (Next-up #2)
- **Goal**: The Delete/Backspace key (when no dialog is up), Edit ▸ Delete, Event ▸
  Delete Event, and the inspector's Delete button all go through
  `ConfirmAndDeleteEvent(id, occurrenceDate)`. The confirmation is a `CautionAlert`
  (ALRT/DITL 129): *"Are you sure you want to delete this event?"* with
  [Cancel] [Delete], where **Delete is the default**, as in Apple. For a recurring event, use ALRT 130 instead: *"Do you want to delete
  all occurrences of this event, or only the selected occurrence?"* with
  [Delete All] [Cancel] [Delete Only This Event], where "Only this" is the default. "Only this" adds an EXDATE.
  Unexpected alert results (including -1 when the ALRT fails to load) never delete.
  Menu items are enabled/disabled from the selection state
  (`EnableItem`/`DisableItem` before `MenuSelect`/`MenuKey`).
- **Touch**: `src/main.cpp`, `Resources/Kalendae.r`, `src/event_dialog.cpp`
- **Success**: CI green. Every path asks first. Cancel leaves the event. The selection
  clears after a delete. The window redraws.

### T4 — Sidebar filtering (Next-up #3)
- **Goal**: The checkbox click calls `Calendar::SetCategoryVisible` and invalidates.
  `SetupSidebar` initialises each checkbox from `IsCategoryVisible` (fixes
  F5 reset-on-resize). Hidden categories are excluded by the occurrence
  queries, so every view inherits this for free. A colour swatch
  is drawn next to each checkbox (Apple's calendar-colour square; Platinum
  checkboxes can't be tinted). The list gets an "On My Mac" group header
  (§0.5).
- **Touch**: `src/main.cpp`, `src/calendar.cpp` (if needed)
- **Success**: CI green. Unchecking hides that calendar's events in all four views,
  and the state survives a window resize and a splitter drag.

### T5 — Real persistence (Next-up #5; done before Import/Export because they share code)
- **Goal**: Write the new `src/ical_io.cpp`/`include/ical_io.h` with
  `OSErr ReadFileToString(const FSSpec&, std::string&)` and
  `OSErr WriteStringToFile(const FSSpec&, const std::string&, OSType type, OSType creator)` (safe-save
  per §1.6). Rewrite `ICALParser` to be buffer-based:
  `std::vector<Event> Parse(const std::string&)` and
  `std::string Serialize(const std::vector<Event>&)`. It must handle CR / LF / CRLF,
  line unfolding, TEXT unescaping/escaping (`\\ \; \, \n`), params
  (`DTSTART;VALUE=DATE:`, `;TZID=…:` treated as local), UTC `Z` → local via
  `ReadLocation` GMT delta, `UID`, `CATEGORIES`, `RRULE`, `EXDATE`,
  `VALARM/TRIGGER:-PT15M`, and the all-day exclusive `DTEND` convention. Output
  is CRLF with 75-octet folding. Remove the sample events. Load at
  startup and save when dirty or on quit. Add a prefs `version` field plus the visibility
  bitmask and the last view.
- **Refinements (2026-10-18, these replace the bitmask idea above)**:
  - **Encoding**: `.ics` text is UTF-8. The model is MacRoman. The parser
    converts both ways with a 128-entry table. Unmappable code points become
    `?`. Folding never splits a UTF-8 sequence.
  - **Calendars live in prefs**: the prefs file is versioned (`short version = 2`)
    and stores, after the window fields, the last view and a list of calendars
    (name, RGB, visible). Visibility toggles mark the *prefs* dirty, not the
    events (T1 review #3). If an event loads with an unknown category, the
    calendar is auto-added with the next palette colour (D2). A v1 prefs file
    keeps its window fields and takes defaults for everything else.
  - **Saving**: the main loop saves events when `IsDirty()` is set and prefs
    when `IsPrefsDirty()` is set, after each handled event and on Quit.
    `ClearDirty()` runs after load.
  - **Testability**: `ICALParser` makes no Toolbox calls. The GMT offset is passed
    in, so the parser can be compiled on the host for review.
- **Touch**: `include/ical_parser.h`, `src/ical_parser.cpp`, `include/ical_io.h`,
  `src/ical_io.cpp`, `src/calendar.cpp`, `include/calendar.h`, `src/main.cpp`,
  `CMakeLists.txt`
- **Success**: CI green. Events, category visibility, and the view survive a quit and
  relaunch. Deleting the events file gives an empty calendar, not a crash.
  A 2nd implementer-independent reviewer checks the parser against RFC 5545 §3.1
  (folding) and §3.3.5 (DATE-TIME forms).

### T6 — File ▸ Import… / Export… (Next-up #4)
- **Goal**: Import uses `StandardGetFile` (filter `TEXT`, plus any type whose name
  ends in `.ics` via a file filter proc), then parse and merge, skipping UIDs
  that already exist. Unknown categories are mapped to the first category
  (or auto-added with a grey colour; see D2). Report "Imported N events" via
  `NoteAlert`. Export uses `StandardPutFile` (default name `Kalendae.ics`) and
  writes all events, including hidden calendars (Apple exports per-calendar; v1
  exports everything).
- **Touch**: `src/main.cpp`, `Resources/Kalendae.r` (ALRTs)
- **Success**: CI green. Exporting, deleting everything, and importing restores identical
  events (title, location, times, calendar, notes, alert, repeat). A
  Google/Apple-exported `.ics` sample imports without crashing (reviewer
  supplies 3 fixtures in the task prompt).

### T6b — Contextual menus + event clipboard (after T6, since it reuses the serializer)
- **Goal**: Apple's right-click menus (§0.5), using the **Contextual Menu Manager**
  (Mac OS 8+). Call `InitContextualMenus()` at startup. On mouseDown, use
  `IsShowContextualMenuClick(event)`, which covers both control-click and a
  right button the mouse driver maps to it. Then `ContextualMenuSelect(menu,
  globalPt, false, kCMHelpItemNoHelp, NULL, NULL, &selType, &menuID, &item)`.
  If Multiversal doesn't declare the CMM calls, or `Gestalt(gestaltContextualMenuAttr)`
  reports it as missing, fall back to control-click → `PopUpMenuSelect`
  with the same menus. Do the hit test first (`HitTest`) so the menu matches the target.
  - **Empty slot** (Month day, Day/Week half-hour or all-day slot):
    - **New Event** opens the popover at that slot, the same as a double-click.
    - **Paste Event** pastes the clipboard event at that slot, keeping its
      duration and all-day flag. It is disabled when the clipboard is empty.
    - "New Reminder" is **omitted**, since there is no Reminders app and it is out of scope.
  - **Event** (right-click selects it first):
    - **Get Info** opens the popover anchored to the chip, the same as a double-click.
    - **Calendar ▸** is a hierarchical submenu of the calendars, with the current one
      checked. Choosing one moves the event (`UpdateEvent`).
    - **Delete** goes through `ConfirmAndDeleteEvent`.
    - **Cut** copies, then deletes without confirming. Apple doesn't ask on Cut, and
      the event can be pasted back.
    - **Copy**.
    - **Paste** is enabled only when the clipboard holds an event, and pastes onto
      that event's date.
    - **Duplicate** makes a copy with a new UID on the same date and time and selects it.
    - "Email Event", "Email All Participants" and "Message All Participants" are
      **omitted**, because they need the network.
- **Event clipboard**: an in-app copy of one `Event` (ID and UID cleared on paste).
  Copy and Cut also put the event's iCalendar text on the desk scrap
  (`ZeroScrap` + `PutScrap('TEXT')`), so it can be pasted into SimpleText. This uses the
  T5 serializer.
- **Edit menu**: when no dialog is up and an event is selected, Cut ⌘X, Copy ⌘C,
  Paste ⌘V and Duplicate ⌘D (a new item) act on events the same way. Paste then
  targets the selected date, or the current date if nothing is selected.
  They are enabled and disabled in `UpdateMenuStates`.
- **Touch**: `src/main.cpp`, `src/calendar.cpp`/`include/calendar.h` (only for a
  duplicate/clipboard helper), `Resources/Kalendae.r` (only if a MENU resource helps),
  `include/ui_renderer.h`/`src/ui_renderer.cpp` (only if hit lookup needs it)
- **Success**: CI green. Right-click and control-click show the correct menu
  on events and empty slots in Day, Week and Month. Every item works. Cut then Paste
  moves an event. Copy puts iCalendar text on the desk scrap. Duplicate creates
  an independent event. On a system without the CMM, control-click still works.

### T7 — Today + navigation (Next-up #6)
- **Goal**: Follow §0.5. `[<] [Today] [>]` sit at the right end of the
  **content title row**, not in the toolbar. The toolbar becomes `+` (New Event,
  same as ⌘N) on the left, the Day/Week/Month/Year buttons centred, and a search
  button on the right (disabled until T10). Add the push buttons
  (refCons `kTodayRefCon`, `kPrevPeriodRefCon`, `kNextPeriodRefCon`). Prev/Next
  step by the active view's unit (day/week/month/year). Bind View ▸ Go to Today
  ⌘T and Previous/Next ⌘[ / ⌘]. The active view's toolbar button is drawn
  highlighted (`HiliteControl` / value 1), which approximates Apple's segmented control.
  The mini-month matches §0.5 (`<  Month YYYY  >` header, grey adjacent
  days, a red circle for today) and marks the selected date.
- **Touch**: `src/main.cpp`, `src/ui_renderer.cpp`
- **Success**: CI green. Today jumps to the current date in every view. Prev/Next
  step correctly across month/year boundaries.

### T8 — Recurrence (Next-up #7)
- **Goal**: Rewrite `RecurrenceEngine` to support `FREQ=DAILY|WEEKLY|MONTHLY|YEARLY`,
  `INTERVAL`, `COUNT`, `UNTIL`, `BYDAY` (weekly only), and EXDATE. Use
  `Date::AddDays`/serials (fixes F7). Monthly on the 31st skips short months
  (RFC behaviour), and Feb 29 yearly only occurs in leap years. `GetOccurrencesInRange`
  expands per query range. No pre-materialisation, so memory stays bounded. Unsupported
  rules (`BYSETPOS`, `BYMONTHDAY` lists…) show only the first occurrence and are
  round-tripped verbatim on export.
- **Touch**: `include/recurrence.h`, `src/recurrence.cpp`, `src/event.cpp`,
  `src/calendar.cpp`
- **Success**: CI green. A weekly event shows on every matching day in all views,
  `COUNT`/`UNTIL` stop correctly, and an EXDATE hides one instance. Edit applies
  to the whole series (Apple's "this/future" split is D5).

### T9 — Reminders (Next-up #8)
- **Goal**: `Calendar::CheckForUpcomingReminders(now)` runs from the event loop
  at most every 15 s (`TickCount`), and it finds occurrences whose
  `start - alertMinutes` falls in `(lastCheck, now]`. Each fires once via
  `NMInstall` (`nmMark` on the app, `nmSound = (Handle)-1` default beep,
  `nmStr` = "Kalendae: <title> at <time>"). The response proc removes and frees the
  NMRec. Already-fired keys (`eventID+date`) are tracked in-session. The `SIZE`
  `canBackground` flag is already set, so alerts fire while backgrounded.
  See D6 on Time Manager.
- **Touch**: `src/calendar.cpp`, `include/calendar.h`, `src/main.cpp`
- **Success**: CI green. An event with "5 min before" posts a notification
  ~5 min ahead, once, including when Kalendae isn't frontmost.

### T10 — Search (Next-up #9, only if time remains)
- Find… (⌘F) opens a dialog with a query field. Results show in a List Manager list
  (title, date), and double-clicking one jumps to its Day view and selects it.
  This is planned only after T9 is reviewed.

### T11 — Final strong-model review + docs
Do a full-diff review (`git diff d3ff84a..HEAD`) against this plan and CLAUDE.md's
fidelity rules, then update CLAUDE.md's Done/Next lists and remove superseded
comments.

---

### Carry-over notes from reviews
- **T11**: temp names now step through `~`, `~1` … `~9` and never delete a colliding file. A crash-leftover `Kalendae Events~` therefore stays until it is recovered or removed. Once the target has loaded successfully, delete stale `Kalendae Events~*` at startup. Otherwise saves start failing after 10 leftovers (T6 fix-up review).
- **T6**: use `X-WR-CALNAME` as the calendar for events that have no `CATEGORIES`, so an Apple export lands in its own calendar (D2) and not in "Home" (T5 RFC review).
- **All host tests**: the PPC target is ILP32. Any host test of date/time math must run with 32-bit `long` semantics, or it hides overflow (T5 RFC review B1).
- **Known limits (accepted)**: TZID times are read as local, and one GMT offset applies to all dates, so UTC events across a DST change can be off by one hour. `RECURRENCE-ID` overrides import as standalone events.
- **T6**: when an import auto-creates a calendar (D2), call `RebuildControls()` and invalidate. `SetupSidebar` only runs at startup and on rebuild, so a new calendar otherwise gets no checkbox and no colour swatch (T4 review #6).
- **T2b**: build the edited event from `*FindEvent(id)` and not from a blank `Event`. `UpdateEvent` copies `mUID` verbatim, so a fresh `Event` would wipe the UID (T1 review #1).
- **T5**: call `ClearDirty()` after loading. Category visibility goes to the prefs file, so give it its own prefs-dirty flag instead of `mDirty`. Otherwise every checkbox toggle rewrites the events file (T1 review #3).
- **T8**: expand each RRULE once per event over the query range, not once per day. `GetOccurrencesInRange` is currently O(days × events) (T1 review #4).
- **T8**: `OccurrenceMinutes` (ui_renderer.cpp) clips multi-day timed events per date, but only for non-recurring events. A recurring *multi-day* event needs each occurrence's own start and end dates from the engine to clip the same way (T2a fix-up).

---

## 3. Per-task sub-agent protocol

For each task (and sub-task) Tn:
1. **Implement**: one `porter` agent (weaker model, e.g. Sonnet) gets: the
   Tn section verbatim, §1 for shared design, the current contents of every
   file in its Touch list, the constraint list below, and the success
   criteria. It runs the CLAUDE.md loop (commit → push → `gh run watch
   --exit-status` → fix on failure).
2. **Review**: one `reviewer` agent reviews `git diff <before>..HEAD` for
   correctness, Toolbox misuse (unbalanced `SetPort`, handle locking across
   moves-memory calls, Str255 overflows, `DisposeDialog` paths), and plan
   conformance. Blocking findings go back to step 1 (same agent via SendMessage).
3. **Gate**: the strong model checks the review, confirms CI is green, and updates this
   file's checklist.

**Constraints given to every implementer**
- Classic Toolbox only (Multiversal Interfaces). No Carbon-only calls
  (`GetPortBounds`, `GetControlBounds`, Navigation Services, etc.).
- No GWorld. Draw into the window port. No network APIs.
- No C++ global objects with constructors (see the `main.cpp:56-63` note:
  Retro68 PPC crt0 skips static ctors). Function-local statics of POD
  types only.
- Don't use exceptions or `std::stoi` (it throws). Write your own digit parsing.
- Every `std::string` → `Str255` conversion clamps to 255.
- Stay inside the Touch list, and don't run local builds.

---

## 4. Decisions (resolved 2026-10-18)

D1, D2, D4, D6 and D8 were confirmed by the user. D3, D5 and D7 take the recommendation as the default.

| ID | Decision | Resolution |
|----|----------|----------------|
| **D1** | CLAUDE.md says "List Manager for grids and lists", but the Month grid needs per-chip hit-testing, today-circle drawing and selection highlighting, none of which the List Manager can do (F3/F4). | Hand-draw the Month/Week/Day grids with a hit map. Keep the List Manager for genuine lists (search results, any future list). |
| **D2** | What should happen to imported events whose `CATEGORIES` aren't one of the 4 built-in calendars? | Auto-create the calendar with a palette colour (Apple creates a new calendar on import). This needs sidebar checkboxes to be dynamic, which they already are. |
| **D3** | Shortcuts: Apple Calendar has no ⌘I for Import (⌘I is Get Info). | Import/Export get no shortcut. ⌘E = Edit Event, ⌘T = Today, ⌘N = New Event. |
| **D4** | `git add .` will commit your uncommitted CLAUDE.md/PROJECT.md edits and AGENT.md with the first task. | Commit them first as their own "docs" commit (after `git pull --ff-only`). |
| **D5** | Recurring-event edits: Apple asks "this event / all future". | v1 edits the whole series. Delete offers "only this" via EXDATE. Revisit after T8. |
| **D6** | CLAUDE.md names Time Manager for reminders, but a Time Manager task runs at interrupt time and can't call `NMInstall` safely with an allocated record anyway. | Poll from the main event loop (`WaitNextEvent` sleep is 10 ticks, so latency is ≤ 15 s). No Time Manager. |
| **D7** | T0 hotfix: is it worth a CI round now, or should it go straight to T1/T2? | Do T0. It's 5 lines, and it lets you confirm F3 on real hardware/emulator. |
| **D8** | Default calendars (decided 2026-10-18). | The defaults are Apple's local pair: **Home** (Apple blue 0,122,255) and **Work** (Apple red 255,59,48). They replace Work/Personal/Retro/Vintage. Applied in T5 (or the T4 fix-up, if there is one). |

---

## 5. Checklist

- [x] D1–D7 answered
- [x] T0 hotfix
- [x] T1 foundation (reviewed 2026-10-18, no blockers)
- [x] T2a renderer (fix-up 4f5a965) / T2b inspector popover / T2c wiring (merged into T2b; fix-up 55c5cf5), reviewed. Awaiting on-screen test by the user.
- [ ] T2d popover tail (custom WDEF, polish)
- [x] T3 delete (reviewed; fix-up 3aea0a9)
- [x] T4 filtering (reviewed; fix-up b0dacc5, Home/Work per D8)
- [x] T5 persistence (reviewed twice; fix-ups f8a01bb, 2c64c6d). Quit/relaunch verified on the target by the user on 2026-10-18.
- [x] T6 import/export
- [ ] T6b contextual menus + event clipboard
- [ ] T7 today + nav
- [ ] T8 recurrence
- [ ] T9 reminders
- [ ] T10 search (optional)
- [ ] T11 final review
