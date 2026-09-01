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
#include <Dialogs.h>
#include <Events.h>
#include <Processes.h>
#include <cstdio>
#include <cstring>
#include "calendar.h"
#include "ical_parser.h"
#include "ui_renderer.h"

enum
{
    kAppleMenuID = 1,
    kFileMenuID = 2,
    kViewMenuID = 3
};

enum
{
    kFileQuitItem = 1
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
static void TrackSidebarResize();
static void TrackWindowResize(WindowPtr window, Point startPoint);
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
    // UIRenderer uses RGBBackColor for the Platinum background. 800x600,
    // centered on the screen (menu bar excluded from the centering math
    // so the window doesn't creep up under it).
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
}

static void SetupSidebar()
{
    const short checkboxHeight = 18;
    short top = kToolbarHeight + 28;
    int index = 0;

    const std::map<std::string, ColorRGB>& categories = gCalendar->GetCategories();
    for (std::map<std::string, ColorRGB>::const_iterator it = categories.begin();
         it != categories.end(); ++it)
    {
        Rect r;
        r.top = top;
        r.bottom = top + checkboxHeight;
        r.left = 20;
        r.right = gSidebarWidth - 10;

        Str255 title;
        size_t len = it->first.size();
        if (len > 255) len = 255;
        title[0] = (unsigned char)len;
        memcpy(&title[1], it->first.c_str(), len);

        NewControl(gMainWindow, &r, title, true, 1, 0, 1, checkBoxProc,
                   kCheckboxRefConBase + index);

        top += checkboxHeight + 4;
        index++;
    }
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

    TextFace(bold);
    TextSize(11);
    MoveTo(20, sidebar.top + 16);
    DrawString("\pCalendars");
    TextFace(normal);
}

static short MiniCalendarTop()
{
    const short checkboxHeight = 18;
    short listBottom = kToolbarHeight + 28 +
        (short)(gCalendar->GetCategories().size() * (checkboxHeight + 4));
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
    gCalendar->SetCurrentView(view);
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

    // File menu: Quit is the one thing this app must reliably offer.
    menu = NewMenu(kFileMenuID, "\pFile");
    AppendMenu(menu, "\pQuit/Q");
    InsertMenu(menu, 0);

    // View menu: switch which calendar view is drawn.
    menu = NewMenu(kViewMenuID, "\pView");
    AppendMenu(menu, "\pDay/1;Week/2;Month/3;Year/4");
    InsertMenu(menu, 0);

    DrawMenuBar();
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

    switch (menuID)
    {
        case kFileMenuID:
            if (menuItem == kFileQuitItem)
            {
                ExitToShell();
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

    HiliteMenu(0);
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
            if (FindControl(clickPoint, window, &control) != 0)
            {
                if (TrackControl(control, clickPoint, NULL))
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
                        SetControlValue(control, !GetControlValue(control));
                    }
                    else
                    {
                        SwitchToView((ViewType)ref);
                    }
                }
            }
            else
            {
                HandleMiniCalendarClick(clickPoint);
            }

            // Handle event interaction here
            break;
        }

        case inMenuBar:
        {
            long menuChoice = MenuSelect(event->where);
            HandleMenuChoice(menuChoice);
            break;
        }

        case inDrag:
        {
            Rect dragBounds = qd.screenBits.bounds;
            dragBounds.top += 20; // stay clear of the menu bar
            DragWindow(window, event->where, &dragBounds);
            break;
        }

        case inGoAway:
            if (TrackGoAway(window, event->where))
            {
                ExitToShell();
            }
            break;

        case inGrow:
            TrackWindowResize(window, event->where);
            break;
    }
}

static void HandleCommandEvent(EventRecord *event)
{
    if (event->modifiers & cmdKey)
    {
        char ch = (char)(event->message & charCodeMask);
        long menuChoice = MenuKey(ch);
        if (menuChoice != 0)
        {
            HandleMenuChoice(menuChoice);
        }
    }
}
