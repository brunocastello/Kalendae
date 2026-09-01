# Building a Native Calendar App for Mac OS 9
**Software Architecture & Technical Blueprint**

---

## 1. Executive Summary & Strict Offline Design

During the Classic Mac OS era, date tracking was handled by basic Desk Accessories like the Date & Time Control Panel or simple utilities like Claris Organizer. Apple's modern macOS Calendar app provides a clean, category-coded multi-view interface (Day, Week, Month, Year) with sidebar list filters.

In accordance with strict project guidelines, this application operates **100% offline with zero external proxies or cloud synchronization**. Data interchange relies entirely on standard local `.ics` (iCalendar) file imports and exports via local volumes, floppy disks, network shares, or disk images.

---

## 2. Architectural Layout & System Data Flow

The UI mirrors modern macOS Calendar while using native Mac OS 9 Platinum controls. A custom Canvas rendering pipeline draws fluid Day, Week, and Month grids with event boxes, color categories, and time blocks.

```
[ Import .ICS File ] ───► iCalendar Text Parser ───► Store in Local Preferences Database
                                │
                                ▼
                 [ Modern Calendar Clone UI ]
  ┌──────────────────┬──────────────────────────────────────────┐
  │ Calendars / Tags │  [ Day ]  [ Week ]  [ Month ]  [ Year ]  │
  │  [x] Work        ├──────────────────────────────────────────┤
  │  [x] Personal    │ Sun   Mon   Tue   Wed   Thu   Fri   Sat  │
  │  [ ] Retro Dev   │  1     2     3     4     5     6     7   │
  │                  │       [ Meeting ]      [ Launch App ]    │
  │ Mini-Month Nav   │  8     9    10    11    12    13    14   │
  └──────────────────┴──────────────────┴──────────────────┘
```

> **iCalendar (.ics) Format Handling**
> * **Standard iCal Parsing:** Native text parser for `VCALENDAR` and `VEVENT` structures, extracting `SUMMARY`, `DTSTART`, `DTEND`, `LOCATION`, `DESCRIPTION`, and `CATEGORIES`.
> * **Recurrence Engine:** Evaluates standard `RRULE` fields (e.g., daily, weekly, monthly, yearly repeat patterns) locally in memory without relying on external system libraries.
> * **Notification System:** Uses native Mac OS 9 Notification Manager APIs to trigger floating system alerts and sound cues for event reminders even when the main window is minimized.

---

## 3. Core Features & Functional Specification

### A. Multi-View Rendering Canvas
Features switchable Day, Week, Month, and Year view modes using custom Offscreen Graphics Worlds (`GWorld`) to prevent visual flicker during window redraws and scrolling.

### B. Color-Coded Categorization
Allows users to create custom color-coded local calendar categories (e.g., Personal, Work, Retro Computing). The left sidebar toggles visibility for individual calendars dynamically updating the grid.

### C. Interactive Event Creation & Inspector
Double-clicking any slot on the calendar grid opens a modern popover inspector dialog to set event titles, start/end times, location, repeat frequency, alerts, and custom notes.

---

## 4. Technical Stack & API Mapping

| Component | Technology / API | Description |
| :--- | :--- | :--- |
| **Storage & Parser** | Flat `.ics` Directory / Prefs DB | Parses standard `.ics` text files and stores structured events in a local preference file inside `System Folder:Preferences`. |
| **Grid Graphics Engine** | QuickDraw / GWorld Offscreen Buffers | Renders anti-flicker calendar grids, time columns, drag-and-drop event blocks, and color badges. |
| **Reminders & Alerts** | Notification Manager & Time Manager | Sets background high-precision timers to fire event alarm alerts and system sounds. |
| **Development Environment**| REALbasic 5.5 / CodeWarrior C/C++ | Custom canvas controls, date math algorithms, and string parsing routines. |

---

## 5. Work Effort & Development Estimates

| Development Phase | Core Tasks | Estimated Time |
| :--- | :--- | :--- |
| **1. iCalendar (.ics) Parser Engine** | Parsing `VCALENDAR`/`VEVENT` strings, date math routines, and `RRULE` recurrence evaluator. | 8 – 10 Hours |
| **2. Calendar Grid Canvas UI** | Drawing flicker-free Month, Week, and Day grid views with QuickDraw offscreen `GWorld`. | 10 – 12 Hours |
| **3. Event Inspector & Editing Dialogs** | Modal popovers for creating/editing events, date pickers, and category color toggles. | 6 – 8 Hours |
| **4. Reminders & Local Export Engine** | Integrating Notification Manager alarms and exporting modified schedules back to `.ics`. | 4 – 6 Hours |
| **TOTAL ESTIMATED EFFORT** | **Complete native, offline Mac OS 9 Calendar app** | **~28 – 36 Hours** |
