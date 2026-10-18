/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * UI Rendering engine -- draws directly into the current port. Every view
 * is hand-drawn and registers a hit map (mHits) that main.cpp queries with
 * HitTest.
 */

#include "ui_renderer.h"
#include "calendar.h"
#include <OSUtils.h>
#include <Fonts.h>
#include <cstdio>
#include <cstring>

static const char* kMonthNames[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

static const char* kWeekdayNames[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

static const char* kFullWeekdayNames[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

static const short kGenevaFont = 3;
static const short kGutterWidth = 44;      // hour labels, left of the day columns
static const short kScrollBarReserve = 15; // right edge: the 16px bar overlaps the window frame by 1px
static const short kHeaderBackground = 0xDD;

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

static short CStringWidth(const char* str)
{
    size_t len = strlen(str);
    if (len > 255) len = 255;
    return TextWidth((Ptr)str, 0, (short)len);
}

static void ForeRGB(int r, int g, int b)
{
    RGBColor c;
    c.red = (unsigned short)(r * 257);
    c.green = (unsigned short)(g * 257);
    c.blue = (unsigned short)(b * 257);
    RGBForeColor(&c);
}

static void BackRGB(int r, int g, int b)
{
    RGBColor c;
    c.red = (unsigned short)(r * 257);
    c.green = (unsigned short)(g * 257);
    c.blue = (unsigned short)(b * 257);
    RGBBackColor(&c);
}

static void ResetColors()
{
    ForeRGB(0, 0, 0);
    BackRGB(255, 255, 255);
}

static void ForeFull(const ColorRGB& c) { ForeRGB(c.r, c.g, c.b); }

// ~30% colour blended with white
static void ForeTint(const ColorRGB& c)
{
    ForeRGB((c.r * 3 + 255 * 7) / 10, (c.g * 3 + 255 * 7) / 10, (c.b * 3 + 255 * 7) / 10);
}

static void ForeDark(const ColorRGB& c)
{
    ForeRGB(c.r * 45 / 100, c.g * 45 / 100, c.b * 45 / 100);
}

// Apple-style "today" red (a function, not a static object: no global ctors)
static void ForeTodayRed() { ForeRGB(222, 52, 44); }

static ColorRGB LookupCategoryColor(const Calendar& calendar, const std::string& category)
{
    const std::map<std::string, ColorRGB>& categories = calendar.GetCategories();
    std::map<std::string, ColorRGB>::const_iterator it = categories.find(category);
    if (it != categories.end())
        return it->second;
    return ColorRGB(128, 128, 128);
}

// Draws str at (x, baseline), truncated with an ellipsis to fit maxWidth.
static void DrawFitted(const std::string& str, short x, short baseline, short maxWidth)
{
    if (maxWidth <= 0)
        return;

    char buf[260];
    size_t len = str.size();
    if (len > 255) len = 255;
    memcpy(buf, str.c_str(), len);
    buf[len] = '\0';

    if (TextWidth(buf, 0, (short)len) > maxWidth)
    {
        while (len > 0)
        {
            len--;
            buf[len] = (char)0xC9; // MacRoman ellipsis
            if (TextWidth(buf, 0, (short)(len + 1)) <= maxWidth)
            {
                len++;
                break;
            }
        }
        if (len == 0)
            return;
    }

    MoveTo(x, baseline);
    DrawText(buf, 0, (short)len);
}

// Minutes the occurrence covers on its own date. A timed event spanning
// several days starts at 00:00 after its first day and runs to 24:00 until
// its last. Returns false when nothing is left to draw on that date.
static bool OccurrenceMinutes(const Occurrence& occ, short* startMin, short* endMin)
{
    const Event* ev = occ.event;
    short s = ev->GetStartMinute();
    short e = ev->GetEndMinute();
    if (!ev->IsRecurring())
    {
        if (occ.date > ev->GetStartTime()) s = 0;
        if (occ.date < ev->GetEndTime()) e = 1440;
    }
    if (e <= s)
        return false;
    if (s < 0) s = 0;
    if (s > 1410) s = 1410;
    if (e < s + 30) e = s + 30;
    if (e > 1440) e = 1440;
    *startMin = s;
    *endMin = e;
    return true;
}

static void FormatTime(short minute, char* out, size_t outSize)
{
    int h = minute / 60;
    int m = minute % 60;
    int h12 = (h % 12 == 0) ? 12 : h % 12;
    const char* ampm = (h >= 12 && h < 24) ? "PM" : "AM";
    if (m == 0)
        snprintf(out, outSize, "%d %s", h12, ampm);
    else
        snprintf(out, outSize, "%d:%02d %s", h12, m, ampm);
}

static void DrawTitle(short x, short baseline, const char* bold1, const char* plain)
{
    TextFont(0);
    TextSize(18);
    TextFace(bold);
    MoveTo(x, baseline);
    DrawCString(bold1);
    TextFace(normal);
    DrawCString(plain);
}

// Draws a day-number label with Apple's red "today" circle. right is the
// right edge the text (or circle) aligns to.
static void DrawDayLabel(const char* label, short right, short top, bool isToday, bool inMonth)
{
    TextFont(kGenevaFont);
    TextSize(10);
    if (isToday)
    {
        TextFace(bold);
        short w = CStringWidth(label);
        short ow = w + 10;
        if (ow < 18) ow = 18;
        Rect oval;
        oval.right = right + 2;
        oval.left = oval.right - ow;
        oval.top = top + 1;
        oval.bottom = oval.top + 18;
        ForeTodayRed();
        PaintOval(&oval);
        ForeRGB(255, 255, 255);
        MoveTo(oval.left + (ow - w) / 2, oval.top + 13);
        DrawCString(label);
        TextFace(normal);
    }
    else
    {
        if (inMonth) ForeRGB(0, 0, 0); else ForeRGB(136, 136, 136);
        TextFace(normal);
        MoveTo(right - CStringWidth(label), top + 12);
        DrawCString(label);
    }
    ForeRGB(0, 0, 0);
}

// UI rendering implementation
UIRenderer::UIRenderer()
{
    mMainWindow = NULL;
    mIsInitialized = false;
    mSelectedEvent = 0;
    mTimeGridScroll = 0;
    mHeaderHeight = 34;
    mAllDayHeight = 20;
    mGridHitBase = 0;
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

bool UIRenderer::HitTest(Point local, HitRegion* out) const
{
    for (size_t i = mHits.size(); i > 0; i--)
    {
        const HitRegion& h = mHits[i - 1];
        if (PtInRect(local, &h.r))
        {
            if (out)
                *out = h;
            return true;
        }
    }
    return false;
}

// Prefers the hit on `preferred` (recurring events have several); else the first.
bool UIRenderer::FindHitForEvent(unsigned long id, const Date& preferred, HitRegion* out) const
{
    if (id == 0)
        return false;
    int first = -1;
    for (size_t i = 0; i < mHits.size(); i++)
    {
        if (mHits[i].eventID != id)
            continue;
        if (first < 0)
            first = (int)i;
        if (mHits[i].date == preferred)
        {
            first = (int)i;
            break;
        }
    }
    if (first < 0)
        return false;
    if (out)
        *out = mHits[first];
    return true;
}

// ---------------------------------------------------------------- Month

void UIRenderer::RenderMonthView(const Calendar& calendar, const Rect& bounds)
{
    if (!mIsInitialized)
        return;

    mHits.clear();

    BackRGB(kHeaderBackground, kHeaderBackground, kHeaderBackground);
    EraseRect(&bounds);
    ForeRGB(0, 0, 0);

    Date cur = calendar.GetCurrentDate();
    Date today = Calendar::Today();

    char yearBuf[16];
    snprintf(yearBuf, sizeof(yearBuf), " %d", cur.year);
    DrawTitle(bounds.left + 10, bounds.top + 24, kMonthNames[cur.month - 1], yearBuf);

    // Weekday row: right-aligned like the day numbers, one rule beneath.
    short gridTop = bounds.top + 49;
    short width = bounds.right - bounds.left;
    short height = bounds.bottom - gridTop;
    const int rows = 6;

    TextFont(kGenevaFont);
    TextSize(10);
    TextFace(normal);
    for (int c = 0; c < 7; c++)
    {
        short colRight = bounds.left + (short)((long)(c + 1) * width / 7);
        MoveTo(colRight - 5 - CStringWidth(kWeekdayNames[c]), bounds.top + 44);
        DrawCString(kWeekdayNames[c]);
    }
    ForeRGB(0xBB, 0xBB, 0xBB);
    MoveTo(bounds.left, gridTop - 1);
    LineTo(bounds.right, gridTop - 1);

    Rect gridRect = bounds;
    gridRect.top = gridTop;
    BackRGB(255, 255, 255);
    EraseRect(&gridRect);

    for (int c = 1; c < 7; c++)
    {
        short x = bounds.left + (short)((long)c * width / 7);
        MoveTo(x, gridTop);
        LineTo(x, bounds.bottom);
    }
    for (int r = 1; r < rows; r++)
    {
        short y = gridTop + (short)((long)r * height / rows);
        MoveTo(bounds.left, y);
        LineTo(bounds.right, y);
    }

    int firstWeekday = Date(cur.year, cur.month, 1).DayOfWeek();
    Date startDate = Date(cur.year, cur.month, 1).AddDays(-firstWeekday);

    for (int r = 0; r < rows; r++)
    {
        for (int c = 0; c < 7; c++)
        {
            Date date = startDate.AddDays(r * 7 + c);

            Rect cell;
            cell.left = bounds.left + (short)((long)c * width / 7) + 1;
            cell.right = bounds.left + (short)((long)(c + 1) * width / 7);
            cell.top = gridTop + (short)((long)r * height / rows) + 1;
            cell.bottom = gridTop + (short)((long)(r + 1) * height / rows);

            HitRegion slot;
            slot.r = cell;
            slot.eventID = 0;
            slot.date = date;
            slot.minute = -1;
            mHits.push_back(slot);

            bool inMonth = (date.month == cur.month && date.year == cur.year);

            char label[24];
            if (date.day == 1)
                snprintf(label, sizeof(label), "1 %.3s", kMonthNames[date.month - 1]);
            else
                snprintf(label, sizeof(label), "%d", date.day);
            DrawDayLabel(label, cell.right - 5, cell.top, date == today, inMonth);

            std::vector<Occurrence> events = calendar.GetOccurrencesForDate(date);
            int n = (int)events.size();
            if (n == 0)
                continue;

            short startY = cell.top + 20;
            int avail = (cell.bottom - startY - 1) / 14;
            if (avail < 1)
                continue;
            int shown = (n <= avail) ? n : avail - 1;

            TextFont(kGenevaFont);
            TextSize(9);
            TextFace(normal);

            for (int k = 0; k < shown; k++)
            {
                const Occurrence& occ = events[k];
                ColorRGB color = LookupCategoryColor(calendar, occ.event->GetCategory());
                bool selected = (occ.eventID != 0 && occ.eventID == mSelectedEvent);

                Rect chip;
                chip.left = cell.left + 2;
                chip.right = cell.right - 2;
                chip.top = startY + k * 14;
                chip.bottom = chip.top + 13;

                short textX;
                if (occ.event->IsAllDay())
                {
                    if (selected) ForeFull(color); else ForeTint(color);
                    PaintRoundRect(&chip, 6, 6);
                    textX = chip.left + 4;
                    if (selected) ForeRGB(255, 255, 255); else ForeDark(color);
                }
                else
                {
                    if (selected)
                    {
                        ForeFull(color);
                        PaintRoundRect(&chip, 6, 6);
                        ForeRGB(255, 255, 255);
                    }
                    else
                    {
                        Rect bar = chip;
                        bar.right = bar.left + 3;
                        ForeFull(color);
                        PaintRect(&bar);
                        ForeRGB(0, 0, 0);
                    }
                    textX = chip.left + 7;
                }
                DrawFitted(occ.event->GetTitle(), textX, chip.top + 10, chip.right - textX - 2);

                HitRegion hit;
                hit.r = chip;
                hit.eventID = occ.eventID;
                hit.date = date;
                hit.minute = -1;
                mHits.push_back(hit);
            }

            if (n > shown)
            {
                char more[24];
                snprintf(more, sizeof(more), "+%d more", n - shown);
                ForeRGB(128, 128, 128);
                MoveTo(cell.left + 9, startY + shown * 14 + 10);
                DrawCString(more);
            }
            ForeRGB(0, 0, 0);
        }
    }

    ResetColors();
    TextFont(0);
    TextSize(12);
    TextFace(normal);
}

// ---------------------------------------------------------------- Year

void UIRenderer::RenderYearView(const Calendar& calendar, const Rect& bounds)
{
    if (!mIsInitialized)
        return;

    mHits.clear();

    BackRGB(kHeaderBackground, kHeaderBackground, kHeaderBackground);
    EraseRect(&bounds);
    ForeRGB(0, 0, 0);

    int year = calendar.GetCurrentDate().year;

    TextFont(0);
    TextSize(20);
    TextFace(bold);
    char titleBuf[8];
    snprintf(titleBuf, sizeof(titleBuf), "%d", year);
    MoveTo(bounds.left + 10, bounds.top + 26);
    DrawCString(titleBuf);
    TextFace(normal);

    const int cols = 4;
    const int rows = 3;
    int cellWidth = (bounds.right - bounds.left) / cols;
    int cellHeight = (bounds.bottom - bounds.top - 36) / rows;

    for (int month = 1; month <= 12; month++)
    {
        int col = (month - 1) % cols;
        int row = (month - 1) / cols;

        Rect cellBounds;
        cellBounds.top = bounds.top + 36 + row * cellHeight;
        cellBounds.left = bounds.left + col * cellWidth;
        cellBounds.bottom = cellBounds.top + cellHeight;
        cellBounds.right = cellBounds.left + cellWidth;

        DrawMiniMonth(calendar, year, month, cellBounds);
    }

    ResetColors();
    TextFont(0);
    TextSize(12);
    TextFace(normal);
}

void UIRenderer::DrawMiniMonth(const Calendar& calendar, int year, int month, const Rect& bounds)
{
    static const char* kMiniWeekdayNames[] = { "S", "M", "T", "W", "T", "F", "S" };

    TextFont(kGenevaFont);
    TextSize(10);
    TextFace(bold);
    MoveTo(bounds.left + 4, bounds.top + 12);
    DrawCString(kMonthNames[month - 1]);
    TextFace(normal);
    TextSize(9);

    int colWidth = (bounds.right - bounds.left - 8) / 7;
    short weekdayRow = bounds.top + 24;
    for (int i = 0; i < 7; i++)
    {
        MoveTo(bounds.left + 4 + i * colWidth, weekdayRow);
        DrawCString(kMiniWeekdayNames[i]);
    }

    Date today = Calendar::Today();
    int firstWeekday = Date(year, month, 1).DayOfWeek();
    int daysInMonth = Date::DaysInMonth(year, month);
    short rowHeight = 11;

    for (int day = 1; day <= daysInMonth; day++)
    {
        int index = firstWeekday + day - 1;
        int row = index / 7;
        int col = index % 7;

        short x = bounds.left + 4 + col * colWidth;
        short baseline = weekdayRow + 12 + row * rowHeight;

        char numBuf[4];
        snprintf(numBuf, sizeof(numBuf), "%d", day);

        Date date(year, month, day);
        bool isToday = (date == today);
        if (isToday)
        {
            short w = CStringWidth(numBuf);
            short cx = x + w / 2;
            Rect oval;
            oval.left = cx - 7;
            oval.right = cx + 7;
            oval.top = baseline - 11;
            oval.bottom = baseline + 3;
            ForeTodayRed();
            PaintOval(&oval);
            ForeRGB(255, 255, 255);
        }
        MoveTo(x, baseline);
        DrawCString(numBuf);
        ForeRGB(0, 0, 0);

        HitRegion hit;
        hit.r.left = x - 2;
        hit.r.right = x - 2 + colWidth;
        hit.r.top = baseline - 10;
        hit.r.bottom = hit.r.top + rowHeight;
        hit.eventID = 0;
        hit.date = date;
        hit.minute = -1;
        mHits.push_back(hit);
    }
}

// ---------------------------------------------------------- Day / Week

void UIRenderer::RenderDayView(const Calendar& calendar, const Rect& bounds)
{
    RenderTimeGrid(calendar, bounds, false);
}

void UIRenderer::RenderWeekView(const Calendar& calendar, const Rect& bounds)
{
    RenderTimeGrid(calendar, bounds, true);
}

// Header and all-day strip heights depend only on the calendar's current
// view and date, so they can be known before (and without) rendering.
void UIRenderer::ComputeTimeGridLayout(const Calendar& calendar)
{
    bool weekMode = (calendar.GetCurrentView() == WeekView);
    int numCols = weekMode ? 7 : 1;
    Date cur = calendar.GetCurrentDate();
    Date first = weekMode ? cur.AddDays(-cur.DayOfWeek()) : cur;

    int maxAllDay = 0;
    for (int i = 0; i < numCols; i++)
    {
        std::vector<Occurrence> occ = calendar.GetOccurrencesForDate(first.AddDays(i));
        int n = 0;
        for (size_t k = 0; k < occ.size(); k++)
            if (occ[k].event->IsAllDay())
                n++;
        if (n > maxAllDay)
            maxAllDay = n;
    }
    if (maxAllDay > 5) maxAllDay = 5;

    mHeaderHeight = weekMode ? 54 : 34;
    mAllDayHeight = (short)(4 + maxAllDay * 14);
    if (mAllDayHeight < 20) mAllDayHeight = 20;
}

// Scroll offset to use when entering Day/Week: 8 AM, or earlier if a
// visible day has a timed event starting before that.
short UIRenderer::DefaultTimeGridScroll(const Calendar& calendar) const
{
    bool weekMode = (calendar.GetCurrentView() == WeekView);
    int numCols = weekMode ? 7 : 1;
    Date cur = calendar.GetCurrentDate();
    Date first = weekMode ? cur.AddDays(-cur.DayOfWeek()) : cur;

    short minute = 8 * 60;
    for (int i = 0; i < numCols; i++)
    {
        std::vector<Occurrence> occ = calendar.GetOccurrencesForDate(first.AddDays(i));
        for (size_t k = 0; k < occ.size(); k++)
        {
            short sm, em;
            if (!occ[k].event->IsAllDay() && OccurrenceMinutes(occ[k], &sm, &em) && sm < minute)
                minute = sm;
        }
    }
    return ScrollForMinute(minute);
}

Rect UIRenderer::GetTimeGridViewRect(const Rect& contentBounds) const
{
    Rect r;
    r.top = contentBounds.top + mHeaderHeight + mAllDayHeight;
    r.left = contentBounds.left;
    r.right = contentBounds.right - kScrollBarReserve;
    r.bottom = contentBounds.bottom;
    if (r.bottom < r.top)
        r.bottom = r.top;
    return r;
}

short UIRenderer::GetTimeGridMaxScroll(const Rect& contentBounds) const
{
    Rect view = GetTimeGridViewRect(contentBounds);
    short max = (short)(24 * kHourHeight - (view.bottom - view.top));
    return max > 0 ? max : 0;
}

short UIRenderer::ScrollForMinute(short minute) const
{
    return (short)((long)minute * kHourHeight / 60);
}

void UIRenderer::RedrawTimeGrid(const Calendar& calendar, const Rect& contentBounds)
{
    if (!mIsInitialized)
        return;
    ViewType v = calendar.GetCurrentView();
    if (v != DayView && v != WeekView)
        return;

    GrafPtr savedPort;
    GetPort(&savedPort);
    SetPort(mMainWindow);

    if (mGridHitBase <= mHits.size())
        mHits.erase(mHits.begin() + mGridHitBase, mHits.end());
    DrawTimeGridHourArea(calendar, contentBounds);

    SetPort(savedPort);
}

void UIRenderer::RenderTimeGrid(const Calendar& calendar, const Rect& bounds, bool weekMode)
{
    if (!mIsInitialized)
        return;

    mHits.clear();

    Date cur = calendar.GetCurrentDate();
    int numCols = weekMode ? 7 : 1;
    Date first = weekMode ? cur.AddDays(-cur.DayOfWeek()) : cur;
    Date today = Calendar::Today();

    BackRGB(kHeaderBackground, kHeaderBackground, kHeaderBackground);
    EraseRect(&bounds);
    ForeRGB(0, 0, 0);

    ComputeTimeGridLayout(calendar);
    int maxAllDay = (mAllDayHeight - 4) / 14;

    std::vector<Occurrence> allDay[7];
    for (int i = 0; i < numCols; i++)
    {
        std::vector<Occurrence> occ = calendar.GetOccurrencesForDate(first.AddDays(i));
        for (size_t k = 0; k < occ.size(); k++)
            if (occ[k].event->IsAllDay())
                allDay[i].push_back(occ[k]);
    }

    short colLeft = bounds.left + kGutterWidth;
    short colRight = bounds.right - kScrollBarReserve;
    short colSpan = colRight - colLeft;
    if (colSpan < numCols) colSpan = (short)numCols;

    // Header
    if (weekMode)
    {
        char yearBuf[16];
        snprintf(yearBuf, sizeof(yearBuf), " %d", cur.year);
        DrawTitle(bounds.left + 10, bounds.top + 22, kMonthNames[cur.month - 1], yearBuf);

        TextFont(kGenevaFont);
        TextSize(10);
        for (int i = 0; i < numCols; i++)
        {
            Date d = first.AddDays(i);
            short x0 = colLeft + (short)((long)i * colSpan / numCols);
            short x1 = colLeft + (short)((long)(i + 1) * colSpan / numCols);

            char dayName[8], num[8];
            snprintf(dayName, sizeof(dayName), "%s ", kWeekdayNames[i]);
            snprintf(num, sizeof(num), "%d", d.day);

            TextFace(normal);
            short w1 = CStringWidth(dayName);
            TextFace(bold);
            short w2 = CStringWidth(num);
            short total = w1 + w2 + 10;
            short x = x0 + (short)((x1 - x0 - total) / 2);
            short baseline = bounds.top + 46;

            TextFace(normal);
            MoveTo(x, baseline);
            DrawCString(dayName);

            TextFace(bold);
            if (d == today)
            {
                Rect oval;
                oval.left = x + w1;
                oval.right = oval.left + w2 + 10;
                oval.top = baseline - 13;
                oval.bottom = baseline + 4;
                ForeTodayRed();
                PaintOval(&oval);
                ForeRGB(255, 255, 255);
            }
            MoveTo(x + w1 + 5, baseline);
            DrawCString(num);
            ForeRGB(0, 0, 0);
            TextFace(normal);
        }
    }
    else
    {
        TextFont(0);
        TextSize(18);
        TextFace(bold);
        char titleBuf[64];
        snprintf(titleBuf, sizeof(titleBuf), "%s, %s %d, %d",
                 kFullWeekdayNames[cur.DayOfWeek()], kMonthNames[cur.month - 1], cur.day, cur.year);
        MoveTo(bounds.left + 10, bounds.top + 24);
        DrawCString(titleBuf);
        TextFace(normal);
    }

    // All-day strip
    Rect strip;
    strip.top = bounds.top + mHeaderHeight;
    strip.bottom = strip.top + mAllDayHeight;
    strip.left = bounds.left;
    strip.right = bounds.right;
    BackRGB(255, 255, 255);
    EraseRect(&strip);

    TextFont(kGenevaFont);
    TextSize(9);
    TextFace(normal);
    ForeRGB(0x66, 0x66, 0x66);
    MoveTo(colLeft - 4 - CStringWidth("all-day"), strip.top + 14);
    DrawCString("all-day");

    ForeRGB(0xBB, 0xBB, 0xBB);
    for (int i = 0; i <= numCols; i++)
    {
        short x = colLeft + (short)((long)i * colSpan / numCols);
        MoveTo(x, strip.top);
        LineTo(x, strip.bottom - 1);
    }
    MoveTo(bounds.left, strip.bottom - 1);
    LineTo(colRight, strip.bottom - 1);

    for (int i = 0; i < numCols; i++)
    {
        Date d = first.AddDays(i);
        short x0 = colLeft + (short)((long)i * colSpan / numCols);
        short x1 = colLeft + (short)((long)(i + 1) * colSpan / numCols);

        HitRegion slot;
        slot.r.left = x0;
        slot.r.right = x1;
        slot.r.top = strip.top;
        slot.r.bottom = strip.bottom;
        slot.eventID = 0;
        slot.date = d;
        slot.minute = -1;
        mHits.push_back(slot);

        int total = (int)allDay[i].size();
        int shownBars = (total > maxAllDay && maxAllDay > 0) ? maxAllDay - 1 : total;
        if (total > maxAllDay && maxAllDay > 0)
        {
            char more[24];
            snprintf(more, sizeof(more), "+%d more", total - shownBars);
            ForeRGB(128, 128, 128);
            MoveTo(x0 + 6, strip.top + 2 + shownBars * 14 + 10);
            DrawCString(more);
        }
        for (int k = 0; k < shownBars; k++)
        {
            const Occurrence& occ = allDay[i][k];
            ColorRGB color = LookupCategoryColor(calendar, occ.event->GetCategory());
            bool selected = (occ.eventID != 0 && occ.eventID == mSelectedEvent);

            Rect bar;
            bar.left = x0 + 2;
            bar.right = x1 - 2;
            bar.top = strip.top + 2 + k * 14;
            bar.bottom = bar.top + 13;

            if (selected) ForeFull(color); else ForeTint(color);
            PaintRoundRect(&bar, 6, 6);
            if (selected) ForeRGB(255, 255, 255); else ForeDark(color);
            DrawFitted(occ.event->GetTitle(), bar.left + 4, bar.top + 10, bar.right - bar.left - 6);

            HitRegion hit;
            hit.r = bar;
            hit.eventID = occ.eventID;
            hit.date = d;
            hit.minute = -1;
            mHits.push_back(hit);
        }
    }

    ResetColors();
    mGridHitBase = mHits.size();
    DrawTimeGridHourArea(calendar, bounds);

    ResetColors();
    TextFont(0);
    TextSize(12);
    TextFace(normal);
}

namespace {
    struct TimedBlock
    {
        Occurrence occ;
        short startMin;
        short endMin;
        short lane;
        short lanes;
    };
}

void UIRenderer::DrawTimeGridHourArea(const Calendar& calendar, const Rect& bounds)
{
    bool weekMode = (calendar.GetCurrentView() == WeekView);
    int numCols = weekMode ? 7 : 1;
    Date cur = calendar.GetCurrentDate();
    Date first = weekMode ? cur.AddDays(-cur.DayOfWeek()) : cur;
    Date today = Calendar::Today();

    Rect view = GetTimeGridViewRect(bounds);
    short maxScroll = GetTimeGridMaxScroll(bounds);
    if (mTimeGridScroll > maxScroll) mTimeGridScroll = maxScroll;
    if (mTimeGridScroll < 0) mTimeGridScroll = 0;

    short colLeft = bounds.left + kGutterWidth;
    short colRight = view.right;
    short colSpan = colRight - colLeft;
    if (colSpan < numCols) colSpan = (short)numCols;
    short gridTop = view.top - mTimeGridScroll;

    RgnHandle savedClip = NewRgn();
    GetClip(savedClip);
    ClipRect(&view);

    BackRGB(255, 255, 255);
    EraseRect(&view);

    TextFont(kGenevaFont);
    TextSize(9);
    TextFace(normal);

    // Hour lines and labels
    for (int h = 0; h <= 24; h++)
    {
        short y = gridTop + h * kHourHeight;
        if (y < view.top - 12 || y > view.bottom + 12)
            continue;

        ForeRGB(0xD0, 0xD0, 0xD0);
        MoveTo(colLeft - 4, y);
        LineTo(colRight, y);

        if (h == 0 || h == 24)
            continue;
        char label[16];
        if (h == 12)
            snprintf(label, sizeof(label), "Noon");
        else
            FormatTime((short)(h * 60), label, sizeof(label));
        ForeRGB(0x66, 0x66, 0x66);
        MoveTo(colLeft - 6 - CStringWidth(label), y + 4);
        DrawCString(label);
    }

    // Column separators
    ForeRGB(0xBB, 0xBB, 0xBB);
    for (int i = 0; i <= numCols; i++)
    {
        short x = colLeft + (short)((long)i * colSpan / numCols);
        MoveTo(x, view.top);
        LineTo(x, view.bottom);
    }

    // Half-hour slots first, so event blocks added later win HitTest.
    for (int i = 0; i < numCols; i++)
    {
        short x0 = colLeft + (short)((long)i * colSpan / numCols);
        short x1 = colLeft + (short)((long)(i + 1) * colSpan / numCols);
        for (int s = 0; s < 48; s++)
        {
            Rect slot;
            slot.left = x0;
            slot.right = x1;
            slot.top = gridTop + s * (kHourHeight / 2);
            slot.bottom = slot.top + kHourHeight / 2;

            HitRegion hit;
            if (!SectRect(&slot, &view, &hit.r))
                continue;
            hit.eventID = 0;
            hit.date = first.AddDays(i);
            hit.minute = (short)(s * 30);
            mHits.push_back(hit);
        }
    }

    // Timed event blocks
    for (int i = 0; i < numCols; i++)
    {
        Date d = first.AddDays(i);
        short x0 = colLeft + (short)((long)i * colSpan / numCols);
        short x1 = colLeft + (short)((long)(i + 1) * colSpan / numCols);

        std::vector<Occurrence> occ = calendar.GetOccurrencesForDate(d);
        std::vector<TimedBlock> blocks;
        for (size_t k = 0; k < occ.size(); k++)
        {
            if (occ[k].event->IsAllDay())
                continue;
            TimedBlock b;
            b.occ = occ[k];
            if (!OccurrenceMinutes(occ[k], &b.startMin, &b.endMin))
                continue;
            b.lane = 0;
            b.lanes = 1;
            blocks.push_back(b);
        }

        // Insertion sort by start (stable), then cluster transitively
        // overlapping blocks and give each a lane.
        for (size_t a = 1; a < blocks.size(); a++)
        {
            TimedBlock key = blocks[a];
            size_t j = a;
            while (j > 0 && blocks[j - 1].startMin > key.startMin)
            {
                blocks[j] = blocks[j - 1];
                j--;
            }
            blocks[j] = key;
        }

        size_t ci = 0;
        while (ci < blocks.size())
        {
            std::vector<short> laneEnds;
            short clusterEnd = blocks[ci].endMin;
            size_t cj = ci;
            while (cj < blocks.size() && blocks[cj].startMin < clusterEnd)
            {
                size_t lane = 0;
                while (lane < laneEnds.size() && laneEnds[lane] > blocks[cj].startMin)
                    lane++;
                if (lane == laneEnds.size())
                    laneEnds.push_back(blocks[cj].endMin);
                else
                    laneEnds[lane] = blocks[cj].endMin;
                blocks[cj].lane = (short)lane;
                if (blocks[cj].endMin > clusterEnd) clusterEnd = blocks[cj].endMin;
                cj++;
            }
            for (size_t k = ci; k < cj; k++)
                blocks[k].lanes = (short)laneEnds.size();
            ci = cj;
        }

        short usable = x1 - x0 - 3;
        for (size_t k = 0; k < blocks.size(); k++)
        {
            const TimedBlock& b = blocks[k];
            ColorRGB color = LookupCategoryColor(calendar, b.occ.event->GetCategory());
            bool selected = (b.occ.eventID != 0 && b.occ.eventID == mSelectedEvent);

            // Lanes narrower than 4px overlap instead of shrinking away.
            short step = usable / b.lanes;
            short offset = b.lane * step;
            if (offset > usable - 4) offset = usable - 4;
            if (offset < 0) offset = 0;
            Rect blk;
            blk.left = x0 + 1 + offset;
            if (step < 4)
                blk.right = blk.left + 4;
            else
                blk.right = (b.lane == b.lanes - 1) ? (x0 + 1 + usable) : (blk.left + step - 1);
            blk.top = gridTop + (short)((long)b.startMin * kHourHeight / 60);
            blk.bottom = gridTop + (short)((long)b.endMin * kHourHeight / 60) - 1;
            if (blk.bottom < blk.top + 10) blk.bottom = blk.top + 10;

            Rect vis;
            if (!SectRect(&blk, &view, &vis))
                continue;

            if (selected) ForeFull(color); else ForeTint(color);
            PaintRect(&blk);
            ForeFull(color);
            FrameRect(&blk);
            Rect bar = blk;
            bar.right = bar.left + 3;
            PaintRect(&bar);

            ClipRect(&vis);
            if (selected) ForeRGB(255, 255, 255); else ForeDark(color);
            TextFace(bold);
            DrawFitted(b.occ.event->GetTitle(), blk.left + 6, blk.top + 10, blk.right - blk.left - 8);
            if (blk.bottom - blk.top >= 22)
            {
                TextFace(normal);
                char tbuf[16];
                FormatTime(b.startMin, tbuf, sizeof(tbuf));
                DrawFitted(tbuf, blk.left + 6, blk.top + 20, blk.right - blk.left - 8);
            }
            TextFace(normal);
            ClipRect(&view);

            HitRegion hit;
            hit.r = vis;
            hit.eventID = b.occ.eventID;
            hit.date = d;
            // Blocks report their own start slot rather than the all-day marker.
            hit.minute = b.startMin;
            mHits.push_back(hit);
        }

        // Current-time indicator
        if (d == today)
        {
            unsigned long secs = 0;
            DateTimeRec rec;
            GetDateTime(&secs);
            SecondsToDate(secs, &rec);
            short y = gridTop + (short)((long)(rec.hour * 60 + rec.minute) * kHourHeight / 60);
            ForeTodayRed();
            MoveTo(x0, y);
            LineTo(x1 - 1, y);
            Rect dot;
            dot.left = x0 - 2;
            dot.right = x0 + 3;
            dot.top = y - 2;
            dot.bottom = y + 3;
            PaintOval(&dot);
        }
    }

    SetClip(savedClip);
    DisposeRgn(savedClip);
    ResetColors();
    TextFont(0);
    TextSize(12);
    TextFace(normal);
}
