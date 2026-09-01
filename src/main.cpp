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
static const short kSidebarWidth = 140;

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
    // UIRenderer uses RGBBackColor for the Platinum background.
    Rect windowRect = { 50, 50, 500, 600 };
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

    for (int i = 0; i < 4; i++)
    {
        Rect r;
        r.top = top;
        r.bottom = top + buttonHeight;
        r.left = 10 + i * (buttonWidth + gap);
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
    bounds.left += kSidebarWidth;
    return bounds;
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
        r.right = kSidebarWidth - 10;

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
    sidebar.right = kSidebarWidth;

    RGBColor backgroundColor = {0xDDDD, 0xDDDD, 0xDDDD};
    RGBBackColor(&backgroundColor);
    EraseRect(&sidebar);

    MoveTo(sidebar.left, sidebar.top + 20);
    LineTo(sidebar.right, sidebar.top + 20);

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
    nextRect.left = kSidebarWidth - 30;
    nextRect.right = kSidebarWidth - 10;
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
    g.colWidth = (kSidebarWidth - 20) / 7;
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

    if (clickPoint.h < 10 || clickPoint.v < g.gridTop)
        return false;

    int col = (clickPoint.h - 10) / g.colWidth;
    int row = (clickPoint.v - g.gridTop) / g.rowHeight;
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
