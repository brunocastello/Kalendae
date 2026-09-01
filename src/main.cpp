/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * Main application entry point and event loop.
 *
 * Deliberately minimal and free of C++ classes/STL containers for now:
 * four straight rewrites of the update/render path all froze the whole
 * emulated system identically, which points at something in this
 * project's C++/STL usage on the classic PowerPC target rather than at
 * any one drawing call -- Retro68's PowerPC crt0 (libretro/ppcstart.c)
 * confirmed does not run global C++ constructors the way its 68k crt0
 * does, and that combination (classic PowerPC + modern GCC libstdc++)
 * gets very little real-world exercise. This file matches the plain
 * Toolbox-only window+event-loop shape as closely as possible to a
 * known-working Retro68 sample, to get a stable, non-crashing baseline
 * before layering the Calendar/UIRenderer classes back in.
 */

#include <Quickdraw.h>
#include <Windows.h>
#include <Menus.h>
#include <Fonts.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Events.h>
#include <Processes.h>

static WindowPtr gMainWindow = NULL;

static const unsigned char kWindowTitle[] = {
    28, 'K', 'a', 'l', 'e', 'n', 'd', 'a', 'e', ' ', '-', ' ',
    'M', 'a', 'c', ' ', 'O', 'S', ' ', '9', ' ', 'C', 'a', 'l', 'e', 'n', 'd', 'a', 'r'
};

static const unsigned char kBodyText[] = {
    16, 'K', 'a', 'l', 'e', 'n', 'd', 'a', 'e', ' ', 'r', 'u', 'n', 'n', 'i', 'n', 'g'
};

static void DrawContent(void)
{
    RGBColor backgroundColor = {0xCCCC, 0xCCCC, 0xCCCC};
    RGBBackColor(&backgroundColor);
    EraseRect(&gMainWindow->portRect);

    MoveTo(20, 30);
    DrawString(kBodyText);
}

static void HandleUpdateEvent(EventRecord *event)
{
    WindowPtr window = (WindowPtr)event->message;

    SetPort(window);
    BeginUpdate(window);
    if (window == gMainWindow)
    {
        DrawContent();
    }
    EndUpdate(window);
}

static void HandleWindowEvent(EventRecord *event)
{
    WindowPtr window;

    if (FindWindow(event->where, &window) == inGoAway)
    {
        if (TrackGoAway(window, event->where))
        {
            ExitToShell();
        }
    }
}

static void HandleCommandEvent(EventRecord *event)
{
    if (event->modifiers & cmdKey)
    {
        char ch = (char)(event->message & charCodeMask);
        if (ch == 'q' || ch == 'Q')
        {
            ExitToShell();
        }
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

        case updateEvt:
            HandleUpdateEvent(event);
            break;

        default:
            break;
    }
}

static void SetupApplication(void)
{
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    FlushEvents(everyEvent, 0);

    Rect windowRect = { 50, 50, 500, 600 };
    gMainWindow = NewCWindow(NULL, &windowRect, kWindowTitle,
                              true, documentProc, (WindowPtr)-1L, true, 0);

    SetPort(gMainWindow);
    ShowWindow(gMainWindow);
}

int main(void)
{
    SetupApplication();

    EventRecord event;
    for (;;)
    {
        if (WaitNextEvent(everyEvent, &event, 10, NULL))
        {
            HandleEvent(&event);
        }
    }

    return 0;
}
