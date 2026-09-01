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

// Forward declarations
static void SetupApplication();
static void HandleEvent(EventRecord *event);
static void HandleWindowEvent(EventRecord *event);
static void HandleCommandEvent(EventRecord *event);

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

    // Create main window
    Rect windowRect = { 50, 50, 500, 600 };
    gMainWindow = NewWindow(NULL, &windowRect, "\pKalendae - Mac OS 9 Calendar",
                             true, documentProc, (WindowPtr)-1L, true, 0);

    // Show window
    SetPort(gMainWindow);
    ShowWindow(gMainWindow);
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

        default:
            // Handle other events
            break;
    }
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
