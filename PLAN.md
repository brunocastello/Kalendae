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

### 1.4 Event inspector (DLOG/DITL 128, rewritten)
The fields follow Apple Calendar's inspector order:

| Item | Control | Notes |
|---|---|---|
| Title | EditText | Required. Empty title on OK = treat as Cancel for new, keep old for edit |
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

The signature is `bool ShowEventInspector(Event& inOut, bool isNew)`, which
returns the user's choice: `kInspectorSaved | kInspectorCancelled | kInspectorDeleted`.
The dialog code moves out of `main.cpp` into a new `src/event_dialog.cpp` +
`include/event_dialog.h` so `main.cpp` stops growing.

### 1.5 Menus (Apple Calendar–style)
- **File**: New Event (⌘N), —, Import… (⌘I… see D3), Export…, —, Quit (⌘Q)
- **Edit**: Undo (disabled), —, Cut/Copy/Paste (for dialogs), Clear → "Delete" (Delete key)
- **View**: Day ⌘1, Week ⌘2, Month ⌘3, Year ⌘4, —, Go to Today ⌘T, Previous ⌘[ , Next ⌘]
- **Event**: Edit Event (⌘E / Return), Delete Event

### 1.6 Persistence
- Events go to `System Folder:Preferences:Kalendae Events` (type `TEXT`, creator
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
- **T2b — Inspector**: §1.4. New `event_dialog.{h,cpp}`, DITL/DLOG 128
  rewrite, CNTL/MENU resources for the 3 popups, add to `CMakeLists.txt`.
  **Touch**: `include/event_dialog.h`, `src/event_dialog.cpp`,
  `Resources/Kalendae.r`, `CMakeLists.txt`, `src/main.cpp` (delete old
  `ShowNewEventDialog`).
- **T2c — Wiring**: mouse routing in `HandleWindowEvent` uses `HitTest`.
  A single click selects an event (or clears the selection on an empty slot). A double-click
  on an event opens the inspector (edit); a double-click on an empty slot
  opens it for a new event at that date/time (Month = all-day, Day/Week = 1h at
  the slot). Return and ⌘E open the selected event. File ▸ New Event (⌘N) creates
  one at today/now, rounded to the next hour.
  **Touch**: `src/main.cpp`
- **Success (T2 overall)**: CI green. Create and edit work by
  double-click in Day, Week and Month. Edits persist in-session. Cancel
  changes nothing. The time grid scrolls. The Month double-click bug is gone
  (verified on a fresh artifact, per memory note).

### T3 — Deletion with confirmation (Next-up #2)
- **Goal**: The Delete/Backspace key (when no dialog is up), Edit ▸ Delete, Event ▸
  Delete Event, and the inspector's Delete button all go through
  `ConfirmAndDeleteEvent(id, occurrenceDate)`. The confirmation is a `CautionAlert`
  (ALRT/DITL 129): *"Are you sure you want to delete this event?"* with
  [Cancel] [Delete]. For a recurring event, use ALRT 130 instead: *"Do you want to delete
  all occurrences of this event, or only the selected occurrence?"* with
  [Cancel] [Delete All] [Delete Only This Event]. "Only this" adds an EXDATE.
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
  is drawn next to each checkbox (Apple's calendar-colour square).
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

### T7 — Today + navigation (Next-up #6)
- **Goal**: Apple's toolbar is `[◀] [Today] [▶]` on the right and the
  Day/Week/Month/Year segmented buttons centred. Add those push buttons
  (refCons `kTodayRefCon`, `kPrevPeriodRefCon`, `kNextPeriodRefCon`). Prev/Next
  step by the active view's unit (day/week/month/year). Bind View ▸ Go to Today
  ⌘T and Previous/Next ⌘[ / ⌘]. The active view's toolbar button is drawn
  highlighted (`HiliteControl` / value 1), which approximates Apple's segmented control.
  The mini-month marks today and the selected date.
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

D1, D2, D4 and D6 were confirmed by the user. D3, D5 and D7 take the recommendation as the default.

| ID | Decision | Resolution |
|----|----------|----------------|
| **D1** | CLAUDE.md says "List Manager for grids and lists", but the Month grid needs per-chip hit-testing, today-circle drawing and selection highlighting, none of which the List Manager can do (F3/F4). | Hand-draw the Month/Week/Day grids with a hit map. Keep the List Manager for genuine lists (search results, any future list). |
| **D2** | What should happen to imported events whose `CATEGORIES` aren't one of the 4 built-in calendars? | Auto-create the calendar with a palette colour (Apple creates a new calendar on import). This needs sidebar checkboxes to be dynamic, which they already are. |
| **D3** | Shortcuts: Apple Calendar has no ⌘I for Import (⌘I is Get Info). | Import/Export get no shortcut. ⌘E = Edit Event, ⌘T = Today, ⌘N = New Event. |
| **D4** | `git add .` will commit your uncommitted CLAUDE.md/PROJECT.md edits and AGENT.md with the first task. | Commit them first as their own "docs" commit (after `git pull --ff-only`). |
| **D5** | Recurring-event edits: Apple asks "this event / all future". | v1 edits the whole series. Delete offers "only this" via EXDATE. Revisit after T8. |
| **D6** | CLAUDE.md names Time Manager for reminders, but a Time Manager task runs at interrupt time and can't call `NMInstall` safely with an allocated record anyway. | Poll from the main event loop (`WaitNextEvent` sleep is 10 ticks, so latency is ≤ 15 s). No Time Manager. |
| **D7** | T0 hotfix: is it worth a CI round now, or should it go straight to T1/T2? | Do T0. It's 5 lines, and it lets you confirm F3 on real hardware/emulator. |

---

## 5. Checklist

- [x] D1–D7 answered
- [x] T0 hotfix
- [x] T1 foundation
- [ ] T2a renderer / T2b inspector / T2c wiring
- [ ] T3 delete
- [ ] T4 filtering
- [ ] T5 persistence
- [ ] T6 import/export
- [ ] T7 today + nav
- [ ] T8 recurrence
- [ ] T9 reminders
- [ ] T10 search (optional)
- [ ] T11 final review
