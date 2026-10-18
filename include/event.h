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

    // 0 = Sunday .. 6 = Saturday (Sakamoto's algorithm)
    int DayOfWeek() const
    {
        static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
        int y = year;
        if (month < 3) y -= 1;
        return ((y + y / 4 - y / 100 + y / 400 + t[month - 1] + day) % 7 + 7) % 7;
    }

    static bool IsLeapYear(int y)
    {
        return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    }

    static int DaysInMonth(int y, int m)
    {
        static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (m == 2 && IsLeapYear(y)) return 29;
        return days[m - 1];
    }

    // Days since 1904-01-01 (serial 0, the Mac epoch), proleptic Gregorian.
    // Pure integer arithmetic (days-from-civil), valid across leap years.
    long ToSerial() const
    {
        return DaysFromCivil(year, month, day) - DaysFromCivil(1904, 1, 1);
    }

    static Date FromSerial(long serial)
    {
        long z = serial + DaysFromCivil(1904, 1, 1) + 719468L;
        long era = (z >= 0 ? z : z - 146096L) / 146097L;
        long doe = z - era * 146097L;
        long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        long y = yoe + era * 400;
        long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        long mp = (5 * doy + 2) / 153;
        long d = doy - (153 * mp + 2) / 5 + 1;
        long m = mp < 10 ? mp + 3 : mp - 9;
        if (m <= 2) y++;
        return Date((int)y, (int)m, (int)d);
    }

    Date AddDays(int delta) const
    {
        return FromSerial(ToSerial() + delta);
    }

private:
    // Days since 1970-01-01 for a civil date.
    static long DaysFromCivil(int yy, int m, int d)
    {
        long y = yy;
        if (m <= 2) y--;
        long era = (y >= 0 ? y : y - 399) / 400;
        long yoe = y - era * 400;
        long doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097L + doe - 719468L;
    }
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

    // Identity. mID is runtime-only, assigned by Calendar::AddEvent (0 = none).
    void SetID(unsigned long id) { mID = id; }
    unsigned long GetID() const { return mID; }

    // iCalendar UID (persisted)
    void SetUID(const std::string& uid) { mUID = uid; }
    std::string GetUID() const { return mUID; }

    // Time of day, minutes after midnight (0..1440); ignored when all-day
    void SetStartMinute(short m) { mStartMinute = m; }
    short GetStartMinute() const { return mStartMinute; }
    void SetEndMinute(short m) { mEndMinute = m; }
    short GetEndMinute() const { return mEndMinute; }

    // Alert, minutes before start (-1 = none)
    void SetAlertMinutes(short m) { mAlertMinutes = m; }
    short GetAlertMinutes() const { return mAlertMinutes; }

    // Recurrence exceptions
    void AddExDate(const Date& d) { if (!IsExDate(d)) mExDates.push_back(d); }
    const std::vector<Date>& GetExDates() const { return mExDates; }
    bool IsExDate(const Date& d) const
    {
        for (size_t i = 0; i < mExDates.size(); i++)
            if (mExDates[i] == d) return true;
        return false;
    }

    // Comparison (for sorting and lookup)
    bool operator<(const Event& other) const
    {
        return mStartTime < other.mStartTime;
    }

    bool operator==(const Event& other) const
    {
        return mID == other.mID;
    }

private:
    unsigned long mID;
    std::string mUID;
    short mStartMinute;
    short mEndMinute;
    short mAlertMinutes;
    std::vector<Date> mExDates;
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