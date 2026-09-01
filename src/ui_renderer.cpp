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
}

UIRenderer::~UIRenderer()
{
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

    int gridTop = bounds.top + 52;
    int colWidth = (bounds.right - bounds.left) / 7;
    int numRows = (firstWeekday + daysInMonth + 6) / 7;
    if (numRows < 1) numRows = 1;
    int rowHeight = (bounds.bottom - gridTop) / numRows;

    TextSize(10);

    for (int day = 1; day <= daysInMonth; day++)
    {
        int index = firstWeekday + day - 1;
        int row = index / 7;
        int col = index % 7;

        Rect cell;
        cell.top = gridTop + row * rowHeight;
        cell.left = bounds.left + col * colWidth;
        cell.bottom = cell.top + rowHeight;
        cell.right = cell.left + colWidth;

        FrameRect(&cell);

        char numBuf[8];
        snprintf(numBuf, sizeof(numBuf), "%d", day);
        MoveTo(cell.left + 4, cell.top + 12);
        DrawCString(numBuf);
    }
}

void UIRenderer::DrawEventsForMonth(const Calendar& calendar, const Rect& bounds)
{
    // Render events for month view
}

void UIRenderer::DrawYearViewHeader(const Calendar& calendar, const Rect& bounds)
{
    // Draw the year view header
}

void UIRenderer::DrawYearViewGrid(const Rect& bounds)
{
    // Draw the grid for year view
}
