/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * Main application entry point and event loop
 */

#include <Carbon/Carbon.h>
#include "calendar.h"
#include "ical_parser.h"
#include "ui_renderer.h"

// Global variables
static WindowRef gMainWindow = NULL;
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
    // Set up application menus, windows, etc.

    // Create main window
    Rect windowRect = { 50, 50, 500, 600 };
    CreateNewWindow(kDocumentWindowClass, kWindowStandardHandlerAttribute,
                    &windowRect, &gMainWindow);

    // Set window title
    SetWTitle(gMainWindow, "\pKalendae - Mac OS 9 Calendar");

    // Show window
    ShowWindow(gMainWindow);

    // Register for Carbon events
    RegisterEventClass(kEventClassWindow);
    RegisterEventClass(kEventClassCommand);
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
    WindowRef window;
    WindowRef windowUnderMouse;

    switch (FindWindow(event->where, &windowUnderMouse))
    {
        case inContent:
            // Content area clicked
            window = windowUnderMouse;
            // Handle content interactions

            // Check if the click was on a calendar event
            Point clickPoint = event->where;
            // Convert to local coordinates
            GlobalToLocal(&clickPoint);

            // Handle event interaction here
            break;

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