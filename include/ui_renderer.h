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

#include <Quickdraw.h>
#include <Windows.h>
#include "calendar.h"
#include <vector>

// One clickable rectangle registered while a view is drawn. eventID 0 is an
// empty slot. minute: -1 = all-day slot (Month cells, all-day strip, Year
// days), else minutes after midnight of the half-hour slot. r is window-local.
struct HitRegion
{
    Rect r;
    unsigned long eventID;
    Date date;
    short minute;
};

class UIRenderer
{
public:
    UIRenderer();
    ~UIRenderer();

    // Initialize the renderer with a window
    bool Initialize(WindowPtr window);

    // Render different calendar views. Each clears and refills the hit map.
    void RenderDayView(const Calendar& calendar, const Rect& bounds);
    void RenderWeekView(const Calendar& calendar, const Rect& bounds);
    void RenderMonthView(const Calendar& calendar, const Rect& bounds);
    void RenderYearView(const Calendar& calendar, const Rect& bounds);

    // Last-added match wins (chips are added after the slots under them).
    bool HitTest(Point local, HitRegion* out) const;
    // First drawn region of an event (window-local rect), for anchoring popovers.
    bool FindHitForEvent(unsigned long id, HitRegion* out) const;

    // 0 = none. The selected chip/block is drawn highlighted.
    void SetSelectedEvent(unsigned long id) { mSelectedEvent = id; }
    unsigned long GetSelectedEvent() const { return mSelectedEvent; }

    // Day/Week time grid scrolling. The grid is 24 hours of kHourHeight.
    // GetTimeGridViewRect reserves 16px on the right of contentBounds for the
    // scroll bar; the header/all-day heights come from the last render.
    // Computes the header / all-day strip heights for the calendar's current
    // view and date, so the view rect is valid before any render.
    void ComputeTimeGridLayout(const Calendar& calendar);
    // 8 AM, or earlier when a visible day has an earlier timed event.
    short DefaultTimeGridScroll(const Calendar& calendar) const;
    Rect GetTimeGridViewRect(const Rect& contentBounds) const;
    short GetTimeGridMaxScroll(const Rect& contentBounds) const;
    void SetTimeGridScroll(short px) { mTimeGridScroll = px; }
    short GetTimeGridScroll() const { return mTimeGridScroll; }
    short ScrollForMinute(short minute) const;
    // Redraws only the scrolled hour area (used while tracking the scroll bar)
    void RedrawTimeGrid(const Calendar& calendar, const Rect& contentBounds);

    static const short kHourHeight = 22;

private:
    void RenderTimeGrid(const Calendar& calendar, const Rect& bounds, bool weekMode);
    void DrawTimeGridHourArea(const Calendar& calendar, const Rect& bounds);

    void DrawMiniMonth(const Calendar& calendar, int year, int month, const Rect& bounds);

    // Window and rendering context
    WindowPtr mMainWindow;
    bool mIsInitialized;

    std::vector<HitRegion> mHits;
    unsigned long mSelectedEvent;

    short mTimeGridScroll;
    short mHeaderHeight;   // title (+ column headers) height of the last time grid
    short mAllDayHeight;   // all-day strip height of the last time grid
    size_t mGridHitBase;   // mHits.size() before the scrollable hour area
};

#endif // UI_RENDERER_H
