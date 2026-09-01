/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * UI Rendering engine interface using QuickDraw and GWorld
 */

#ifndef UI_RENDERER_H
#define UI_RENDERER_H

#include <Carbon/Carbon.h>
#include "calendar.h"

class UIRenderer
{
public:
    UIRenderer();
    ~UIRenderer();

    // Initialize the renderer with a window
    bool Initialize(WindowRef window);

    // Render different calendar views
    void RenderDayView(const Calendar& calendar, const Rect& bounds);
    void RenderWeekView(const Calendar& calendar, const Rect& bounds);
    void RenderMonthView(const Calendar& calendar, const Rect& bounds);
    void RenderYearView(const Calendar& calendar, const Rect& bounds);

private:
    // Helper methods for different view rendering
    void DrawDayViewHeader(const Date& date, const Rect& bounds);
    void DrawDayViewGrid(const Rect& bounds);
    void DrawEventsForDay(const Calendar& calendar, const Date& date, const Rect& bounds);

    void DrawWeekViewHeader(const Calendar& calendar, const Rect& bounds);
    void DrawWeekViewGrid(const Rect& bounds);
    void DrawEventsForWeek(const Calendar& calendar, const Rect& bounds);

    void DrawMonthViewHeader(const Calendar& calendar, const Rect& bounds);
    void DrawMonthViewGrid(const Rect& bounds);
    void DrawEventsForMonth(const Calendar& calendar, const Rect& bounds);

    void DrawYearViewHeader(const Calendar& calendar, const Rect& bounds);
    void DrawYearViewGrid(const Rect& bounds);

    // Window and rendering context
    WindowRef mMainWindow;
    GWorldPtr mGWorld;
    bool mIsInitialized;
};

#endif // UI_RENDERER_H