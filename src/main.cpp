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
#include <cstdio>
#include <cstring>
#include "calendar.h"
#include "ical_parser.h"
#include "ui_renderer.h"
#include "event_dialog.h"

enum
{
    kAppleMenuID = 1,
    kFileMenuID = 2,
    kViewMenuID = 3,
    kEditMenuID = 4,
    kEventMenuID = 5
};

enum
{
    kFileNewEventItem = 1,
    kFileQuitItem = 3
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
    kMaxAlertTitle = 40
};

enum
{
    kViewDayItem = 1,
    kViewWeekItem = 2,
    kViewMonthItem = 3,
    kViewYearItem = 4
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
static const long kTimeGridScrollRefCon = 3000;

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
static void ChangeMonth(int delta);
static void SwitchToView(ViewType view);
static Rect ContentBounds();
static void RebuildControls();
static void SetupTimeGridScrollBar();
static void SyncTimeGridScrollBar();
static void HandleTimeGridScrollClick(ControlHandle control, short part, Point clickPoint);
static void HandleContentClick(EventRecord *event, Point clickPoint);
static void TrackSidebarResize();
static void TrackWindowResize(WindowPtr window, Point startPoint);
static bool GetPrefsFileSpec(FSSpec *spec);
static void SaveWindowPrefs();
static bool LoadWindowPrefs(Rect *outRect, short *outSidebarWidth);
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
static void EditSelectedEvent();
static void HandleEvent(EventRecord *event);
static void HandleMenuChoice(long menuChoice);
static void HandleWindowEvent(EventRecord *event);
static void HandleCommandEvent(EventRecord *event);
static void HandleUpdateEvent(EventRecord *event);

int main()
{
    // Construct the app's objects explicitly, now that we're definitely
    // running (see the comment on the globals above for why).
    gCalendar = new Calendar();
    gUIRenderer = new UIRenderer();

    // Initialize the application
    SetupApplication();

    // Load calendar data from preferences
    gCalendar->LoadFromPreferences();

    // The sidebar checkboxes mirror the category list, so they can only
    // be built once LoadFromPreferences has populated it. The mini
    // calendar's nav buttons are positioned below that list, so they have
    // the same ordering requirement.
    SetupSidebar();
    SetupMiniCalendarButtons();
    SetupTimeGridScrollBar();

    // Main event loop
    EventRecord event;
    while (true)
    {
        if (WaitNextEvent(everyEvent, &event, 10, NULL))
        {
            HandleEvent(&event);
        }

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

    LoadWindowPrefs(&windowRect, &gSidebarWidth);

    gMainWindow = NewCWindow(NULL, &windowRect, "\pKalendae",
                              true, documentProc, (WindowPtr)-1L, true, 0);

    SetPort(gMainWindow);
    ShowWindow(gMainWindow);

    gUIRenderer->Initialize(gMainWindow);
    gCalendar->SetCurrentView(MonthView);

    SetupToolbar();
}

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

    for (int i = 0; i < 4; i++)
    {
        Rect r;
        r.top = top;
        r.bottom = top + buttonHeight;
        r.left = startX + i * (buttonWidth + gap);
        r.right = r.left + buttonWidth;

        Str255 title;
        title[0] = (unsigned char)strlen(labels[i]);
        memcpy(&title[1], labels[i], title[0]);

        NewControl(gMainWindow, &r, title, true, 0, 0, 1, pushButProc, (long)views[i]);
    }
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
static pascal void TimeGridScrollAction(ControlHandle control, short part)
{
    Rect viewRect = gUIRenderer->GetTimeGridViewRect(ContentBounds());
    short pageStep = (short)(viewRect.bottom - viewRect.top - UIRenderer::kHourHeight);
    if (pageStep < UIRenderer::kHourHeight)
        pageStep = UIRenderer::kHourHeight;

    short step = 0;
    switch (part)
    {
        case inUpButton:   step = -UIRenderer::kHourHeight; break;
        case inDownButton: step = UIRenderer::kHourHeight; break;
        case inPageUp:     step = -pageStep; break;
        case inPageDown:   step = pageStep; break;
        default: return;
    }

    short oldValue = GetControlValue(control);
    long newValue = (long)oldValue + step;
    if (newValue < GetControlMinimum(control)) newValue = GetControlMinimum(control);
    if (newValue > GetControlMaximum(control)) newValue = GetControlMaximum(control);
    if (newValue == oldValue)
        return;

    SetControlValue(control, (short)newValue);
    ScrollTimeGridTo((short)newValue);
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

    SaveWindowPrefs();
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

    SaveWindowPrefs();
}

// Real classic document windows have a ~19px title bar above portRect's
// local (0,0); saving/restoring the *structure* rect (what NewCWindow
// takes) needs that added back in, since portRect only ever exposes the
// content area.
static const short kTitleBarHeight = 19;

struct WindowPrefsData
{
    short left, top, width, height;
    short sidebarWidth;
};

// Fills in an FSSpec for "Kalendae Preferences" inside the Preferences
// folder. Per Inside Macintosh, FSMakeFSSpec fills the spec in and
// returns fnfErr when the file doesn't exist yet -- that's expected the
// first time this app runs and is not a failure the caller needs to
// check for.
static bool GetPrefsFileSpec(FSSpec *spec)
{
    short vRefNum;
    long dirID;
    if (FindFolder(-1, kPreferencesFolderType, true, &vRefNum, &dirID) != noErr)
        return false;

    FSMakeFSSpec(vRefNum, dirID, "\pKalendae Preferences", spec);
    return true;
}

static void SaveWindowPrefs()
{
    FSSpec spec;
    if (!GetPrefsFileSpec(&spec))
        return;

    FSpCreate(&spec, 'KALE', 'pref', smSystemScript); // dupFNErr if it already exists is fine

    short refNum;
    if (FSpOpenDF(&spec, fsWrPerm, &refNum) != noErr)
        return;

    SetPort(gMainWindow);
    Point contentTopLeft;
    contentTopLeft.h = 0;
    contentTopLeft.v = 0;
    LocalToGlobal(&contentTopLeft);

    WindowPrefsData prefs;
    prefs.left = contentTopLeft.h;
    prefs.top = contentTopLeft.v - kTitleBarHeight;
    prefs.width = gMainWindow->portRect.right - gMainWindow->portRect.left;
    prefs.height = gMainWindow->portRect.bottom - gMainWindow->portRect.top;
    prefs.sidebarWidth = gSidebarWidth;

    long count = sizeof(prefs);
    SetFPos(refNum, fsFromStart, 0);
    FSWrite(refNum, &count, &prefs);
    SetEOF(refNum, count);
    FSClose(refNum);
}

static bool LoadWindowPrefs(Rect *outRect, short *outSidebarWidth)
{
    FSSpec spec;
    if (!GetPrefsFileSpec(&spec))
        return false;

    short refNum;
    if (FSpOpenDF(&spec, fsRdPerm, &refNum) != noErr)
        return false;

    WindowPrefsData prefs;
    long count = sizeof(prefs);
    OSErr err = FSRead(refNum, &count, &prefs);
    FSClose(refNum);

    if (err != noErr || count != sizeof(prefs))
        return false;

    if (prefs.width < kWindowMinWidth || prefs.height < kWindowMinHeight)
        return false;

    outRect->left = prefs.left;
    outRect->top = prefs.top;
    outRect->right = (short)(prefs.left + prefs.width);
    outRect->bottom = (short)(prefs.top + prefs.height);

    if (prefs.sidebarWidth >= kSidebarMinWidth && prefs.sidebarWidth <= kSidebarMaxWidth)
    {
        *outSidebarWidth = prefs.sidebarWidth;
    }

    return true;
}

// Single path to quit through, so every trigger (File > Quit, Cmd-Q, the
// close box) saves window prefs first rather than relying on each call
// site to remember to.
static void QuitApp()
{
    SaveWindowPrefs();
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

static void CreateEventWithInspector(Event& e, const Rect& anchorLocal)
{
    Rect anchor;
    LocalRectToGlobal(anchorLocal, &anchor);
    short result = ShowEventInspector(e, true, anchor, *gCalendar, InspectorUpdate);
    SetPort(gMainWindow);

    if (result == kInspectorSaved)
    {
        unsigned long id = gCalendar->AddEvent(e);
        SelectEvent(id, e.GetStartTime());
        RefreshAfterEdit();
    }
}

static void SetFirstCategory(Event& e)
{
    const std::map<std::string, ColorRGB>& cats = gCalendar->GetCategories();
    if (!cats.empty())
        e.SetCategory(cats.begin()->first);
}

// Double-click on an empty slot. Month slots and the Day/Week all-day strip
// (minute -1) create all-day events; hour slots create one-hour events.
static void NewEventFromHit(const HitRegion& hit)
{
    Event e("New Event", hit.date, hit.date);
    SetFirstCategory(e);
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
    SetFirstCategory(e);
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

static void SetupSidebar()
{
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

// Click on a row's swatch or name toggles the calendar like its checkbox.
static bool HandleSidebarRowClick(Point clickPoint)
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
            SetCategoryVisibleByIndex(i, !gCalendar->IsCategoryVisible(name));
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
    const std::map<std::string, ColorRGB>& categories = gCalendar->GetCategories();
    for (std::map<std::string, ColorRGB>::const_iterator it = categories.begin();
         it != categories.end(); ++it, ++index)
    {
        short rowTop = SidebarRowTop(index);

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
            TruncString(avail, name, truncEnd);

        MoveTo(kSidebarNameLeft, rowTop + 13);
        DrawString(name);
    }
}

static short MiniCalendarTop()
{
    short listBottom = SidebarRowTop((int)gCalendar->GetCategories().size());
    return listBottom + 20 - kSidebarRowGap;
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

static void ChangeMonth(int delta)
{
    Date d = gCalendar->GetCurrentDate();
    int month = d.month + delta;
    int year = d.year;
    while (month > 12) { month -= 12; year++; }
    while (month < 1) { month += 12; year--; }

    int maxDay = Date::DaysInMonth(year, month);
    int day = d.day > maxDay ? maxDay : d.day;

    gCalendar->SetCurrentDate(Date(year, month, day));
    SyncTimeGridScrollBar();
    SetPort(gMainWindow);
    InvalRect(&gMainWindow->portRect);
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

// Shared by drawing and click hit-testing so the two can never disagree
// about where each day number actually sits.
struct MiniCalGeometry
{
    short colWidth;
    short weekdayRow;
    short gridTop;
    short rowHeight;
    int firstWeekday;
    int daysInMonth;
};

static MiniCalGeometry ComputeMiniCalGeometry()
{
    Date date = gCalendar->GetCurrentDate();

    MiniCalGeometry g;
    g.colWidth = (gSidebarWidth - 20) / 7;
    g.weekdayRow = MiniCalendarTop() + 30;
    g.rowHeight = 14;
    g.gridTop = g.weekdayRow + g.rowHeight;
    g.firstWeekday = Date(date.year, date.month, 1).DayOfWeek();
    g.daysInMonth = Date::DaysInMonth(date.year, date.month);
    return g;
}

static void DrawMiniCalendar()
{
    static const char *kMiniWeekdayNames[] = { "S", "M", "T", "W", "T", "F", "S" };

    Date date = gCalendar->GetCurrentDate();
    short top = MiniCalendarTop();
    MiniCalGeometry g = ComputeMiniCalGeometry();

    TextSize(10);
    TextFace(normal);

    char titleBuf[32];
    snprintf(titleBuf, sizeof(titleBuf), "%s %d", kMonthNames[date.month - 1], date.year);
    MoveTo(40, top + 12);
    DrawCString(titleBuf);

    for (int i = 0; i < 7; i++)
    {
        MoveTo(10 + i * g.colWidth, g.weekdayRow);
        DrawCString(kMiniWeekdayNames[i]);
    }

    for (int day = 1; day <= g.daysInMonth; day++)
    {
        int index = g.firstWeekday + day - 1;
        int row = index / 7;
        int col = index % 7;

        char numBuf[4];
        snprintf(numBuf, sizeof(numBuf), "%d", day);
        MoveTo(10 + col * g.colWidth, g.gridTop + row * g.rowHeight);
        DrawCString(numBuf);
    }
}

// Returns true if the click landed on a valid day number, in which case
// it has already navigated there.
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
    if (col < 0 || col > 6)
        return false;

    int day = row * 7 + col - g.firstWeekday + 1;
    if (day < 1 || day > g.daysInMonth)
        return false;

    Date date = gCalendar->GetCurrentDate();
    gCalendar->SetCurrentDate(Date(date.year, date.month, day));
    SwitchToView(DayView);
    return true;
}

static void SwitchToView(ViewType view)
{
    ViewType previous = gCalendar->GetCurrentView();
    gCalendar->SetCurrentView(view);
    // Only a fresh entry into the time grid resets the scroll position.
    if ((view == DayView || view == WeekView) &&
        (previous == MonthView || previous == YearView))
    {
        gUIRenderer->ComputeTimeGridLayout(*gCalendar);
        gUIRenderer->SetTimeGridScroll(gUIRenderer->DefaultTimeGridScroll(*gCalendar));
    }
    SyncTimeGridScrollBar();
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
    AppendMenu(menu, "\pNew Event/N;(-;Quit/Q");
    InsertMenu(menu, 0);

    // Edit menu: the popover inspector handles Cut/Copy/Paste itself, so the
    // items are only here for the standard menu bar (always disabled).
    menu = NewMenu(kEditMenuID, "\pEdit");
    AppendMenu(menu, "\p(Undo/Z;(-;(Cut/X;(Copy/C;(Paste/V;(Delete");
    InsertMenu(menu, 0);
    gEditMenu = menu;

    // View menu: switch which calendar view is drawn.
    menu = NewMenu(kViewMenuID, "\pView");
    AppendMenu(menu, "\pDay/1;Week/2;Month/3;Year/4");
    InsertMenu(menu, 0);

    // Event menu: enabled only while an event is selected (UpdateMenuStates).
    menu = NewMenu(kEventMenuID, "\pEvent");
    AppendMenu(menu, "\pEdit Event/E;Delete Event");
    InsertMenu(menu, 0);
    gEventMenu = menu;

    DrawMenuBar();
}

static void UpdateMenuStates()
{
    if (!gEventMenu || !gEditMenu)
        return;
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
            else if (menuItem == kFileQuitItem)
            {
                QuitApp();
            }
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
                DeleteSelectedEvent();
            }
            break;

        case kViewMenuID:
        {
            ViewType newView = DayView;
            switch (menuItem)
            {
                case kViewDayItem:   newView = DayView; break;
                case kViewWeekItem:  newView = WeekView; break;
                case kViewMonthItem: newView = MonthView; break;
                case kViewYearItem:  newView = YearView; break;
            }
            SwitchToView(newView);
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
                        ChangeMonth(-1);
                    }
                    else if (ref == kNextMonthRefCon)
                    {
                        ChangeMonth(1);
                    }
                    else if (ref >= kCheckboxRefConBase)
                    {
                        SetCategoryVisibleByIndex((int)(ref - kCheckboxRefConBase),
                                                  !GetControlValue(control));
                    }
                    else
                    {
                        SwitchToView((ViewType)ref);
                    }
                }
            }
            else if (HandleSidebarRowClick(clickPoint))
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
            SaveWindowPrefs();
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
    // Opening an inspector must not repeat while the key is held.
    if (event->what == autoKey)
        return;

    char ch = (char)(event->message & charCodeMask);
    if (event->modifiers & cmdKey)
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
        // Delete / Forward Delete removes the selected event (after asking)
        DeleteSelectedEvent();
    }
}
