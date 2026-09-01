/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * UI Rendering engine interface using QuickDraw
 *
 * Draws directly into the window's own port (the caller is responsible
 * for SetPort'ing to it first, e.g. from an updateEvt handler). There is
 * no offscreen GWorld here: nothing animates yet, so there's no flicker
 * to double-buffer against, and an offscreen buffer would just be a
 * second full-window color pixmap competing for the same tiny classic
 * Mac OS memory partition.
 */

#ifndef UI_RENDERER_H
#define UI_RENDERER_H

// Every classic-named Multiversal header (Quickdraw.h, Windows.h, ...) is
// just a wrapper around one master Multiverse.h containing every Manager's
// declarations, so <Windows.h> already brings in the List Manager too --
// there is no separate ListMgr.h/Lists.h file to include.
#include <Quickdraw.h>
#include <Windows.h>
#include "calendar.h"

class UIRenderer
{
public:
    UIRenderer();
    ~UIRenderer();

    // Initialize the renderer with a window
    bool Initialize(WindowPtr window);

    // Render different calendar views
    void RenderDayView(const Calendar& calendar, const Rect& bounds);
    void RenderWeekView(const Calendar& calendar, const Rect& bounds);
    void RenderMonthView(const Calendar& calendar, const Rect& bounds);
    void RenderYearView(const Calendar& calendar, const Rect& bounds);

    // Only valid to call while Month view is the one currently rendered
    // (mMonthList is rebuilt/disposed on every RenderMonthView call, so it
    // may be null or stale otherwise). Returns true, and fills outDate,
    // only when localPt was a *double*-click on a valid day cell --
    // LClick's own double-click detection, not hand-rolled timing logic.
    bool HandleMonthClick(const Calendar& calendar, Point localPt, Date* outDate);

private:
    // Helper methods for different view rendering
    void DrawDayViewHeader(const Date& date, const Rect& bounds);
    void DrawDayViewGrid(const Calendar& calendar, const Date& date, const Rect& bounds);
    void DrawEventsForDay(const Calendar& calendar, const Date& date, const Rect& bounds);

    void DrawWeekViewHeader(const Calendar& calendar, const Rect& bounds);
    void DrawWeekViewGrid(const Rect& bounds);
    void DrawEventsForWeek(const Calendar& calendar, const Rect& bounds);

    void DrawMonthViewHeader(const Calendar& calendar, const Rect& bounds);
    void DrawMonthViewGrid(const Calendar& calendar, const Rect& bounds);
    void DrawEventsForMonth(const Calendar& calendar, const Rect& bounds);

    void DrawYearViewHeader(const Calendar& calendar, const Rect& bounds);
    void DrawYearViewGrid(const Calendar& calendar, const Rect& bounds);
    void DrawMiniMonth(int year, int month, const Rect& bounds);

    // Window and rendering context
    WindowPtr mMainWindow;
    bool mIsInitialized;

    // The month grid and day event list both use the real List Manager
    // (standard Mac OS UI for any tabular display) instead of hand-drawn
    // cells. Rebuilt on every render since row/column counts change.
    ListHandle mMonthList;
    ListHandle mDayList;
};

#endif // UI_RENDERER_H