/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * Calendar management and view rendering interface
 */

#ifndef CALENDAR_H
#define CALENDAR_H

#include <vector>
#include <map>
#include <string>
#include "event.h"

// Simple RGB color structure
struct ColorRGB
{
    unsigned char r, g, b;

    ColorRGB() : r(0), g(0), b(0) {}
    ColorRGB(unsigned char r_, unsigned char g_, unsigned char b_)
        : r(r_), g(g_), b(b_) {}
};

// Calendar view types
enum ViewType
{
    DayView,
    WeekView,
    MonthView,
    YearView
};

// One appearance of an event on a given date. `event` points into the
// Calendar's storage and is only valid until the next Calendar mutation.
struct Occurrence
{
    unsigned long eventID;
    Date date;
    const Event* event;
};

class Calendar
{
public:
    Calendar();
    ~Calendar();

    // Persistence (iCalendar text; the caller does the file I/O).
    // LoadEventsFromText adds the parsed events through AddEvent, auto-adding
    // calendars for unknown categories, and leaves the events clean. Returns
    // false if the text looks like it held events (or isn't iCalendar at
    // all) but none could be read, so the caller can keep the file safe.
    bool LoadEventsFromText(const std::string& utf8, long gmtOffsetSeconds);
    std::string SerializeEvents(long gmtOffsetSeconds, unsigned long utcNowMacSeconds) const;

    // Event management
    unsigned long AddEvent(const Event& event);   // returns the assigned ID
    Event* FindEvent(unsigned long id);
    const Event* FindEvent(unsigned long id) const;
    bool UpdateEvent(const Event& event);          // matched by ID, keeps ID
    bool RemoveEvent(unsigned long id);
    void AddExDate(unsigned long id, const Date& date);

    // Event querying (all-day first, then start minute, then title;
    // hidden categories and ExDates are skipped)
    std::vector<Occurrence> GetOccurrencesForDate(const Date& date) const;
    std::vector<Occurrence> GetOccurrencesInRange(const Date& start, const Date& end) const;
    const std::vector<Event>& GetAllEvents() const { return mEvents; }

    // Category visibility (missing key = visible)
    void SetCategoryVisible(const std::string& name, bool visible);
    bool IsCategoryVisible(const std::string& name) const;

    // Dirty tracking: events live in the events file, while calendars
    // (visibility, colours) and the view live in the preferences file.
    bool IsDirty() const { return mDirty; }
    void ClearDirty() { mDirty = false; }
    bool IsPrefsDirty() const { return mPrefsDirty; }
    void ClearPrefsDirty() { mPrefsDirty = false; }
    void MarkPrefsDirty() { mPrefsDirty = true; }

    static Date Today();

    // View management
    void SetCurrentView(ViewType view);
    ViewType GetCurrentView() const;

    // Date management
    void SetCurrentDate(const Date& date);
    Date GetCurrentDate() const;

    // Background tasks
    void ProcessBackgroundTasks();

    // Categories
    void InitializeDefaultCategories();
    void CheckForUpcomingReminders();

    // Utility methods
    void SortEvents();

    // Category management
    void AddCategory(const std::string& name, const ColorRGB& color)
    {
        std::string n(name);
        if (n.size() > 63)
            n.resize(63);       // Str63 in the prefs file
        mCategories[n] = color;
        mPrefsDirty = true;
    }

    // Drops every calendar (used when the preferences file supplies its own list)
    void ClearCategories()
    {
        mCategories.clear();
        mCategoryVisible.clear();
    }

    // Next colour of the Apple palette cycle not yet used by a calendar
    ColorRGB NextPaletteColor() const;

    const std::map<std::string, ColorRGB>& GetCategories() const
    {
        return mCategories;
    }

private:
    ViewType mCurrentView;
    Date mCurrentDate;
    std::vector<Event> mEvents;
    std::map<std::string, ColorRGB> mCategories;
    std::map<std::string, bool> mCategoryVisible;
    unsigned long mNextID;
    bool mDirty;
    bool mPrefsDirty;

    unsigned long AddEventInternal(const Event& event, bool sort);
    void AppendOccurrences(std::vector<Occurrence>& out, const Date& date) const;
};

#endif // CALENDAR_H