/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * UI Rendering engine -- draws directly into the current port.
 */

#include "ui_renderer.h"
#include "calendar.h"
#include <cstdio>
#include <cstring>

static const char* kMonthNames[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

static const char* kWeekdayNames[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

// DrawString needs a Pascal string (length byte + bytes); everything we
// draw is built at runtime (month names, day numbers), so convert here
// rather than hand-writing Pascal string literals for each one.
static void DrawCString(const char* str)
{
    unsigned char pstr[256];
    size_t len = strlen(str);
    if (len > 255) len = 255;
    pstr[0] = (unsigned char)len;
    memcpy(&pstr[1], str, len);
    DrawString(pstr);
}

// UI rendering implementation
UIRenderer::UIRenderer()
{
    mMainWindow = NULL;
    mIsInitialized = false;
    mMonthList = NULL;
}

UIRenderer::~UIRenderer()
{
    if (mMonthList)
    {
        LDispose(mMonthList);
    }
}

bool UIRenderer::Initialize(WindowPtr window)
{
    mMainWindow = window;
    mIsInitialized = (window != NULL);
    return mIsInitialized;
}

void UIRenderer::RenderDayView(const Calendar& calendar, const Rect& bounds)
{
    if (!mIsInitialized)
        return;

    Date currentDate = calendar.GetCurrentDate();

    RGBColor backgroundColor = {0xCCCC, 0xCCCC, 0xCCCC};
    RGBBackColor(&backgroundColor);
    EraseRect(&bounds);

    DrawDayViewHeader(currentDate, bounds);
    DrawDayViewGrid(bounds);
    DrawEventsForDay(calendar, currentDate, bounds);
}

void UIRenderer::RenderWeekView(const Calendar& calendar, const Rect& bounds)
{
    if (!mIsInitialized)
        return;

    RGBColor backgroundColor = {0xCCCC, 0xCCCC, 0xCCCC};
    RGBBackColor(&backgroundColor);
    EraseRect(&bounds);

    DrawWeekViewHeader(calendar, bounds);
    DrawWeekViewGrid(bounds);
    DrawEventsForWeek(calendar, bounds);
}

void UIRenderer::RenderMonthView(const Calendar& calendar, const Rect& bounds)
{
    if (!mIsInitialized)
        return;

    RGBColor backgroundColor = {0xCCCC, 0xCCCC, 0xCCCC};
    RGBBackColor(&backgroundColor);
    EraseRect(&bounds);

    DrawMonthViewHeader(calendar, bounds);
    DrawMonthViewGrid(calendar, bounds);
    DrawEventsForMonth(calendar, bounds);
}

void UIRenderer::RenderYearView(const Calendar& calendar, const Rect& bounds)
{
    if (!mIsInitialized)
        return;

    RGBColor backgroundColor = {0xCCCC, 0xCCCC, 0xCCCC};
    RGBBackColor(&backgroundColor);
    EraseRect(&bounds);

    DrawYearViewHeader(calendar, bounds);
    DrawYearViewGrid(bounds);
}

void UIRenderer::DrawDayViewHeader(const Date& date, const Rect& bounds)
{
    // Draw the header with date information
    // In a real implementation, this would use Geneva font and proper Mac OS 9 UI
}

void UIRenderer::DrawDayViewGrid(const Rect& bounds)
{
    // Draw the grid for day view - time columns and slots
}

void UIRenderer::DrawEventsForDay(const Calendar& calendar, const Date& date, const Rect& bounds)
{
    // Render events for a specific day
    std::vector<Event> events = calendar.GetEventsForDate(date);
    // Render each event on the grid
}

void UIRenderer::DrawWeekViewHeader(const Calendar& calendar, const Rect& bounds)
{
    // Draw the week view header
}

void UIRenderer::DrawWeekViewGrid(const Rect& bounds)
{
    // Draw the grid for week view
}

void UIRenderer::DrawEventsForWeek(const Calendar& calendar, const Rect& bounds)
{
    // Render events for a week view
}

void UIRenderer::DrawMonthViewHeader(const Calendar& calendar, const Rect& bounds)
{
    Date date = calendar.GetCurrentDate();

    TextSize(18);
    TextFace(bold);

    char titleBuf[64];
    snprintf(titleBuf, sizeof(titleBuf), "%s %d", kMonthNames[date.month - 1], date.year);
    MoveTo(bounds.left + 10, bounds.top + 24);
    DrawCString(titleBuf);

    TextFace(normal);
    TextSize(10);

    int colWidth = (bounds.right - bounds.left) / 7;
    for (int i = 0; i < 7; i++)
    {
        MoveTo(bounds.left + i * colWidth + 4, bounds.top + 44);
        DrawCString(kWeekdayNames[i]);
    }

    MoveTo(bounds.left, bounds.top + 48);
    LineTo(bounds.right, bounds.top + 48);
}

void UIRenderer::DrawMonthViewGrid(const Calendar& calendar, const Rect& bounds)
{
    Date date = calendar.GetCurrentDate();
    int firstWeekday = Date(date.year, date.month, 1).DayOfWeek();
    int daysInMonth = Date::DaysInMonth(date.year, date.month);
    int numRows = (firstWeekday + daysInMonth + 6) / 7;
    if (numRows < 1) numRows = 1;

    // The row/column count changes with the month, so the list is rebuilt
    // on every render rather than resized in place.
    if (mMonthList)
    {
        LDispose(mMonthList);
        mMonthList = NULL;
    }

    Rect gridRect;
    gridRect.top = bounds.top + 52;
    gridRect.left = bounds.left;
    gridRect.bottom = bounds.bottom;
    gridRect.right = bounds.right;

    Point cellSize;
    cellSize.h = (short)((gridRect.right - gridRect.left) / 7);
    cellSize.v = (short)((gridRect.bottom - gridRect.top) / numRows);

    Rect dataBounds;
    dataBounds.top = 0;
    dataBounds.left = 0;
    dataBounds.bottom = (short)numRows;
    dataBounds.right = 7;

    TextSize(10);
    TextFace(normal);

    mMonthList = LNew(&gridRect, &dataBounds, cellSize, 0, mMainWindow, true, false, false, false);
    if (!mMonthList)
        return;

    for (int day = 1; day <= daysInMonth; day++)
    {
        int index = firstWeekday + day - 1;

        Cell cell;
        cell.h = (short)(index % 7);
        cell.v = (short)(index / 7);

        char numBuf[8];
        snprintf(numBuf, sizeof(numBuf), "%d", day);
        LSetCell(numBuf, (short)strlen(numBuf), cell, mMonthList);
    }

    LUpdate(mMainWindow->visRgn, mMonthList);
}

void UIRenderer::DrawEventsForMonth(const Calendar& calendar, const Rect& bounds)
{
    if (!mMonthList)
        return;

    Date date = calendar.GetCurrentDate();
    int firstWeekday = Date(date.year, date.month, 1).DayOfWeek();
    int daysInMonth = Date::DaysInMonth(date.year, date.month);
    const std::map<std::string, ColorRGB>& categories = calendar.GetCategories();

    TextSize(9);

    for (int day = 1; day <= daysInMonth; day++)
    {
        Date cellDate(date.year, date.month, day);
        std::vector<Event> events = calendar.GetEventsForDate(cellDate);
        if (events.empty())
            continue;

        int index = firstWeekday + day - 1;
        Cell cell;
        cell.h = (short)(index % 7);
        cell.v = (short)(index / 7);

        Rect cellRect;
        LRect(&cellRect, cell, mMonthList);

        ColorRGB color(128, 128, 128);
        std::map<std::string, ColorRGB>::const_iterator catIt = categories.find(events[0].GetCategory());
        if (catIt != categories.end())
            color = catIt->second;

        Rect swatch;
        swatch.top = cellRect.top + 16;
        swatch.left = cellRect.left + 4;
        swatch.bottom = swatch.top + 8;
        swatch.right = swatch.left + 8;

        RGBColor rgb;
        rgb.red = (unsigned short)(color.r << 8);
        rgb.green = (unsigned short)(color.g << 8);
        rgb.blue = (unsigned short)(color.b << 8);
        RGBForeColor(&rgb);
        PaintRect(&swatch);

        RGBColor black = {0, 0, 0};
        RGBForeColor(&black);

        char titleBuf[16];
        snprintf(titleBuf, sizeof(titleBuf), "%.10s", events[0].GetTitle().c_str());
        MoveTo(swatch.right + 3, swatch.bottom);
        DrawCString(titleBuf);
    }
}

void UIRenderer::DrawYearViewHeader(const Calendar& calendar, const Rect& bounds)
{
    // Draw the year view header
}

void UIRenderer::DrawYearViewGrid(const Rect& bounds)
{
    // Draw the grid for year view
}
