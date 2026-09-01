/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * Main application entry point and event loop
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

// Global variables
static WindowPtr gMainWindow = NULL;
static Calendar gCalendar;
static UIRenderer gUIRenderer;

// Forward declarations
static void SetupApplication();
static void HandleEvent(EventRecord *event);
static void HandleWindowEvent(EventRecord *event);
static void HandleCommandEvent(EventRecord *event);
static void HandleUpdateEvent(EventRecord *event);

int main()
{
    // Initialize the application
    SetupApplication();

    // Load calendar data from preferences
    gCalendar.LoadFromPreferences();

    // Main event loop
    EventRecord event;
    while (true)
    {
        if (WaitNextEvent(everyEvent, &event, 10, NULL))
        {
            HandleEvent(&event);
        }

        // Handle any background tasks
        gCalendar.ProcessBackgroundTasks();
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

    // Create main window. Must be a *color* window (NewCWindow, not
    // NewWindow) since UIRenderer's CopyBits reads the window port as a
    // CGrafPort (portPixMap) -- a plain NewWindow GrafPort has no such
    // field at that offset, so treating it as one is a wild memory read.
    Rect windowRect = { 50, 50, 500, 600 };
    gMainWindow = NewCWindow(NULL, &windowRect, "\pKalendae - Mac OS 9 Calendar",
                              true, documentProc, (WindowPtr)-1L, true, 0);

    // Show window
    SetPort(gMainWindow);
    ShowWindow(gMainWindow);

    // Set up offscreen rendering for the window
    gUIRenderer.Initialize(gMainWindow);
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
        gUIRenderer.RenderMonthView(gCalendar, window->portRect);
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
            // Handle menu bar clicks
            break;
    }
}

static void HandleCommandEvent(EventRecord *event)
{
    // Handle keyboard commands
    switch (event->message & charCodeMask)
    {
        case 'q':
        case 'Q':
            // Quit application
            ExitToShell();
            break;
    }
}
