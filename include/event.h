/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * Event management and recurrence handling interface
 */

#ifndef EVENT_H
#define EVENT_H

#include <string>
#include <vector>

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

    bool operator<(const Date& other) const
    {
        if (year != other.year) return year < other.year;
        if (month != other.month) return month < other.month;
        return day < other.day;
    }

    bool operator<=(const Date& other) const { return *this < other || *this == other; }
    bool operator>(const Date& other) const { return other < *this; }
    bool operator>=(const Date& other) const { return other < *this || *this == other; }
};

class Event
{
public:
    Event();
    Event(const std::string& title, const Date& startDate, const Date& endDate);
    ~Event();

    // Event properties
    void SetTitle(const std::string& title) { mTitle = title; }
    std::string GetTitle() const { return mTitle; }

    void SetStartTime(const Date& time) { mStartTime = time; }
    Date GetStartTime() const { return mStartTime; }

    void SetEndTime(const Date& time) { mEndTime = time; }
    Date GetEndTime() const { return mEndTime; }

    void SetAllDay(bool allDay) { mAllDay = allDay; }
    bool IsAllDay() const { return mAllDay; }

    // Date and time operations
    bool IsOnDate(const Date& date) const;
    bool IntersectsPeriod(const Date& startDate, const Date& endDate) const;

    // Recurrence
    void SetRecurrenceRule(const std::string& rule);
    std::string GetRecurrenceRule() const;
    bool IsRecurring() const;
    void GetRecurrenceOccurrences(std::vector<Date>& occurrences, const Date& startDate, const Date& endDate) const;

    // Categories
    void SetCategory(const std::string& category) { mCategory = category; }
    std::string GetCategory() const { return mCategory; }

    // Location and description
    void SetLocation(const std::string& location) { mLocation = location; }
    std::string GetLocation() const { return mLocation; }

    void SetDescription(const std::string& description) { mDescription = description; }
    std::string GetDescription() const { return mDescription; }

    // Comparison (for sorting and lookup)
    bool operator<(const Event& other) const
    {
        return mStartTime < other.mStartTime;
    }

    bool operator==(const Event& other) const
    {
        return mTitle == other.mTitle && mStartTime == other.mStartTime && mEndTime == other.mEndTime;
    }

private:
    std::string mTitle;
    Date mStartTime;
    Date mEndTime;
    bool mAllDay;
    std::string mRecurrenceRule;
    std::string mCategory;
    std::string mLocation;
    std::string mDescription;
};

#endif // EVENT_H