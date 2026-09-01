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
#include "calendar.h"
#include "ical_parser.h"
#include "ui_renderer.h"

enum
{
    kAppleMenuID = 1,
    kFileMenuID = 2
};

enum
{
    kFileQuitItem = 1
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

// Forward declarations
static void SetupApplication();
static void SetupMenuBar();
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
    gMainWindow = NewCWindow(NULL, &windowRect, "\pKalendae - Mac OS 9 Calendar",
                              true, documentProc, (WindowPtr)-1L, true, 0);

    SetPort(gMainWindow);
    ShowWindow(gMainWindow);

    gUIRenderer->Initialize(gMainWindow);
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
        gUIRenderer->RenderMonthView(*gCalendar, window->portRect);
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
            // Content area clicked

            // Check if the click was on a calendar event
            Point clickPoint = event->where;
            // Convert to local coordinates
            GlobalToLocal(&clickPoint);

            // Handle event interaction here
            break;
        }

        case inMenuBar:
        {
            long menuChoice = MenuSelect(event->where);
            HandleMenuChoice(menuChoice);
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
