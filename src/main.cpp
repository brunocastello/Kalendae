/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * Main application entry point and event loop.
 */

#include <Quickdraw.h>
#include <Windows.h>
#include <Menus.h>
#include <Fonts.h>
#include <TextEdit.h>
#include <TextUtils.h>
#include <Dialogs.h>
#include <Events.h>
#include <Processes.h>
#include <OSUtils.h>
#include <MacMemory.h>
#include <Files.h>
#include <StandardFile.h>
#include <cstdio>
#include <cstring>
#include "calendar.h"
#include "ical_io.h"
#include "ical_parser.h"
#include "ui_renderer.h"
#include "event_dialog.h"
#include "calendar_dialog.h"

enum
{
    kAppleMenuID = 1,
    kFileMenuID = 2,
    kViewMenuID = 3,
    kEditMenuID = 4,
    kEventMenuID = 5,
    kExportMenuID = 6       // hierarchical submenu of File > Export
};

enum
{
    kFileNewEventItem = 1,
    kFileNewCalendarItem = 2,
    kFileImportItem = 4,
    kFileExportItem = 5,    // parent of the Export submenu
    kFileQuitItem = 7
};

enum
{
    kExportSelectedItem = 1,
    kExportAllItem = 2
};

enum
{
    kEditDeleteItem = 6
};

enum
{
    kEventEditItem = 1,
    kEventDeleteItem = 2
};

enum
{
    kConfirmDeleteAlertID = 129,
    kConfirmRecurringAlertID = 130,
    kConfirmDeleteCalendarAlertID = 144,
    kQuitSaveFailedAlertID = 141,
    kMessageAlertID = 142,
    kNoteAlertID = 143,
    kMaxAlertTitle = 40
};

enum
{
    kViewDayItem = 1,
    kViewWeekItem = 2,
    kViewMonthItem = 3,
    kViewYearItem = 4,
    kViewTodayItem = 6,
    kViewPreviousItem = 7,
    kViewNextItem = 8
};

// gCalendar/gUIRenderer are pointers, heap-allocated explicitly in main()
// rather than file-scope objects, because Retro68's classic-PowerPC crt0
// (libretro/ppcstart.c) jumps straight from __start to main() and never
// runs C++ global constructors -- a file-scope Calendar's std::map member
// would sit as zeroed-but-never-constructed memory, which is undefined
// behavior the instant anything touches it. A raw pointer needs no
// constructor, so it is safe to zero-initialize at file scope; the real
// object is built by an explicit `new` once main() is actually running.
static WindowPtr gMainWindow = NULL;
static Calendar *gCalendar = NULL;
static UIRenderer *gUIRenderer = NULL;
static MenuHandle gEventMenu = NULL;
static MenuHandle gEditMenu = NULL;
static MenuHandle gExportMenu = NULL;

// Height, in pixels, of the toolbar strip reserved at the top of the
// window content for the Day/Week/Month/Year buttons. Render*View draws
// below this line so it never overlaps the buttons.
static const short kToolbarHeight = 32;

// Width, in pixels, of the sidebar strip reserved on the left for the
// calendar checklist. Render*View draws to the right of this line.
// Adjustable at runtime via the splitter (see TrackSidebarResize);
// clamped to [kSidebarMinWidth, kSidebarMaxWidth]. The minimum has to
// stay wide enough for the mini-calendar's 7-column day grid to remain
// legible -- there isn't much room to give below that.
static short gSidebarWidth = 140;
static const short kSidebarMinWidth = 90;
static const short kSidebarMaxWidth = 220;

// Width, in pixels, of the draggable splitter between the sidebar and
// the main content (an iTunes-2-style divider, not a Control Manager
// control -- it's just a hit-tested strip handled in HandleWindowEvent).
static const short kSplitterWidth = 4;

static const short kWindowMinWidth = 400;
static const short kWindowMinHeight = 300;

// FindControl/TrackControl funnel every control click through one place,
// so refCon doubles as a tag: toolbar buttons store a ViewType (0..3),
// sidebar checkboxes store this base plus their category index, keeping
// the two kinds of control distinguishable without a separate registry.
static const long kCheckboxRefConBase = 1000;
static const long kPrevMonthRefCon = 2000;
static const long kNextMonthRefCon = 2001;
static const long kPrevPeriodRefCon = 2010;
static const long kTodayRefCon = 2011;
static const long kNextPeriodRefCon = 2012;
static const long kNewEventRefCon = 2020;
static const long kSearchRefCon = 2021;
static const long kTimeGridScrollRefCon = 3000;

// The calendar chosen by clicking its name or swatch in the sidebar (empty
// until first needed; SelectedCalendarName validates it). It receives new
// events and is what File > Export writes. Fixed buffer: no static ctors.
static char gSelectedCalendar[64] = "";

// True after a calendar row was the last thing clicked (cleared by clicks in
// the content, event selection, navigation and view changes): Edit > Delete
// and the Delete key then act on the selected calendar, if no event is selected.
static bool gSidebarHasFocus = false;

// Double-click detection for the sidebar rows (POD only).
static unsigned long gLastRowClickTicks = 0;
static int gLastRowClickIndex = -1;

// The month shown in the sidebar mini-calendar; moved by its own arrows and
// whenever the main date changes. 0 until first needed.
static short gMiniYear = 0, gMiniMonth = 0;

// The Day/Week hour-grid scroll bar. Recreated by SetupTimeGridScrollBar
// whenever RebuildControls tears the window's controls down.
static ControlHandle gScrollBar = NULL;

// Created lazily on first use (no global constructors; see above).
static ControlActionUPP gScrollActionUPP = NULL;

// Double-click detection state for HandleContentClick (POD only).
static unsigned long gLastClickTicks = 0;
static bool gLastClickValid = false;
static unsigned long gLastClickEventID = 0;
static int gLastClickYear = 0, gLastClickMonth = 0, gLastClickDay = 0;
static short gLastClickMinute = 0;

// Date of the selected occurrence (needed to delete just one occurrence of a
// recurring event). Plain shorts: a static Date would need a constructor.
static short gSelYear = 0, gSelMonth = 0, gSelDay = 0;
static ModalFilterUPP gConfirmFilterUPP = NULL;
static FileFilterUPP gImportFilterUPP = NULL;

static const char* kMonthNames[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

// Forward declarations
static void SetupApplication();
static void SetupMenuBar();
static void SetupToolbar();
static void SetupSidebar();
static void DrawSidebar();
static void SetupMiniCalendarButtons();
static void DrawMiniCalendar();
static bool HandleMiniCalendarClick(Point clickPoint);
static short MiniCalendarTop();
static void ChangeMiniMonth(int delta);
static void GoToDate(const Date& date);
static void StepPeriod(int direction);
static void SetupTitleNavButtons();
static void UpdateToolbarHilite();
static std::string SelectedCalendarName();
static void UpdateExportMenuItem();
static void SwitchToView(ViewType view);
static Rect ContentBounds();
static void RebuildControls();
static void SetupTimeGridScrollBar();
static void SyncTimeGridScrollBar();
static void HandleTimeGridScrollClick(ControlHandle control, short part, Point clickPoint);
static void HandleContentClick(EventRecord *event, Point clickPoint);
static void TrackSidebarResize();
static void TrackWindowResize(WindowPtr window, Point startPoint);
static bool GetPrefsFolderSpec(ConstStr255Param name, FSSpec *spec);
static OSErr GetEventsFileSpec(FSSpec *spec);
static void MarkWindowPrefsDirty();
static OSErr SavePrefs();
static bool LoadPrefs(Rect *outRect, short *outSidebarWidth);
static void LoadEvents();
static OSErr SaveEvents();
static void SaveIfDirty();
static void QuitApp();
static void UpdateMenuStates();
static void SelectEvent(unsigned long id, const Date& date);
static bool ConfirmAndDeleteEvent(unsigned long id, const Date& occurrenceDate);
static void RefreshAfterEdit();
static void InspectorUpdate(EventRecord *event);
static void LocalRectToGlobal(const Rect& local, Rect *global);
static Rect CenteredAnchor();
static void RefreshAfterEdit();
static void EditEventWithID(unsigned long id, const Rect& anchorLocal);
static void CreateEventWithInspector(Event& e, const Rect& anchorLocal);
static void NewEventFromHit(const HitRegion& hit);
static void NewEventAtNextHour();
static void ImportEvents();
static void ExportEvents(bool all);
static void EditSelectedEvent();
static void HandleEvent(EventRecord *event);
static void HandleMenuChoice(long menuChoice);
static void HandleWindowEvent(EventRecord *event);
static void HandleCommandEvent(EventRecord *event);
static void HandleUpdateEvent(EventRecord *event);

int main()
{
    // Grow the application heap to the full partition before anything is
    // allocated; otherwise MaxBlock() only sees the small initial zone.
    MaxApplZone();
    for (int i = 0; i < 4; i++)
        MoreMasters();

    // Construct the app's objects explicitly, now that we're definitely
    // running (see the comment on the globals above for why).
    gCalendar = new Calendar();
    gUIRenderer = new UIRenderer();

    // Initialize the application
    SetupApplication();

    // Load events from the events file (preferences, including the
    // calendar list, were already loaded by SetupApplication)
    LoadEvents();

    // The sidebar checkboxes mirror the category list, so they can only
    // be built once the prefs and the events have populated it. The mini
    // calendar's nav buttons are positioned below that list, so they have
    // the same ordering requirement.
    SetupSidebar();
    SetupMiniCalendarButtons();
    SetupTitleNavButtons();
    SetupTimeGridScrollBar();
    gMiniYear = (short)gCalendar->GetCurrentDate().year;
    gMiniMonth = (short)gCalendar->GetCurrentDate().month;

    // A Day/Week view restored from the prefs needs its grid scrolled into place
    ViewType startView = gCalendar->GetCurrentView();
    if (startView == DayView || startView == WeekView)
    {
        gUIRenderer->ComputeTimeGridLayout(*gCalendar);
        gUIRenderer->SetTimeGridScroll(gUIRenderer->DefaultTimeGridScroll(*gCalendar));
        SyncTimeGridScrollBar();
    }

    // Main event loop
    EventRecord event;
    while (true)
    {
        if (WaitNextEvent(everyEvent, &event, 10, NULL))
        {
            HandleEvent(&event);
        }

        // Write out whatever the last event changed
        SaveIfDirty();

        // Handle any background tasks
        gCalendar->ProcessBackgroundTasks();
    }

    return 0;
}

static void SetupApplication()
{
    // Classic Toolbox Manager initialization
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    FlushEvents(everyEvent, 0);

    SetupMenuBar();

    // Create main window. NewCWindow (color) rather than NewWindow, since
    // UIRenderer uses RGBBackColor for the Platinum background. Default is
    // 800x600, centered on the screen (menu bar excluded from the
    // centering math so the window doesn't creep up under it) -- unless a
    // saved size/position from a previous run is available.
    const short windowWidth = 800;
    const short windowHeight = 600;
    const short menuBarHeight = 20;
    Rect screenBounds = qd.screenBits.bounds;
    short left = screenBounds.left + ((screenBounds.right - screenBounds.left) - windowWidth) / 2;
    short top = screenBounds.top + menuBarHeight +
        ((screenBounds.bottom - screenBounds.top - menuBarHeight) - windowHeight) / 2;

    Rect windowRect;
    windowRect.top = top;
    windowRect.left = left;
    windowRect.bottom = top + windowHeight;
    windowRect.right = left + windowWidth;

    // Month is the default view; a saved prefs file overrides it, along
    // with the calendar list, before anything is drawn.
    gCalendar->SetCurrentView(MonthView);
    LoadPrefs(&windowRect, &gSidebarWidth);

    gMainWindow = NewCWindow(NULL, &windowRect, "\pKalendae",
                              true, documentProc, (WindowPtr)-1L, true, 0);

    SetPort(gMainWindow);
    ShowWindow(gMainWindow);

    gUIRenderer->Initialize(gMainWindow);

    SetupToolbar();
}

static ControlHandle MakeButton(short left, short top, short width, short height,
                                const char *label, long refCon)
{
    Rect r;
    r.top = top;
    r.bottom = top + height;
    r.left = left;
    r.right = left + width;

    Str255 title;
    title[0] = (unsigned char)strlen(label);
    memcpy(&title[1], label, title[0]);

    return NewControl(gMainWindow, &r, title, true, 0, 0, 1, pushButProc, refCon);
}

// Toolbar: [+] on the left, the view buttons centred, a (for now inactive)
// search button on the right.
static void SetupToolbar()
{
    const short buttonWidth = 60;
    const short buttonHeight = 20;
    const short gap = 5;
    const short top = 6;
    const char *labels[] = { "Day", "Week", "Month", "Year" };
    ViewType views[] = { DayView, WeekView, MonthView, YearView };

    short windowWidth = gMainWindow->portRect.right - gMainWindow->portRect.left;
    short totalWidth = 4 * buttonWidth + 3 * gap;
    short startX = (windowWidth - totalWidth) / 2;

    const short plusRight = 10 + 24;
    short searchLeft = windowWidth - 10 - 64;
    bool showSearch = true;

    // At narrow widths the centred group would run into [+] or Search:
    // shrink the buttons and gaps (min 44 / 3) and centre in the free space,
    // and drop the Search placeholder if even that doesn't fit.
    if (startX < plusRight + 8 || startX + totalWidth > searchLeft - 8)
    {
        short lo = plusRight + 8;
        short hi = searchLeft - 8;
        if (hi - lo < 4 * 44 + 3 * 3)
        {
            showSearch = false;
            hi = windowWidth - 10;
        }
        short gap2 = 3;
        short bw = (hi - lo - 3 * gap2) / 4;
        if (bw > buttonWidth) bw = buttonWidth;
        if (bw < 44) bw = 44;
        short total2 = 4 * bw + 3 * gap2;
        startX = lo + (hi - lo - total2) / 2;
        if (startX < lo) startX = lo;
        MakeButton(10, top, 24, buttonHeight, "+", kNewEventRefCon);
        for (int i = 0; i < 4; i++)
            MakeButton(startX + i * (bw + gap2), top, bw, buttonHeight, labels[i], (long)views[i]);
    }
    else
    {
        MakeButton(10, top, 24, buttonHeight, "+", kNewEventRefCon);
        for (int i = 0; i < 4; i++)
            MakeButton(startX + i * (buttonWidth + gap), top, buttonWidth, buttonHeight,
                       labels[i], (long)views[i]);
    }

    if (showSearch)
    {
        ControlHandle search = MakeButton(searchLeft, top, 64, buttonHeight,
                                          "Search", kSearchRefCon);
        if (search)
            HiliteControl(search, 255);     // placeholder until search exists
    }

    UpdateToolbarHilite();
}

// The active view's button is drawn pressed (hilite inButton). TrackControl
// resets the hilite to 0 when the mouse is released, so this is re-applied
// after every control click as well as after rebuilds and view switches.
static void UpdateToolbarHilite()
{
    if (!gMainWindow)
        return;
    short active = (short)gCalendar->GetCurrentView();
    ControlHandle c = ((WindowPeek)gMainWindow)->controlList;
    while (c)
    {
        long ref = GetControlReference(c);
        if (ref >= 0 && ref <= 3)
        {
            short want = (ref == active) ? inButton : 0;
            if ((**c).contrlHilite != want)
                HiliteControl(c, want);
        }
        c = (*c)->nextControl;
    }
}

// [<] [Today] [>] at the right end of the content title row, vertically
// centred in the renderer's title-row rect.
static void SetupTitleNavButtons()
{
    Rect row = gUIRenderer->GetTitleRowRect(ContentBounds());
    const short height = 20;
    short top = row.top + (short)((row.bottom - row.top - height) / 2);
    short right = row.right - 10;

    MakeButton(right - 22, top, 22, height, ">", kNextPeriodRefCon);
    MakeButton(right - 22 - 4 - 52, top, 52, height, "Today", kTodayRefCon);
    MakeButton(right - 22 - 4 - 52 - 4 - 22, top, 22, height, "<", kPrevPeriodRefCon);
}

static Rect ContentBounds()
{
    Rect bounds = gMainWindow->portRect;
    bounds.top += kToolbarHeight;
    bounds.left += gSidebarWidth;
    return bounds;
}

// The toolbar buttons, sidebar checkboxes, and mini-calendar nav buttons
// all have fixed positions computed at creation time, so both a window
// resize (toolbar re-centers) and a sidebar-width change (everything in
// the sidebar shifts) require tearing them down and recreating them
// rather than trying to reposition each one individually.
static void RebuildControls()
{
    KillControls(gMainWindow);
    SetupToolbar();
    SetupTitleNavButtons();
    SetupSidebar();
    SetupMiniCalendarButtons();
    SetupTimeGridScrollBar();
    SyncTimeGridScrollBar();
}

// Created hidden with a placeholder rect; SyncTimeGridScrollBar positions
// it against the renderer's time-grid view rect and shows it in Day/Week.
static void SetupTimeGridScrollBar()
{
    Rect r;
    r.top = 0;
    r.left = 0;
    r.bottom = 100;
    r.right = 16;
    gScrollBar = NewControl(gMainWindow, &r, "\p", false, 0, 0, 0, scrollBarProc,
                            kTimeGridScrollRefCon);
}

static void SyncTimeGridScrollBar()
{
    if (!gScrollBar)
        return;

    ViewType view = gCalendar->GetCurrentView();
    if (view != DayView && view != WeekView)
    {
        if ((**gScrollBar).contrlVis)
            HideControl(gScrollBar);
        return;
    }

    // The layout heights are recomputed from the calendar here, so the bar's
    // rect never depends on what was last rendered.
    gUIRenderer->ComputeTimeGridLayout(*gCalendar);
    Rect content = ContentBounds();
    Rect viewRect = gUIRenderer->GetTimeGridViewRect(content);

    // Standard scroll bar idiom: overlaps the window edge by one pixel and
    // runs down to the grow box.
    Rect r;
    r.top = viewRect.top - 1;
    r.bottom = gMainWindow->portRect.bottom - 14;
    r.right = gMainWindow->portRect.right + 1;
    r.left = r.right - 16;
    if (r.bottom <= r.top + 32)
        return;

    const Rect& cur = (**gScrollBar).contrlRect;
    if (cur.top != r.top || cur.left != r.left || cur.bottom != r.bottom || cur.right != r.right)
    {
        HideControl(gScrollBar);
        MoveControl(gScrollBar, r.left, r.top);
        SizeControl(gScrollBar, r.right - r.left, r.bottom - r.top);
    }

    short maxScroll = gUIRenderer->GetTimeGridMaxScroll(content);
    short scroll = gUIRenderer->GetTimeGridScroll();
    if (scroll > maxScroll) scroll = maxScroll;
    if (scroll < 0) scroll = 0;
    gUIRenderer->SetTimeGridScroll(scroll);

    SetControlMaximum(gScrollBar, maxScroll);
    SetControlValue(gScrollBar, scroll);
    if (!(**gScrollBar).contrlVis)
        ShowControl(gScrollBar);
}

static void ScrollTimeGridTo(short value)
{
    gUIRenderer->SetTimeGridScroll(value);
    gUIRenderer->RedrawTimeGrid(*gCalendar, ContentBounds());
}

// Arrow/page tracking: runs repeatedly while the mouse is held down.
static short TimeGridPageStep()
{
    Rect viewRect = gUIRenderer->GetTimeGridViewRect(ContentBounds());
    short pageStep = (short)(viewRect.bottom - viewRect.top - UIRenderer::kHourHeight);
    if (pageStep < UIRenderer::kHourHeight)
        pageStep = UIRenderer::kHourHeight;
    return pageStep;
}

// Moves the scroll bar (and the grid) by step pixels, clamped to its range.
static void ScrollTimeGridBy(ControlHandle control, short step)
{
    short oldValue = GetControlValue(control);
    long newValue = (long)oldValue + step;
    if (newValue < GetControlMinimum(control)) newValue = GetControlMinimum(control);
    if (newValue > GetControlMaximum(control)) newValue = GetControlMaximum(control);
    if (newValue == oldValue)
        return;

    SetControlValue(control, (short)newValue);
    ScrollTimeGridTo((short)newValue);
}

static pascal void TimeGridScrollAction(ControlHandle control, short part)
{
    short pageStep = TimeGridPageStep();

    short step = 0;
    switch (part)
    {
        case inUpButton:   step = -UIRenderer::kHourHeight; break;
        case inDownButton: step = UIRenderer::kHourHeight; break;
        case inPageUp:     step = -pageStep; break;
        case inPageDown:   step = pageStep; break;
        default: return;
    }

    ScrollTimeGridBy(control, step);
}

static void HandleTimeGridScrollClick(ControlHandle control, short part, Point clickPoint)
{
    if (part == inThumb)
    {
        if (TrackControl(control, clickPoint, NULL))
            ScrollTimeGridTo(GetControlValue(control));
        return;
    }

    if (!gScrollActionUPP)
        gScrollActionUPP = NewControlActionUPP(TimeGridScrollAction);
    TrackControl(control, clickPoint, gScrollActionUPP);
}

// Hit-map driven clicks in the content area: single click selects an event
// (or clears the selection on an empty slot); a double-click opens the
// inspector popover (edit an event / create at an empty slot), or in Year
// view jumps to that day.
static void HandleContentClick(EventRecord *event, Point clickPoint)
{
    gSidebarHasFocus = false;
    HitRegion hit;
    if (!gUIRenderer->HitTest(clickPoint, &hit))
    {
        // A click that misses every region still deselects.
        gLastClickValid = false;
        if (gUIRenderer->GetSelectedEvent() != 0)
        {
            SelectEvent(0, Date());
            Rect content = ContentBounds();
            SetPort(gMainWindow);
            InvalRect(&content);
        }
        return;
    }

    bool isDouble = false;
    if (gLastClickValid && (long)(event->when - gLastClickTicks) <= GetDblTime())
    {
        if (hit.eventID != 0)
            isDouble = (gLastClickEventID == hit.eventID);
        else
            isDouble = gLastClickEventID == 0 &&
                       gLastClickYear == hit.date.year && gLastClickMonth == hit.date.month &&
                       gLastClickDay == hit.date.day && gLastClickMinute == hit.minute;
    }

    if (isDouble)
    {
        gLastClickValid = false;
    }
    else
    {
        gLastClickValid = true;
        gLastClickTicks = event->when;
        gLastClickEventID = hit.eventID;
        gLastClickYear = hit.date.year;
        gLastClickMonth = hit.date.month;
        gLastClickDay = hit.date.day;
        gLastClickMinute = hit.minute;
    }

    Rect content = ContentBounds();
    SetPort(gMainWindow);

    unsigned long selected = gUIRenderer->GetSelectedEvent();
    SelectEvent(hit.eventID, hit.date);
    if (hit.eventID != selected)
        InvalRect(&content);

    if (!isDouble)
        return;

    if (gCalendar->GetCurrentView() == YearView)
    {
        // Year days jump to Day view on that date.
        gCalendar->SetCurrentDate(hit.date);
        gMiniYear = (short)hit.date.year;
        gMiniMonth = (short)hit.date.month;
        SwitchToView(DayView);
    }
    else if (hit.eventID != 0)
    {
        EditEventWithID(hit.eventID, hit.r);
    }
    else
    {
        NewEventFromHit(hit);
    }
}

// Classic XOR-line drag idiom (as used for e.g. Finder column resizing):
// track the mouse with StillDown/GetMouse, drawing and erasing a single
// vertical line via patXor rather than repainting the whole window on
// every pixel of movement, then commit the real change once on mouse-up.
static void TrackSidebarResize()
{
    SetPort(gMainWindow);
    Rect winRect = gMainWindow->portRect;

    PenMode(patXor);
    short lastX = gSidebarWidth;
    MoveTo(lastX, kToolbarHeight);
    LineTo(lastX, winRect.bottom);

    Point pt;
    while (StillDown())
    {
        GetMouse(&pt);
        short newX = pt.h;
        if (newX < kSidebarMinWidth) newX = kSidebarMinWidth;
        if (newX > kSidebarMaxWidth) newX = kSidebarMaxWidth;

        if (newX != lastX)
        {
            MoveTo(lastX, kToolbarHeight);
            LineTo(lastX, winRect.bottom);

            lastX = newX;
            MoveTo(lastX, kToolbarHeight);
            LineTo(lastX, winRect.bottom);
        }
    }

    MoveTo(lastX, kToolbarHeight);
    LineTo(lastX, winRect.bottom);
    PenMode(patCopy);

    gSidebarWidth = lastX;
    RebuildControls();
    InvalRect(&winRect);

    MarkWindowPrefsDirty();
}

static void TrackWindowResize(WindowPtr window, Point startPoint)
{
    Rect sizeLimits;
    sizeLimits.top = kWindowMinHeight;
    sizeLimits.left = kWindowMinWidth;
    sizeLimits.bottom = 32000;
    sizeLimits.right = 32000;

    long newSize = GrowWindow(window, startPoint, &sizeLimits);
    if (newSize == 0)
        return;

    short newWidth = (short)(newSize & 0xFFFF);
    short newHeight = (short)(newSize >> 16);
    SizeWindow(window, newWidth, newHeight, true);

    RebuildControls();
    SetPort(window);
    InvalRect(&window->portRect);

    MarkWindowPrefsDirty();
}

// Real classic document windows have a ~19px title bar above portRect's
// local (0,0); saving/restoring the *structure* rect (what NewCWindow
// takes) needs that added back in, since portRect only ever exposes the
// content area.
static const short kTitleBarHeight = 19;

// Prefs file layout (big-endian shorts), version 2:
//   short version, left, top, width, height, sidebarWidth, lastView, calendarCount
//   calendarCount x { Str63 name[64]; uchar r, g, b, visible }
// Version 1 was just the five window shorts (10 bytes, no version field).
// Not declared by Multiversal's Folders.h
static const short kOnSystemDisk = -32768;

static const short kPrefsVersion = 2;
static const size_t kPrefsV1Size = 10;
static const size_t kPrefsHeaderSize = 16;
static const size_t kPrefsCalendarSize = 68;
static const short kMaxPrefsCalendars = 64;

static const unsigned long kSaveRetryTicks = 300;   // ~5 s between failed attempts
static bool gEventsRetryPending = false;
static bool gPrefsRetryPending = false;
static unsigned long gEventsRetryTick = 0;
static unsigned long gPrefsRetryTick = 0;

// Fills in an FSSpec for a file inside the Preferences folder. Per Inside
// Macintosh, FSMakeFSSpec fills the spec in and returns fnfErr when the
// file doesn't exist yet -- that's expected the first time this app runs
// and is not a failure the caller needs to check for.
static bool GetPrefsFolderSpec(ConstStr255Param name, FSSpec *spec)
{
    short vRefNum;
    long dirID;
    if (FindFolder(kOnSystemDisk, kPreferencesFolderType, true, &vRefNum, &dirID) != noErr)
        return false;

    FSMakeFSSpec(vRefNum, dirID, name, spec);
    return true;
}

// Fills in the spec for "Kalendae Events" inside
// System Folder:Application Support:Kalendae, creating that folder on first
// use. Falls back to the Preferences folder if there is no Application
// Support folder (very old systems) or the folder can't be made.
static OSErr GetEventsFileSpec(FSSpec *spec)
{
    short vRefNum;
    long dirID;
    if (FindFolder(kOnSystemDisk, 'asup', true, &vRefNum, &dirID) != noErr)
        return GetPrefsFolderSpec("\pKalendae Events", spec) ? (OSErr)noErr : (OSErr)fnfErr;

    // From here on a failure is an error, not a reason to scatter the data
    // into another folder.
    FSSpec folderSpec;
    long folderID = 0;
    OSErr err = FSMakeFSSpec(vRefNum, dirID, "\pKalendae", &folderSpec);
    if (err == fnfErr)
    {
        err = FSpDirCreate(&folderSpec, smSystemScript, &folderID);
    }
    else if (err == noErr)
    {
        CInfoPBRec pb;
        memset(&pb, 0, sizeof(pb));
        pb.dirInfo.ioNamePtr = folderSpec.name;
        pb.dirInfo.ioVRefNum = folderSpec.vRefNum;
        pb.dirInfo.ioDrDirID = folderSpec.parID;    // input: the parent folder
        pb.dirInfo.ioFDirIndex = 0;
        err = PBGetCatInfoSync(&pb);
        if (err == noErr)
        {
            if (pb.dirInfo.ioFlAttrib & 0x10)
                folderID = pb.dirInfo.ioDrDirID;    // output: the folder's own ID
            else
                err = dupFNErr;     // a file is in the way
        }
    }
    if (err != noErr)
        return err;

    err = FSMakeFSSpec(folderSpec.vRefNum, folderID, "\pKalendae Events", spec);
    return err == fnfErr ? (OSErr)noErr : err;    // not existing yet is fine
}

static void PutShort(std::string& s, short v)
{
    s += (char)((v >> 8) & 0xFF);
    s += (char)(v & 0xFF);
}

static short GetShortAt(const std::string& s, size_t pos)
{
    return (short)(((unsigned char)s[pos] << 8) | (unsigned char)s[pos + 1]);
}

// Window moves and resizes just flag the prefs; the main loop writes them.
static void MarkWindowPrefsDirty()
{
    gCalendar->MarkPrefsDirty();
}

static OSErr SavePrefs()
{
    if (!gMainWindow)
        return paramErr;

    FSSpec spec;
    if (!GetPrefsFolderSpec("\pKalendae Preferences", &spec))
        return fnfErr;

    SetPort(gMainWindow);
    Point contentTopLeft;
    contentTopLeft.h = 0;
    contentTopLeft.v = 0;
    LocalToGlobal(&contentTopLeft);

    const std::map<std::string, ColorRGB>& categories = gCalendar->GetCategories();
    short calendarCount = (short)categories.size();
    if (calendarCount > kMaxPrefsCalendars)
        calendarCount = kMaxPrefsCalendars;

    std::string data;
    PutShort(data, kPrefsVersion);
    PutShort(data, contentTopLeft.h);
    PutShort(data, contentTopLeft.v - kTitleBarHeight);
    PutShort(data, gMainWindow->portRect.right - gMainWindow->portRect.left);
    PutShort(data, gMainWindow->portRect.bottom - gMainWindow->portRect.top);
    PutShort(data, gSidebarWidth);
    PutShort(data, (short)gCalendar->GetCurrentView());
    PutShort(data, calendarCount);

    short written = 0;
    for (std::map<std::string, ColorRGB>::const_iterator it = categories.begin();
         it != categories.end() && written < calendarCount; ++it, ++written)
    {
        std::string name = it->first;
        if (name.size() > 63)
            name.resize(63);
        char field[64];
        memset(field, 0, sizeof(field));
        field[0] = (char)name.size();
        memcpy(&field[1], name.data(), name.size());
        data.append(field, sizeof(field));
        data += (char)it->second.r;
        data += (char)it->second.g;
        data += (char)it->second.b;
        data += (char)(gCalendar->IsCategoryVisible(it->first) ? 1 : 0);
    }

    OSErr err = WriteStringToFile(spec, data, 'pref', 'KALE');
    if (err == noErr)
        gCalendar->ClearPrefsDirty();
    return err;
}

// Reads the prefs file: window geometry (returned), last view and calendar
// list (applied to gCalendar; a non-empty list replaces the defaults). Any
// inconsistency leaves the defaults alone.
static bool LoadPrefs(Rect *outRect, short *outSidebarWidth)
{
    FSSpec spec;
    if (!GetPrefsFolderSpec("\pKalendae Preferences", &spec))
        return false;

    std::string data;
    if (ReadFileToString(spec, data) != noErr)
        return false;

    size_t pos;
    short calendarCount = 0;
    short lastView = -1;
    if (data.size() == kPrefsV1Size)
    {
        pos = 0;
    }
    else if (data.size() >= kPrefsHeaderSize && GetShortAt(data, 0) == kPrefsVersion)
    {
        pos = 2;
        lastView = GetShortAt(data, 12);
        calendarCount = GetShortAt(data, 14);
        if (calendarCount < 0 || calendarCount > kMaxPrefsCalendars ||
            data.size() != kPrefsHeaderSize + (size_t)calendarCount * kPrefsCalendarSize)
            return false;
    }
    else
    {
        return false;
    }

    short left = GetShortAt(data, pos);
    short top = GetShortAt(data, pos + 2);
    short width = GetShortAt(data, pos + 4);
    short height = GetShortAt(data, pos + 6);
    short sidebarWidth = GetShortAt(data, pos + 8);

    // Validate the calendar list before touching the model
    for (short i = 0; i < calendarCount; i++)
    {
        size_t at = kPrefsHeaderSize + (size_t)i * kPrefsCalendarSize;
        unsigned char nameLen = (unsigned char)data[at];
        if (nameLen == 0 || nameLen > 63)
            return false;
    }

    if (width >= kWindowMinWidth && height >= kWindowMinHeight)
    {
        outRect->left = left;
        outRect->top = top;
        outRect->right = (short)(left + width);
        outRect->bottom = (short)(top + height);

        if (sidebarWidth >= kSidebarMinWidth && sidebarWidth <= kSidebarMaxWidth)
            *outSidebarWidth = sidebarWidth;
    }

    if (lastView >= 0 && lastView <= 3)
        gCalendar->SetCurrentView((ViewType)lastView);

    if (calendarCount > 0)
    {
        gCalendar->ClearCategories();
        for (short i = 0; i < calendarCount; i++)
        {
            size_t at = kPrefsHeaderSize + (size_t)i * kPrefsCalendarSize;
            std::string name(data, at + 1, (unsigned char)data[at]);
            gCalendar->AddCategory(name, ColorRGB((unsigned char)data[at + 64],
                                                  (unsigned char)data[at + 65],
                                                  (unsigned char)data[at + 66]));
            gCalendar->SetCategoryVisible(name, data[at + 67] != 0);
        }
    }

    gCalendar->ClearPrefsDirty();
    return true;
}

// Multiversal has no declaration for ReadLocation (the Date & Time control
// panel's location, kept in extended PRAM), so declare it and its struct here;
// InterfaceLib exports the routine.
struct MachineLocation
{
    long latitude;
    long longitude;
    union
    {
        char dlsDelta;
        long gmtDelta;
    } u;
};
extern "C" void ReadLocation(MachineLocation *loc);

// local = UTC + offset, from the Date & Time control panel's location
static long GetGMTOffsetSeconds()
{
    MachineLocation loc;
    ReadLocation(&loc);
    long delta = loc.u.gmtDelta & 0x00FFFFFF;   // 24-bit signed seconds
    if (delta & 0x00800000)
        delta -= 0x01000000;
    return delta;
}

// Set when the events file couldn't be read or set aside safely, so saving
// would destroy data. Events then stay in memory only for this session.
static bool gEventsNoSave = false;
static bool gSaveFailureShown = false;

// Modal filter for one-button alerts: only keeps the other windows repainting.
static pascal Boolean AlertUpdateFilterProc(DialogPtr dlg, EventRecord *event, short *itemHit)
{
    (void)itemHit;
    if (event->what == updateEvt && (WindowPtr)event->message != (WindowPtr)dlg)
    {
        GrafPtr saved;
        GetPort(&saved);
        HandleUpdateEvent(event);
        SetPort(saved);
    }
    return false;
}

static ModalFilterUPP gAlertUpdateFilterUPP = NULL;

static ModalFilterUPP AlertUpdateFilter()
{
    if (!gAlertUpdateFilterUPP)
        gAlertUpdateFilterUPP = NewModalFilterUPP(AlertUpdateFilterProc);
    return gAlertUpdateFilterUPP;
}

// Shows a one-button message alert (ALRT 142; ^0 = text, up to 255 bytes).
static void ShowMessageAlert(const std::string& text)
{
    Str255 p;
    size_t n = text.size() > 255 ? 255 : text.size();
    p[0] = (unsigned char)n;
    memcpy(&p[1], text.data(), n);
    ParamText(p, "\p", "\p", "\p");
    InitCursor();
    StopAlert(kMessageAlertID, AlertUpdateFilter());
    SetPort(gMainWindow);
}

// Renames an events file we couldn't understand out of the way, so the next
// save can't overwrite it. Returns the name it got in `newName`.
static OSErr SetAsideEventsFile(const FSSpec& specIn, std::string& newName)
{
    FSSpec spec = specIn;
    for (int attempt = 1; attempt <= 9; attempt++)
    {
        std::string name = "Kalendae Events (damaged)";
        if (attempt > 1)
        {
            char suffix[8];
            snprintf(suffix, sizeof(suffix), " %d", attempt);
            name += suffix;
        }
        Str255 pname;
        pname[0] = (unsigned char)name.size();   // <= 31 characters
        memcpy(&pname[1], name.data(), name.size());

        FSSpec probe;
        OSErr err = FSMakeFSSpec(spec.vRefNum, spec.parID, pname, &probe);
        if (err == fnfErr)
        {
            err = FSpRename(&spec, pname);
            if (err == noErr)
                newName = name;
            return err;
        }
        if (err != noErr)
            return err;
    }
    return dupFNErr;
}

static void HandleUnreadableEventsFile(const FSSpec& spec)
{
    std::string newName;
    if (SetAsideEventsFile(spec, newName) == noErr)
    {
        ShowMessageAlert("Kalendae couldn't read its events file. It was renamed to \322" +
                         newName + "\323 and a new one will be created.");
    }
    else
    {
        gEventsNoSave = true;
        ShowMessageAlert("Kalendae couldn't read its events file and couldn't set it aside. "
                         "Changes to events won't be saved in this session.");
    }
}

// A missing events file just means an empty calendar; an unreadable one is
// set aside rather than overwritten.
static void LoadEvents()
{
    FSSpec spec;
    OSErr err = GetEventsFileSpec(&spec);
    if (err != noErr)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", (int)err);
        gEventsNoSave = true;
        ShowMessageAlert(std::string("Kalendae couldn't open its events folder (error ") + buf +
                         "). Changes to events won't be saved in this session.");
        gCalendar->ClearDirty();
        return;
    }

    std::string text;
    err = ReadFileToString(spec, text);

    if (err == fnfErr)
    {
        // An interrupted save may have left the new copy under the temp name
        unsigned char tempName[32];
        unsigned char len = spec.name[0] > 30 ? 30 : spec.name[0];
        tempName[0] = (unsigned char)(len + 1);
        memcpy(&tempName[1], &spec.name[1], len);
        tempName[len + 1] = '~';
        FSSpec temp;
        if (FSMakeFSSpec(spec.vRefNum, spec.parID, tempName, &temp) == noErr &&
            FSpRename(&temp, spec.name) == noErr)
            err = ReadFileToString(spec, text);
    }

    if (err == fnfErr)
    {
        // First run: nothing to load
    }
    else if (err == memFullErr)
    {
        gEventsNoSave = true;
        ShowMessageAlert("The events file is too large for Kalendae's memory partition. "
                         "Choose Get Info on Kalendae and increase the Preferred Size. "
                         "Changes to events won't be saved in this session.");
    }
    else if (err != noErr)
    {
        HandleUnreadableEventsFile(spec);
    }
    else if (!gCalendar->LoadEventsFromText(text, GetGMTOffsetSeconds()))
    {
        HandleUnreadableEventsFile(spec);
    }
    gCalendar->ClearDirty();
}

static OSErr SaveEvents()
{
    if (gEventsNoSave)
    {
        gCalendar->ClearDirty();
        return noErr;
    }

    FSSpec spec;
    OSErr err = GetEventsFileSpec(&spec);
    if (err != noErr)
        return err;

    unsigned long secs = 0;
    GetDateTime(&secs);
    long offset = GetGMTOffsetSeconds();
    std::string text = gCalendar->SerializeEvents(offset, (unsigned long)((long)secs - offset));

    err = WriteStringToFile(spec, text, 'TEXT', 'KALE');
    if (err == noErr)
        gCalendar->ClearDirty();
    return err;
}

// Tells the user once per session that background saves are failing; the
// retries after that are silent.
static void ReportSaveFailure(OSErr err)
{
    if (gSaveFailureShown)
        return;
    gSaveFailureShown = true;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", (int)err);
    ShowMessageAlert(std::string("Your changes couldn't be saved (error ") + buf +
                     "). Kalendae will keep trying.");
}

// Called every pass of the main loop. A failed write keeps the dirty flag
// and is retried no more than once per kSaveRetryTicks.
static void SaveIfDirty()
{
    unsigned long now = TickCount();

    if (gCalendar->IsDirty() &&
        (!gEventsRetryPending || (long)(now - gEventsRetryTick) >= 0))
    {
        OSErr err = SaveEvents();
        gEventsRetryPending = (err != noErr);
        gEventsRetryTick = now + kSaveRetryTicks;
        if (err != noErr)
            ReportSaveFailure(err);
    }

    if (gCalendar->IsPrefsDirty() &&
        (!gPrefsRetryPending || (long)(now - gPrefsRetryTick) >= 0))
    {
        OSErr err = SavePrefs();
        gPrefsRetryPending = (err != noErr);
        gPrefsRetryTick = now + kSaveRetryTicks;
        if (err != noErr)
            ReportSaveFailure(err);
    }
}

// Single path to quit through, so every trigger (File > Quit, Cmd-Q, the
// close box) saves first rather than relying on each call site to
// remember to. A failed save offers Cancel.
static void QuitApp()
{
    OSErr err = noErr;
    if (gCalendar->IsDirty())
        err = SaveEvents();
    gCalendar->MarkPrefsDirty();    // window position may have changed
    OSErr prefsErr = SavePrefs();
    if (err == noErr)
        err = prefsErr;

    if (err != noErr)
    {
        Str255 num;
        NumToString((long)err, num);
        ParamText(num, "\p", "\p", "\p");
        InitCursor();
        short item = StopAlert(kQuitSaveFailedAlertID, NULL);
        SetPort(gMainWindow);
        if (item == 2)
            return;     // Cancel: back to the app
    }
    ExitToShell();
}

// ---------------------------------------------------------------- inspector

static void InspectorUpdate(EventRecord *event)
{
    HandleUpdateEvent(event);
}

static void LocalRectToGlobal(const Rect& local, Rect *global)
{
    SetPort(gMainWindow);
    Point tl, br;
    tl.v = local.top;
    tl.h = local.left;
    br.v = local.bottom;
    br.h = local.right;
    LocalToGlobal(&tl);
    LocalToGlobal(&br);
    global->top = tl.v;
    global->left = tl.h;
    global->bottom = br.v;
    global->right = br.h;
}

// Fallback anchor (window-local): a zero-width rect placed so the ~310px
// popover, which sits 8px right of its anchor, ends up centred in the content.
static Rect CenteredAnchor()
{
    Rect c = ContentBounds();
    short cx = (c.left + c.right) / 2 - 163;
    short cy = (c.top + c.bottom) / 2;
    Rect r;
    r.top = cy;
    r.bottom = cy;
    r.left = cx;
    r.right = cx;
    return r;
}

static void RefreshAfterEdit()
{
    SyncTimeGridScrollBar();
    SetPort(gMainWindow);
    Rect content = ContentBounds();
    InvalRect(&content);
}

static void SelectEvent(unsigned long id, const Date& date)
{
    if (id != 0)
        gSidebarHasFocus = false;
    gUIRenderer->SetSelectedEvent(id);
    gSelYear = (short)date.year;
    gSelMonth = (short)date.month;
    gSelDay = (short)date.day;
}

// Esc and Cmd-. cancel (item 2); Return/Enter take the default (item 1).
static void FlashAlertButton(DialogPtr dlg, short item)
{
    short type;
    Handle h;
    Rect r;
    GetDialogItem(dlg, item, &type, &h, &r);
    if (!h)
        return;
    long ignored;
    HiliteControl((ControlHandle)h, 1);
    Delay(8, &ignored);
    HiliteControl((ControlHandle)h, 0);
}

static pascal Boolean ConfirmFilter(DialogPtr dlg, EventRecord *event, short *itemHit)
{
    // Keep the main window repainting while the alert is up.
    if (event->what == updateEvt && (WindowPtr)event->message != (WindowPtr)dlg)
    {
        GrafPtr saved;
        GetPort(&saved);
        HandleUpdateEvent(event);
        SetPort(saved);
        return false;
    }
    if (event->what == keyDown)
    {
        char ch = (char)(event->message & charCodeMask);
        if (ch == 0x1B || ((event->modifiers & cmdKey) && ch == '.'))
        {
            FlashAlertButton(dlg, 2);
            *itemHit = 2;
            return true;
        }
        if (ch == 0x0D || ch == 0x03)
        {
            FlashAlertButton(dlg, 1);
            *itemHit = 1;
            return true;
        }
    }
    return false;
}

// Asks first, then deletes. Recurring events offer one occurrence (EXDATE)
// or the whole series. Returns true if something was deleted.
static bool ConfirmAndDeleteEvent(unsigned long id, const Date& occurrenceDate)
{
    const Event *found = gCalendar->FindEvent(id);
    if (!found)
        return false;
    bool recurring = found->IsRecurring();
    std::string title = found->GetTitle();

    Str255 p;
    size_t n = title.length();
    if (n > kMaxAlertTitle)
    {
        memcpy(p + 1, title.c_str(), kMaxAlertTitle - 1);
        p[kMaxAlertTitle] = 0xC9;   // ellipsis
        p[0] = kMaxAlertTitle;
    }
    else
    {
        memcpy(p + 1, title.c_str(), n);
        p[0] = (unsigned char)n;
    }

    if (!gConfirmFilterUPP)
        gConfirmFilterUPP = NewModalFilterUPP(ConfirmFilter);
    ParamText(p, "\p", "\p", "\p");
    short item = CautionAlert(recurring ? kConfirmRecurringAlertID : kConfirmDeleteAlertID,
                              gConfirmFilterUPP);
    SetPort(gMainWindow);

    if (recurring && item == 1)
        gCalendar->AddExDate(id, occurrenceDate);
    else if ((recurring && item == 3) || (!recurring && item == 1))
        gCalendar->RemoveEvent(id);
    else
        return false;

    SelectEvent(0, Date());
    RefreshAfterEdit();
    return true;
}

static void DeleteSelectedEvent()
{
    unsigned long id = gUIRenderer->GetSelectedEvent();
    if (id != 0)
        ConfirmAndDeleteEvent(id, Date(gSelYear, gSelMonth, gSelDay));
}

static void EditEventWithID(unsigned long id, const Rect& anchorLocal)
{
    const Event *found = gCalendar->FindEvent(id);
    if (!found)
        return;
    Event e = *found;

    Rect anchor;
    LocalRectToGlobal(anchorLocal, &anchor);
    short result = ShowEventInspector(e, false, anchor, *gCalendar, InspectorUpdate);
    SetPort(gMainWindow);

    if (result == kInspectorSaved)
    {
        gCalendar->UpdateEvent(e);
    }
    else if (result == kInspectorDeleted)
    {
        // Cancelling the confirmation leaves everything as it was.
        ConfirmAndDeleteEvent(id, Date(gSelYear, gSelMonth, gSelDay));
        return;
    }
    else
    {
        return;
    }
    RefreshAfterEdit();
}

static void SetCategoryVisibleByIndex(int index, bool visible);
static bool CategoryAtIndex(int index, std::string& name, ColorRGB& color);

// A new event in an unchecked calendar would vanish; Apple shows the calendar.
static void EnsureCategoryVisible(const std::string& name)
{
    if (gCalendar->IsCategoryVisible(name))
        return;
    std::string n;
    ColorRGB c;
    for (int i = 0; CategoryAtIndex(i, n, c); i++)
        if (n == name)
        {
            SetCategoryVisibleByIndex(i, true);
            return;
        }
}

static void CreateEventWithInspector(Event& e, const Rect& anchorLocal)
{
    Rect anchor;
    LocalRectToGlobal(anchorLocal, &anchor);
    short result = ShowEventInspector(e, true, anchor, *gCalendar, InspectorUpdate);
    SetPort(gMainWindow);

    if (result == kInspectorSaved)
    {
        unsigned long id = gCalendar->AddEvent(e);
        EnsureCategoryVisible(e.GetCategory());
        SelectEvent(id, e.GetStartTime());
        RefreshAfterEdit();
    }
}

static void SetSelectedCategory(Event& e)
{
    std::string name = SelectedCalendarName();
    if (!name.empty())
        e.SetCategory(name);
}

// Double-click on an empty slot. Month slots and the Day/Week all-day strip
// (minute -1) create all-day events; hour slots create one-hour events.
static void NewEventFromHit(const HitRegion& hit)
{
    Event e("New Event", hit.date, hit.date);
    SetSelectedCategory(e);
    if (hit.minute < 0)
    {
        e.SetAllDay(true);
        e.SetStartMinute(9 * 60);
        e.SetEndMinute(10 * 60);
    }
    else
    {
        short end = (short)(hit.minute + 60);
        if (end > 1440) end = 1440;
        e.SetStartMinute(hit.minute);
        e.SetEndMinute(end);
    }
    CreateEventWithInspector(e, hit.r);
}

// Looks for the visible empty slot for date/minute by probing the hit map
// (UIRenderer only exposes point and event queries).
static bool FindSlotRect(const Date& date, short minute, Rect *outLocal)
{
    ViewType view = gCalendar->GetCurrentView();
    if (view == YearView)
        return false;

    Rect c = ContentBounds();
    for (short y = c.top + 1; y < c.bottom; y += 8)
    {
        for (short x = c.left + 1; x < c.right; x += 16)
        {
            Point p;
            p.v = y;
            p.h = x;
            HitRegion h;
            if (gUIRenderer->HitTest(p, &h) && h.eventID == 0 && h.date == date &&
                (view == MonthView || h.minute == minute))
            {
                *outLocal = h.r;
                return true;
            }
        }
    }
    return false;
}

// File > New Event: current date, next whole hour (11 PM - midnight at most).
static void NewEventAtNextHour()
{
    unsigned long secs;
    DateTimeRec rec;
    GetDateTime(&secs);
    SecondsToDate(secs, &rec);

    short start = (short)((rec.hour + 1) * 60);
    if (start > 23 * 60)
        start = 23 * 60;

    Date date = gCalendar->GetCurrentDate();
    Event e("New Event", date, date);
    SetSelectedCategory(e);
    e.SetStartMinute(start);
    e.SetEndMinute((short)(start + 60));

    Rect anchor;
    if (!FindSlotRect(date, start, &anchor))
        anchor = CenteredAnchor();
    CreateEventWithInspector(e, anchor);
}

static void EditSelectedEvent()
{
    unsigned long id = gUIRenderer->GetSelectedEvent();
    if (id == 0)
        return;

    Rect anchor = CenteredAnchor();
    HitRegion h;
    Rect content = ContentBounds();
    Rect visible;
    if (gUIRenderer->FindHitForEvent(id, Date(gSelYear, gSelMonth, gSelDay), &h))
    {
        SelectEvent(id, h.date);
        if (SectRect(&h.r, &content, &visible))
            anchor = h.r;
    }
    EditEventWithID(id, anchor);
}

// Sidebar calendar rows: [checkbox][colour swatch][name]. SetupSidebar,
// DrawSidebar, MiniCalendarTop and the row hit-test all use these.
static const short kSidebarListTop = kToolbarHeight + 28;
static const short kSidebarRowHeight = 18;
static const short kSidebarRowGap = 4;
static const short kSidebarCheckLeft = 14;
static const short kSidebarCheckSize = 16;
static const short kSidebarSwatchLeft = 36;
static const short kSidebarSwatchSize = 10;
static const short kSidebarNameLeft = 52;

static short SidebarRowTop(int index)
{
    return kSidebarListTop + (short)(index * (kSidebarRowHeight + kSidebarRowGap));
}

// The one place a checkbox index maps to a category (std::map order, same
// as SetupSidebar and DrawSidebar iterate).
static bool CategoryAtIndex(int index, std::string& name, ColorRGB& color)
{
    const std::map<std::string, ColorRGB>& categories = gCalendar->GetCategories();
    int i = 0;
    for (std::map<std::string, ColorRGB>::const_iterator it = categories.begin();
         it != categories.end(); ++it, ++i)
    {
        if (i == index)
        {
            name = it->first;
            color = it->second;
            return true;
        }
    }
    return false;
}

// The selected calendar's name; falls back to the first calendar when the
// stored one is unset or gone, and updates the stored copy.
static std::string SelectedCalendarName()
{
    const std::map<std::string, ColorRGB>& categories = gCalendar->GetCategories();
    if (categories.empty())
        return std::string();
    if (gSelectedCalendar[0] != '\0' && categories.find(gSelectedCalendar) != categories.end())
        return std::string(gSelectedCalendar);

    strncpy(gSelectedCalendar, categories.begin()->first.c_str(), sizeof(gSelectedCalendar) - 1);
    gSelectedCalendar[sizeof(gSelectedCalendar) - 1] = '\0';
    return std::string(gSelectedCalendar);
}

// ------------------------------------------------------ calendar management

static void SetSelectedCalendar(const std::string& name)
{
    strncpy(gSelectedCalendar, name.c_str(), sizeof(gSelectedCalendar) - 1);
    gSelectedCalendar[sizeof(gSelectedCalendar) - 1] = '\0';
}

// Opens Calendar Info for `name` and applies the outcome to the window.
static void EditCalendarInfo(const std::string& nameIn)
{
    std::string name(nameIn);
    bool wasSelected = (name == SelectedCalendarName());
    std::string newName = name;
    bool ok = ShowCalendarInfo(*gCalendar, name, InspectorUpdate, &newName);
    SetPort(gMainWindow);
    if (!ok)
        return;

    if (wasSelected)
        SetSelectedCalendar(newName);
    gLastRowClickIndex = -1;
    UpdateExportMenuItem();
    RebuildControls();      // renaming can reorder the rows
    InvalRect(&gMainWindow->portRect);
}

// File > New Calendar: "Untitled" (numbered on a clash), next palette colour,
// selected, then straight into Calendar Info. Cancelling there keeps it.
static void NewCalendar()
{
    const std::map<std::string, ColorRGB>& cats = gCalendar->GetCategories();
    if (cats.size() >= (size_t)kMaxPrefsCalendars)
    {
        ShowMessageAlert("Kalendae supports at most 64 calendars.");
        return;
    }

    std::string name = "Untitled";
    for (int n = 2; cats.find(name) != cats.end() && n < 10000; n++)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", n);
        name = std::string("Untitled ") + buf;
    }
    if (cats.find(name) != cats.end())
        return;

    gCalendar->AddCategory(name, gCalendar->NextPaletteColor());
    gCalendar->SetCategoryVisible(name, true);
    SetSelectedCalendar(name);
    gSidebarHasFocus = true;
    gLastClickValid = false;
    gLastRowClickIndex = -1;
    UpdateExportMenuItem();
    RebuildControls();
    SetPort(gMainWindow);
    InvalRect(&gMainWindow->portRect);

    EditCalendarInfo(name);
}

// Asks first, then deletes the calendar and all of its events. The last
// calendar can't be deleted (returns false without asking).
static bool ConfirmAndDeleteCalendar(const std::string& nameIn)
{
    std::string name(nameIn);
    const std::map<std::string, ColorRGB>& cats = gCalendar->GetCategories();
    if (cats.size() <= 1 || cats.find(name) == cats.end())
        return false;

    int count = gCalendar->CountEventsInCategory(name);
    std::string tail;
    if (count == 0)
    {
        tail = "It has no events.";
    }
    else
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", count);
        tail = std::string("Its ") + buf +
               (count == 1 ? " event" : " events") + " will also be deleted.";
    }

    Str255 p0, p1;
    if (name.size() > (size_t)kMaxAlertTitle)
    {
        memcpy(p0 + 1, name.c_str(), kMaxAlertTitle - 1);
        p0[kMaxAlertTitle] = 0xC9;   // ellipsis
        p0[0] = kMaxAlertTitle;
    }
    else
    {
        memcpy(p0 + 1, name.c_str(), name.size());
        p0[0] = (unsigned char)name.size();
    }
    p1[0] = (unsigned char)tail.size();     // < 60 characters
    memcpy(&p1[1], tail.data(), tail.size());

    if (!gConfirmFilterUPP)
        gConfirmFilterUPP = NewModalFilterUPP(ConfirmFilter);
    ParamText(p0, p1, "\p", "\p");
    InitCursor();
    short item = CautionAlert(kConfirmDeleteCalendarAlertID, gConfirmFilterUPP);
    SetPort(gMainWindow);
    if (item != 1)
        return false;

    if (!gCalendar->RemoveCategory(name))
        return false;

    gSidebarHasFocus = false;
    gLastClickValid = false;
    gLastRowClickIndex = -1;
    unsigned long selected = gUIRenderer->GetSelectedEvent();
    if (selected != 0 && !gCalendar->FindEvent(selected))
        SelectEvent(0, Date());
    SelectedCalendarName();     // falls back to the first calendar if it was the one deleted
    UpdateExportMenuItem();
    RebuildControls();
    RefreshAfterEdit();
    InvalRect(&gMainWindow->portRect);
    return true;
}

// Edit > Delete / the Delete key: the selected event, else (when the sidebar
// was clicked last) the selected calendar.
static void DeleteSelection()
{
    if (gUIRenderer->GetSelectedEvent() != 0)
        DeleteSelectedEvent();
    else if (gSidebarHasFocus)
        ConfirmAndDeleteCalendar(SelectedCalendarName());
}

static Rect SidebarRowsRect()
{
    Rect r;
    r.top = kSidebarListTop;
    r.bottom = SidebarRowTop((int)gCalendar->GetCategories().size());
    r.left = 0;
    r.right = gSidebarWidth;
    return r;
}

static void SetupSidebar()
{
    SelectedCalendarName();     // settle the selection against the current list
    int index = 0;

    const std::map<std::string, ColorRGB>& categories = gCalendar->GetCategories();
    for (std::map<std::string, ColorRGB>::const_iterator it = categories.begin();
         it != categories.end(); ++it)
    {
        Rect r;
        r.top = SidebarRowTop(index) + 1;
        r.bottom = r.top + kSidebarCheckSize;
        r.left = kSidebarCheckLeft;
        r.right = r.left + kSidebarCheckSize;

        NewControl(gMainWindow, &r, "\p", true,
                   gCalendar->IsCategoryVisible(it->first) ? 1 : 0, 0, 1, checkBoxProc,
                   kCheckboxRefConBase + index);

        index++;
    }
}

static ControlHandle FindCategoryCheckbox(int index)
{
    ControlHandle c = ((WindowPeek)gMainWindow)->controlList;
    while (c)
    {
        if (GetControlReference(c) == kCheckboxRefConBase + index)
            return c;
        c = (*c)->nextControl;
    }
    return NULL;
}

// Applies a visibility change from the checkbox or a row click: updates
// the model and the control, drops a selection that just became hidden,
// and repaints the content.
static void SetCategoryVisibleByIndex(int index, bool visible)
{
    std::string name;
    ColorRGB color;
    if (!CategoryAtIndex(index, name, color))
        return;

    SetPort(gMainWindow);
    gLastClickValid = false;

    gCalendar->SetCategoryVisible(name, visible);

    ControlHandle c = FindCategoryCheckbox(index);
    if (c)
        SetControlValue(c, visible ? 1 : 0);

    unsigned long selected = gUIRenderer->GetSelectedEvent();
    if (!visible && selected != 0)
    {
        const Event* e = gCalendar->FindEvent(selected);
        if (e && e->GetCategory() == name)
            SelectEvent(0, Date());
    }

    RefreshAfterEdit();
}

// Click on a row's swatch or name selects the calendar (on mouse-down, as
// Apple does); only the checkbox toggles visibility. A double-click opens
// Calendar Info.
static bool HandleSidebarRowClick(EventRecord *event, Point clickPoint)
{
    if (clickPoint.h < kSidebarCheckLeft || clickPoint.h >= gSidebarWidth - kSplitterWidth)
        return false;

    int count = (int)gCalendar->GetCategories().size();
    for (int i = 0; i < count; i++)
    {
        short top = SidebarRowTop(i);
        if (clickPoint.v >= top && clickPoint.v < top + kSidebarRowHeight)
        {
            std::string name;
            ColorRGB color;
            if (!CategoryAtIndex(i, name, color))
                return false;

            gSidebarHasFocus = true;
            gLastClickValid = false;

            if (name != SelectedCalendarName())
            {
                SetSelectedCalendar(name);
                UpdateExportMenuItem();
                SetPort(gMainWindow);
                Rect rows = SidebarRowsRect();
                InvalRect(&rows);
            }

            if (gLastRowClickIndex == i && (long)(event->when - gLastRowClickTicks) <= GetDblTime())
            {
                gLastRowClickIndex = -1;
                EditCalendarInfo(name);
            }
            else
            {
                gLastRowClickIndex = i;
                gLastRowClickTicks = event->when;
            }
            return true;
        }
    }
    return false;
}

static void DrawSidebar()
{
    Rect sidebar = gMainWindow->portRect;
    sidebar.top += kToolbarHeight;
    sidebar.right = gSidebarWidth;

    RGBColor backgroundColor = {0xDDDD, 0xDDDD, 0xDDDD};
    RGBBackColor(&backgroundColor);
    EraseRect(&sidebar);

    MoveTo(sidebar.right, sidebar.top);
    LineTo(sidebar.right, sidebar.bottom);

    RGBColor black = {0, 0, 0};
    RGBColor headerGrey = {0x6666, 0x6666, 0x6666};

    TextFace(bold);
    TextSize(10);
    RGBForeColor(&headerGrey);
    MoveTo(kSidebarCheckLeft, sidebar.top + 16);
    DrawString("\pOn My Mac");
    RGBForeColor(&black);
    TextFace(normal);
    TextSize(11);

    int index = 0;
    std::string selected = SelectedCalendarName();
    const std::map<std::string, ColorRGB>& categories = gCalendar->GetCategories();
    for (std::map<std::string, ColorRGB>::const_iterator it = categories.begin();
         it != categories.end(); ++it, ++index)
    {
        short rowTop = SidebarRowTop(index);

        if (it->first == selected)
        {
            // Highlight band behind swatch and name. Multiversal's LowMem
            // accessors for the system highlight colour aren't relied on, so
            // use Platinum's light lavender.
            Rect band;
            band.top = rowTop;
            band.bottom = rowTop + kSidebarRowHeight;
            band.left = kSidebarCheckLeft + kSidebarCheckSize;
            band.right = gSidebarWidth - 6;
            RGBColor hilite = {0xCCCC, 0xCCCC, 0xFFFF};
            RGBForeColor(&hilite);
            PaintRect(&band);
            RGBForeColor(&black);
        }

        Rect sw;
        sw.top = rowTop + (kSidebarRowHeight - kSidebarSwatchSize) / 2;
        sw.bottom = sw.top + kSidebarSwatchSize;
        sw.left = kSidebarSwatchLeft;
        sw.right = sw.left + kSidebarSwatchSize;

        const ColorRGB& c = it->second;
        RGBColor fill = {(unsigned short)(c.r * 257), (unsigned short)(c.g * 257),
                         (unsigned short)(c.b * 257)};
        RGBColor frame = {(unsigned short)(c.r * 257 * 6 / 10), (unsigned short)(c.g * 257 * 6 / 10),
                          (unsigned short)(c.b * 257 * 6 / 10)};
        RGBForeColor(&fill);
        PaintRect(&sw);
        RGBForeColor(&frame);
        FrameRect(&sw);
        RGBForeColor(&black);

        Str255 name;
        size_t len = it->first.size();
        if (len > 255) len = 255;
        name[0] = (unsigned char)len;
        memcpy(&name[1], it->first.c_str(), len);
        short avail = gSidebarWidth - kSidebarNameLeft - 6;
        if (avail > 0 && StringWidth(name) > avail)
            TruncString(avail, name, 0); // truncEnd

        MoveTo(kSidebarNameLeft, rowTop + 13);
        DrawString(name);
    }
}

static short MiniCalendarTop()
{
    short listBottom = SidebarRowTop((int)gCalendar->GetCategories().size());
    return listBottom + 20;
}

static void SetupMiniCalendarButtons()
{
    short top = MiniCalendarTop();

    Rect prevRect;
    prevRect.top = top;
    prevRect.bottom = top + 18;
    prevRect.left = 15;
    prevRect.right = 35;
    NewControl(gMainWindow, &prevRect, "\p<", true, 0, 0, 1, pushButProc, kPrevMonthRefCon);

    Rect nextRect;
    nextRect.top = top;
    nextRect.bottom = top + 18;
    nextRect.left = gSidebarWidth - 30;
    nextRect.right = gSidebarWidth - 10;
    NewControl(gMainWindow, &nextRect, "\p>", true, 0, 0, 1, pushButProc, kNextMonthRefCon);
}

static Rect MiniCalendarRect()
{
    Rect r;
    r.top = MiniCalendarTop() - 2;
    r.bottom = gMainWindow->portRect.bottom;
    r.left = 0;
    r.right = gSidebarWidth;
    if (r.bottom < r.top)
        r.bottom = r.top;
    return r;
}

static void EnsureMiniMonth()
{
    if (gMiniMonth < 1 || gMiniMonth > 12)
    {
        Date d = gCalendar->GetCurrentDate();
        gMiniYear = (short)d.year;
        gMiniMonth = (short)d.month;
    }
}

// The mini-calendar's arrows move only the mini-month, not the main view.
static void ChangeMiniMonth(int delta)
{
    EnsureMiniMonth();
    int month = gMiniMonth + delta;
    int year = gMiniYear;
    while (month > 12) { month -= 12; year++; }
    while (month < 1) { month += 12; year--; }
    gMiniYear = (short)year;
    gMiniMonth = (short)month;

    SetPort(gMainWindow);
    Rect mini = MiniCalendarRect();
    InvalRect(&mini);
}

// Sets the main view's date; the mini-month follows it.
static void GoToDate(const Date& date)
{
    gSidebarHasFocus = false;
    bool changed = !(gCalendar->GetCurrentDate() == date);
    gCalendar->SetCurrentDate(date);
    // The selected event may now be off screen; Return/Delete must not act on it.
    if (changed)
        SelectEvent(0, Date());
    gMiniYear = (short)date.year;
    gMiniMonth = (short)date.month;
    SyncTimeGridScrollBar();
    SetPort(gMainWindow);
    Rect content = ContentBounds();
    InvalRect(&content);
    Rect mini = MiniCalendarRect();
    InvalRect(&mini);
}

// Previous/next by the active view's unit: day, week, month, year.
static void StepPeriod(int direction)
{
    Date d = gCalendar->GetCurrentDate();
    switch (gCalendar->GetCurrentView())
    {
        case DayView:
            d = d.AddDays(direction);
            break;
        case WeekView:
            d = d.AddDays(7 * direction);
            break;
        case MonthView:
        {
            int month = d.month + direction;
            int year = d.year;
            while (month > 12) { month -= 12; year++; }
            while (month < 1) { month += 12; year--; }
            int maxDay = Date::DaysInMonth(year, month);
            d = Date(year, month, d.day > maxDay ? maxDay : d.day);
            break;
        }
        case YearView:
        {
            int year = d.year + direction;
            int maxDay = Date::DaysInMonth(year, d.month);
            d = Date(year, d.month, d.day > maxDay ? maxDay : d.day);
            break;
        }
    }
    GoToDate(d);
}

static void DrawCString(const char *str)
{
    unsigned char pstr[256];
    size_t len = strlen(str);
    if (len > 255) len = 255;
    pstr[0] = (unsigned char)len;
    memcpy(&pstr[1], str, len);
    DrawString(pstr);
}

static void DrawCStringCentered(const char *str, short centerX, short baseline)
{
    unsigned char pstr[256];
    size_t len = strlen(str);
    if (len > 255) len = 255;
    pstr[0] = (unsigned char)len;
    memcpy(&pstr[1], str, len);
    MoveTo(centerX - StringWidth(pstr) / 2, baseline);
    DrawString(pstr);
}

// Shared by drawing and click hit-testing so the two can never disagree
// about where each day number actually sits. The grid is always 6 rows,
// starting on the Sunday on or before the 1st of the shown month.
struct MiniCalGeometry
{
    short colWidth;
    short weekdayRow;
    short gridTop;
    short rowHeight;
    int firstWeekday;
};

static MiniCalGeometry ComputeMiniCalGeometry()
{
    EnsureMiniMonth();

    MiniCalGeometry g;
    g.colWidth = (gSidebarWidth - 20) / 7;
    g.weekdayRow = MiniCalendarTop() + 30;
    g.rowHeight = 14;
    g.gridTop = g.weekdayRow + g.rowHeight;
    g.firstWeekday = Date(gMiniYear, gMiniMonth, 1).DayOfWeek();
    return g;
}

static void DrawMiniCalendar()
{
    static const char *kMiniWeekdayNames[] = { "S", "M", "T", "W", "T", "F", "S" };

    Date current = gCalendar->GetCurrentDate();
    Date today = Calendar::Today();
    short top = MiniCalendarTop();
    MiniCalGeometry g = ComputeMiniCalGeometry();

    RGBColor black = {0, 0, 0};
    RGBColor grey = {0x9999, 0x9999, 0x9999};
    RGBColor white = {0xFFFF, 0xFFFF, 0xFFFF};
    RGBColor red = {0xFFFF, 0x3B3B, 0x3030};
    RGBColor selectGrey = {0xBBBB, 0xBBBB, 0xBBBB};

    TextSize(10);
    TextFace(bold);
    {
        // Fitted between the inner edges of the < and > buttons, centred in the gap.
        short gapLeft = 35 + 2, gapRight = gSidebarWidth - 30 - 2;
        short avail = gapRight - gapLeft;
        char titleBuf[32];
        snprintf(titleBuf, sizeof(titleBuf), "%s %d", kMonthNames[gMiniMonth - 1], gMiniYear);
        unsigned char pstr[256];
        size_t len = strlen(titleBuf);
        pstr[0] = (unsigned char)len;
        memcpy(&pstr[1], titleBuf, len);
        if (StringWidth(pstr) > avail)
        {
            len = strlen(kMonthNames[gMiniMonth - 1]);
            pstr[0] = (unsigned char)len;
            memcpy(&pstr[1], kMonthNames[gMiniMonth - 1], len);
        }
        if (avail > 0 && StringWidth(pstr) > avail)
            TruncString(avail, pstr, 0);
        if (avail > 0)
        {
            MoveTo((gapLeft + gapRight) / 2 - StringWidth(pstr) / 2, top + 12);
            DrawString(pstr);
        }
    }
    TextFace(normal);

    for (int i = 0; i < 7; i++)
        DrawCStringCentered(kMiniWeekdayNames[i], 10 + i * g.colWidth + g.colWidth / 2, g.weekdayRow);

    Date start = Date(gMiniYear, gMiniMonth, 1).AddDays(-g.firstWeekday);
    short halfWidth = g.colWidth / 2 < 7 ? g.colWidth / 2 : 7;
    for (int i = 0; i < 42; i++)
    {
        Date d = start.AddDays(i);
        int row = i / 7;
        int col = i % 7;
        short cx = 10 + col * g.colWidth + g.colWidth / 2;
        short baseline = g.gridTop + row * g.rowHeight;

        bool isToday = (d == today);
        bool isCurrent = (d == current);
        if (isToday || isCurrent)
        {
            Rect oval;
            oval.left = cx - halfWidth;
            oval.right = cx + halfWidth;
            oval.top = baseline - 10;
            oval.bottom = baseline + 2;
            RGBForeColor(isToday ? &red : &selectGrey);
            PaintOval(&oval);
        }

        if (isToday)
            RGBForeColor(&white);
        else
            RGBForeColor(d.month == gMiniMonth ? &black : &grey);

        char numBuf[4];
        snprintf(numBuf, sizeof(numBuf), "%d", d.day);
        DrawCStringCentered(numBuf, cx, baseline);
    }
    RGBForeColor(&black);
}

// Returns true if the click landed on a day, in which case the main view
// has already jumped to it (keeping the current view, as Apple does).
static bool HandleMiniCalendarClick(Point clickPoint)
{
    MiniCalGeometry g = ComputeMiniCalGeometry();

    // gridTop is the *baseline* each row's digits are drawn on (MoveTo sets
    // the baseline, not a box top), so a glyph's visible pixels sit mostly
    // above that y-coordinate. Bias by half a row height so the hit-test
    // buckets are centered on each baseline instead of starting at it --
    // otherwise every click reads back as landing one row too early.
    if (clickPoint.h < 10 || clickPoint.v < g.gridTop - g.rowHeight / 2)
        return false;

    int col = (clickPoint.h - 10) / g.colWidth;
    int row = (clickPoint.v - g.gridTop + g.rowHeight / 2) / g.rowHeight;
    if (col < 0 || col > 6 || row < 0 || row > 5)
        return false;

    Date start = Date(gMiniYear, gMiniMonth, 1).AddDays(-g.firstWeekday);
    GoToDate(start.AddDays(row * 7 + col));
    return true;
}

static void SwitchToView(ViewType view)
{
    ViewType previous = gCalendar->GetCurrentView();
    gSidebarHasFocus = false;
    gCalendar->SetCurrentView(view);
    gCalendar->MarkPrefsDirty();
    // Only a fresh entry into the time grid resets the scroll position.
    if ((view == DayView || view == WeekView) &&
        (previous == MonthView || previous == YearView))
    {
        gUIRenderer->ComputeTimeGridLayout(*gCalendar);
        gUIRenderer->SetTimeGridScroll(gUIRenderer->DefaultTimeGridScroll(*gCalendar));
    }
    SyncTimeGridScrollBar();
    UpdateToolbarHilite();
    SetPort(gMainWindow);
    InvalRect(&gMainWindow->portRect);
}

static void SetupMenuBar()
{
    MenuHandle menu;

    // Apple menu: just the required title-bar presence (no "About" item
    // for now -- keep the first menu-bar pass minimal and testable).
    menu = NewMenu(kAppleMenuID, "\p\024");
    AppendMenu(menu, "\pAbout Kalendae...");
    InsertMenu(menu, 0);

    // File menu: New Event, then the one thing this app must reliably offer.
    menu = NewMenu(kFileMenuID, "\pFile");
    AppendMenu(menu, "\pNew Event/N;New Calendar;(-;Import\311;Export;(-;Quit/Q");
    InsertMenu(menu, 0);

    // Export is a hierarchical item (cmd char 0x1B, mark = submenu ID); the
    // submenu is inserted into the hierarchical part of the menu list.
    SetItemCmd(menu, kFileExportItem, 0x1B);
    SetItemMark(menu, kFileExportItem, kExportMenuID);
    gExportMenu = NewMenu(kExportMenuID, "\pExport");
    AppendMenu(gExportMenu, "\pExport Calendar\311;All Calendars\311");
    InsertMenu(gExportMenu, -1);

    // Edit menu: the popover inspector handles Cut/Copy/Paste itself, so the
    // items are only here for the standard menu bar (always disabled).
    menu = NewMenu(kEditMenuID, "\pEdit");
    AppendMenu(menu, "\p(Undo/Z;(-;(Cut/X;(Copy/C;(Paste/V;(Delete");
    InsertMenu(menu, 0);
    gEditMenu = menu;

    // View menu: switch which calendar view is drawn.
    menu = NewMenu(kViewMenuID, "\pView");
    AppendMenu(menu, "\pDay/1;Week/2;Month/3;Year/4;(-;Go to Today/T;Previous/[;Next/]");
    InsertMenu(menu, 0);

    // Event menu: enabled only while an event is selected (UpdateMenuStates).
    menu = NewMenu(kEventMenuID, "\pEvent");
    AppendMenu(menu, "\pEdit Event/E;Delete Event");
    InsertMenu(menu, 0);
    gEventMenu = menu;

    DrawMenuBar();
}

// Standard File filter for Import: returns true to HIDE a file. Folders and
// volumes always show; files show when they are 'TEXT' or end in ".ics".
static pascal Boolean ImportFileFilter(CInfoPBPtr pb)
{
    if (pb->hFileInfo.ioFlAttrib & 0x10)
        return false;
    if (pb->hFileInfo.ioFlFndrInfo.fdType == 'TEXT')
        return false;
    const unsigned char* name = pb->hFileInfo.ioNamePtr;
    if (name && name[0] >= 4)
    {
        unsigned char n = name[0];
        if (name[n - 3] == '.' &&
            (name[n - 2] | 0x20) == 'i' && (name[n - 1] | 0x20) == 'c' && (name[n] | 0x20) == 's')
            return false;
    }
    return true;
}

// Shows a one-button note alert (ALRT 143; ^0 = text).
static void ShowNoteAlert(const std::string& text)
{
    Str255 p;
    size_t n = text.size() > 255 ? 255 : text.size();
    p[0] = (unsigned char)n;
    memcpy(&p[1], text.data(), n);
    ParamText(p, "\p", "\p", "\p");
    InitCursor();
    NoteAlert(kNoteAlertID, AlertUpdateFilter());
    SetPort(gMainWindow);
}

static std::string ErrorText(const char* prefix, OSErr err, const char* suffix)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", (int)err);
    return std::string(prefix) + buf + suffix;
}

// File > Import: merge the events of a chosen .ics file into the calendar.
static void ImportEvents()
{
    if (!gImportFilterUPP)
        gImportFilterUPP = NewFileFilterUPP(ImportFileFilter);

    StandardFileReply reply;
    InitCursor();
    StandardGetFile(gImportFilterUPP, -1, NULL, &reply);
    SetPort(gMainWindow);
    if (!reply.sfGood)
        return;

    std::string text;
    OSErr err = ReadFileToString(reply.sfFile, text);
    if (err == memFullErr)
    {
        ShowMessageAlert("The file is too large to import into Kalendae's memory partition.");
        return;
    }
    if (err != noErr)
    {
        ShowMessageAlert(ErrorText("The file couldn\325t be read (error ", err, ")."));
        return;
    }

    ICALParser parser;
    std::string calName;
    std::vector<Event> parsed = parser.Parse(text, GetGMTOffsetSeconds(), &calName);
    std::string().swap(text);

    if (parsed.empty())
    {
        std::string fileName((const char*)&reply.sfFile.name[1], reply.sfFile.name[0]);
        ShowNoteAlert("No events were found in \322" + fileName + "\323.");
        return;
    }

    size_t calendarsBefore = gCalendar->GetCategories().size();
    int added = 0, skipped = 0, dupInFile = 0, hidden = 0;
    gCalendar->ImportEvents(parsed, calName, &added, &skipped, &dupInFile, &hidden);

    // New calendars need sidebar checkboxes and swatches; the labels are
    // drawn in the update, so the whole window is redrawn.
    if (gCalendar->GetCategories().size() != calendarsBefore)
        RebuildControls();
    RefreshAfterEdit();
    SetPort(gMainWindow);
    InvalRect(&gMainWindow->portRect);

    char buf[16];
    snprintf(buf, sizeof(buf), "%d", added);
    std::string msg = std::string("Imported ") + buf + (added == 1 ? " event." : " events.");
    if (hidden > 0)
    {
        snprintf(buf, sizeof(buf), "%d", hidden);
        msg += std::string(" ") + buf + (hidden == 1 ? " of them is" : " of them are") +
               " in hidden calendars.";
    }
    if (skipped > 0)
    {
        snprintf(buf, sizeof(buf), "%d", skipped);
        msg += std::string(" ") + buf + (skipped == 1 ? " event was" : " events were") +
               " already in Kalendae and " + (skipped == 1 ? "was" : "were") + " skipped.";
    }
    if (dupInFile > 0)
    {
        snprintf(buf, sizeof(buf), "%d", dupInFile);
        msg += std::string(" ") + buf + (dupInFile == 1 ? " duplicate event" : " duplicate events") +
               " in the file " + (dupInFile == 1 ? "was" : "were") + " skipped.";
    }
    if (gEventsNoSave && added > 0)
        msg += " They won\325t be saved in this session. Use File > Export\311 to keep a copy.";
    ShowNoteAlert(msg);
}

// File > Export: all == false writes only the selected calendar, else every
// event (hidden calendars included).
static void ExportEvents(bool all)
{
    std::string calendar = SelectedCalendarName();
    Str255 prompt, defaultName;

    if (all)
    {
        memcpy(prompt, "\pExport all calendars as:", 26);
        memcpy(defaultName, "\pKalendae.ics", 14);
    }
    else
    {
        if (calendar.empty())
            return;
        std::string shown = calendar;
        if (shown.size() > 40)
            shown.resize(40);
        std::string p = "Export \322" + shown + "\323 as:";
        prompt[0] = (unsigned char)p.size();
        memcpy(&prompt[1], p.data(), p.size());

        // <name>.ics in at most 31 characters; ':' isn't allowed in file names
        std::string f = calendar;
        if (f.size() > 27)
            f.resize(27);
        for (size_t i = 0; i < f.size(); i++)
            if (f[i] == ':')
                f[i] = '-';
        f += ".ics";
        defaultName[0] = (unsigned char)f.size();
        memcpy(&defaultName[1], f.data(), f.size());
    }

    StandardFileReply reply;
    InitCursor();
    StandardPutFile(prompt, defaultName, &reply);
    SetPort(gMainWindow);
    if (!reply.sfGood)
        return;

    unsigned long secs = 0;
    GetDateTime(&secs);
    long offset = GetGMTOffsetSeconds();
    unsigned long utcNow = (unsigned long)((long)secs - offset);
    std::string text = all ? gCalendar->SerializeEvents(offset, utcNow)
                           : gCalendar->SerializeEventsInCategory(calendar, offset, utcNow);

    OSErr err = WriteStringToFile(reply.sfFile, text, 'TEXT', 'KALE');
    if (err != noErr)
        ShowMessageAlert(ErrorText("The calendar couldn\325t be exported (error ", err, ")."));
}

// "Export “<Calendar>”…" follows the sidebar selection.
static void UpdateExportMenuItem()
{
    if (!gExportMenu)
        return;
    std::string name = SelectedCalendarName();
    if (name.size() > 40)
        name.resize(40);
    std::string t = name.empty() ? std::string("Export Calendar\311")
                                 : "Export \322" + name + "\323\311";
    Str255 p;
    p[0] = (unsigned char)t.size();
    memcpy(&p[1], t.data(), t.size());
    SetMenuItemText(gExportMenu, kExportSelectedItem, p);
    CalcMenuSize(gExportMenu);
}

static void UpdateMenuStates()
{
    if (!gEventMenu || !gEditMenu)
        return;
    if (gExportMenu)
    {
        UpdateExportMenuItem();
        std::string selected = SelectedCalendarName();
        if (selected.empty() || gCalendar->CountEventsInCategory(selected) == 0)
            DisableItem(gExportMenu, kExportSelectedItem);
        else
            EnableItem(gExportMenu, kExportSelectedItem);
        if (gCalendar->GetAllEvents().empty())
            DisableItem(gExportMenu, kExportAllItem);
        else
            EnableItem(gExportMenu, kExportAllItem);
    }
    // Edit > Delete also deletes the selected calendar when the sidebar was
    // clicked last (never the only one).
    bool calendarDeletable = gSidebarHasFocus && gCalendar->GetCategories().size() > 1;
    if (gUIRenderer->GetSelectedEvent() != 0)
    {
        EnableItem(gEventMenu, kEventEditItem);
        EnableItem(gEventMenu, kEventDeleteItem);
        EnableItem(gEditMenu, kEditDeleteItem);
    }
    else
    {
        DisableItem(gEventMenu, kEventEditItem);
        DisableItem(gEventMenu, kEventDeleteItem);
        if (calendarDeletable)
            EnableItem(gEditMenu, kEditDeleteItem);
        else
            DisableItem(gEditMenu, kEditDeleteItem);
    }
}

static void HandleEvent(EventRecord *event)
{
    switch (event->what)
    {
        case mouseDown:
            HandleWindowEvent(event);
            break;

        case keyDown:
        case autoKey:
            HandleCommandEvent(event);
            break;

        case kHighLevelEvent:
            // Handle AppleEvents
            break;

        case updateEvt:
            HandleUpdateEvent(event);
            break;

        default:
            // Handle other events
            break;
    }
}

static void HandleMenuChoice(long menuChoice)
{
    short menuID = (short)(menuChoice >> 16);
    short menuItem = (short)(menuChoice & 0xFFFF);

    // Un-invert the title first: commands may put up an alert or popover.
    HiliteMenu(0);

    switch (menuID)
    {
        case kFileMenuID:
            if (menuItem == kFileNewEventItem)
            {
                NewEventAtNextHour();
            }
            else if (menuItem == kFileNewCalendarItem)
            {
                NewCalendar();
            }
            else if (menuItem == kFileImportItem)
            {
                ImportEvents();
            }
            else if (menuItem == kFileQuitItem)
            {
                QuitApp();
            }
            break;

        case kExportMenuID:
            if (menuItem == kExportSelectedItem)
                ExportEvents(false);
            else if (menuItem == kExportAllItem)
                ExportEvents(true);
            break;

        case kEventMenuID:
            if (menuItem == kEventEditItem)
            {
                EditSelectedEvent();
            }
            else if (menuItem == kEventDeleteItem)
            {
                DeleteSelectedEvent();
            }
            break;

        case kEditMenuID:
            if (menuItem == kEditDeleteItem)
            {
                DeleteSelection();
            }
            break;

        case kViewMenuID:
        {
            switch (menuItem)
            {
                case kViewDayItem:      SwitchToView(DayView); break;
                case kViewWeekItem:     SwitchToView(WeekView); break;
                case kViewMonthItem:    SwitchToView(MonthView); break;
                case kViewYearItem:     SwitchToView(YearView); break;
                case kViewTodayItem:    GoToDate(Calendar::Today()); break;
                case kViewPreviousItem: StepPeriod(-1); break;
                case kViewNextItem:     StepPeriod(1); break;
            }
            break;
        }

        default:
            break;
    }
}

static void HandleUpdateEvent(EventRecord *event)
{
    WindowPtr window = (WindowPtr)event->message;

    // BeginUpdate/EndUpdate operate on the *current* port's update region --
    // without SetPort first, they act on whatever port was last current
    // (possibly none of our windows), so this window's update region never
    // gets validated and WaitNextEvent keeps redelivering the same updateEvt
    // forever with no idle time, starving the whole cooperative system.
    SetPort(window);
    BeginUpdate(window);
    if (window == gMainWindow)
    {
        RGBColor toolbarColor = {0xDDDD, 0xDDDD, 0xDDDD};
        RGBBackColor(&toolbarColor);
        EraseRect(&window->portRect);

        DrawSidebar();
        DrawMiniCalendar();

        Rect bounds = ContentBounds();
        switch (gCalendar->GetCurrentView())
        {
            case DayView:   gUIRenderer->RenderDayView(*gCalendar, bounds); break;
            case WeekView:  gUIRenderer->RenderWeekView(*gCalendar, bounds); break;
            case MonthView: gUIRenderer->RenderMonthView(*gCalendar, bounds); break;
            case YearView:  gUIRenderer->RenderYearView(*gCalendar, bounds); break;
        }
        DrawControls(window);
        DrawGrowIcon(window);
    }
    EndUpdate(window);
}

static void HandleWindowEvent(EventRecord *event)
{
    WindowPtr window;

    switch (FindWindow(event->where, &window))
    {
        case inContent:
        {
            Point clickPoint = event->where;
            GlobalToLocal(&clickPoint);

            if (clickPoint.v >= kToolbarHeight &&
                clickPoint.h >= gSidebarWidth - kSplitterWidth / 2 &&
                clickPoint.h <= gSidebarWidth + kSplitterWidth / 2)
            {
                TrackSidebarResize();
                break;
            }

            ControlHandle control;
            short controlPart = FindControl(clickPoint, window, &control);
            if (controlPart != 0)
            {
                if (control == gScrollBar)
                {
                    HandleTimeGridScrollClick(control, controlPart, clickPoint);
                }
                else if (TrackControl(control, clickPoint, NULL))
                {
                    long ref = GetControlReference(control);
                    if (ref == kPrevMonthRefCon)
                    {
                        ChangeMiniMonth(-1);
                    }
                    else if (ref == kNextMonthRefCon)
                    {
                        ChangeMiniMonth(1);
                    }
                    else if (ref == kPrevPeriodRefCon)
                    {
                        StepPeriod(-1);
                    }
                    else if (ref == kNextPeriodRefCon)
                    {
                        StepPeriod(1);
                    }
                    else if (ref == kTodayRefCon)
                    {
                        GoToDate(Calendar::Today());
                    }
                    else if (ref == kNewEventRefCon)
                    {
                        NewEventAtNextHour();
                    }
                    else if (ref >= kCheckboxRefConBase && ref < kPrevMonthRefCon)
                    {
                        int idx = (int)(ref - kCheckboxRefConBase);
                        std::string cname;
                        ColorRGB ccolor;
                        if (CategoryAtIndex(idx, cname, ccolor))
                            SetCategoryVisibleByIndex(idx, !gCalendar->IsCategoryVisible(cname));
                    }
                    else if (ref >= 0 && ref <= 3)
                    {
                        SwitchToView((ViewType)ref);
                    }
                }
                // TrackControl clears the pressed look of the active view button
                UpdateToolbarHilite();
            }
            else if (HandleSidebarRowClick(event, clickPoint))
            {
                // toggled a calendar via its swatch or name
            }
            else if (!HandleMiniCalendarClick(clickPoint))
            {
                HandleContentClick(event, clickPoint);
            }

            break;
        }

        case inMenuBar:
        {
            UpdateMenuStates();
            long menuChoice = MenuSelect(event->where);
            HandleMenuChoice(menuChoice);
            break;
        }

        case inDrag:
        {
            Rect dragBounds = qd.screenBits.bounds;
            dragBounds.top += 20; // stay clear of the menu bar
            DragWindow(window, event->where, &dragBounds);
            MarkWindowPrefsDirty();
            break;
        }

        case inGoAway:
            if (TrackGoAway(window, event->where))
            {
                QuitApp();
            }
            break;

        case inGrow:
            TrackWindowResize(window, event->where);
            break;
    }
}

static void HandleCommandEvent(EventRecord *event)
{
    char ch = (char)(event->message & charCodeMask);
    bool cmd = (event->modifiers & cmdKey) != 0;

    // Navigation keys may auto-repeat
    if (cmd && (ch == 0x1C || ch == 0x1D))
    {
        StepPeriod(ch == 0x1C ? -1 : 1);
        return;
    }
    if (!cmd && (ch == 0x0B || ch == 0x0C))
    {
        ViewType view = gCalendar->GetCurrentView();
        if (view == DayView || view == WeekView)
        {
            if (gScrollBar && (**gScrollBar).contrlVis)
            {
                short page = TimeGridPageStep();
                ScrollTimeGridBy(gScrollBar, ch == 0x0B ? (short)-page : page);
            }
        }
        else
        {
            StepPeriod(ch == 0x0B ? -1 : 1);
        }
        return;
    }

    // Opening an inspector must not repeat while the key is held.
    if (event->what == autoKey)
        return;

    // Option-Cmd-N (File > New Calendar). Option changes the character, so
    // match the virtual key code (N = 0x2D).
    if (cmd && (event->modifiers & optionKey) &&
        ((event->message & keyCodeMask) >> 8) == 0x2D)
    {
        NewCalendar();
        return;
    }

    if (!cmd && ch == 0x01)
    {
        GoToDate(Calendar::Today());
    }
    else if (cmd)
    {
        UpdateMenuStates();
        long menuChoice = MenuKey(ch);
        if (menuChoice != 0)
        {
            HandleMenuChoice(menuChoice);
        }
    }
    else if (ch == 0x0D || ch == 0x03)
    {
        // Return / Enter edits the selected event
        EditSelectedEvent();
    }
    else if (ch == 0x08 || ch == 0x7F)
    {
        // Delete / Forward Delete removes the selected event or calendar (after asking)
        DeleteSelection();
    }
}
