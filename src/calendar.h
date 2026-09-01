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

// Date structure for calendar operations
struct Date
{
    int year;
    int month;
    int day;

    Date() : year(0), month(0), day(0) {}
    Date(int y, int m, int d) : year(y), month(m), day(d) {}

    // Comparison operators
    bool operator==(const Date& other) const
    {
        return year == other.year && month == other.month && day == other.day;
    }
};

class Calendar
{
public:
    Calendar();
    ~Calendar();

    // Calendar data management
    void LoadFromPreferences();
    void SaveToPreferences();

    // Event management
    void AddEvent(const Event& event);
    void RemoveEvent(const Event& event);

    // Event querying
    std::vector<Event> GetEventsForDate(const Date& date) const;
    std::vector<Event> GetEventsForPeriod(const Date& startDate, const Date& endDate) const;

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
        mCategories[name] = color;
    }

    const std::map<std::string, ColorRGB>& GetCategories() const
    {
        return mCategories;
    }

private:
    ViewType mCurrentView;
    Date mCurrentDate;
    std::vector<Event> mEvents;
    std::map<std::string, ColorRGB> mCategories;
};

#endif // CALENDAR_H