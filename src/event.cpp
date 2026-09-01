/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * Event management and recurrence handling
 */

#include "event.h"
#include "calendar.h"

Event::Event() : mAllDay(false)
{
}

Event::Event(const std::string& title, const Date& startDate, const Date& endDate)
    : mTitle(title), mStartTime(startDate), mEndTime(endDate), mAllDay(false)
{
}

Event::~Event()
{
}

bool Event::IsOnDate(const Date& date) const
{
    // Simple check - event spans the given date
    // In a full implementation, this would handle recurrence rules
    return mStartTime <= date && mEndTime >= date;
}

bool Event::IntersectsPeriod(const Date& startDate, const Date& endDate) const
{
    // Check if the event overlaps with the given period
    return mStartTime <= endDate && mEndTime >= startDate;
}

void Event::SetRecurrenceRule(const std::string& rule)
{
    mRecurrenceRule = rule;
    // Parse and store recurrence rule (e.g., "FREQ=DAILY;COUNT=10")
}

std::string Event::GetRecurrenceRule() const
{
    return mRecurrenceRule;
}

bool Event::IsRecurring() const
{
    return !mRecurrenceRule.empty();
}

void Event::GetRecurrenceOccurrences(std::vector<Date>& occurrences, const Date& startDate, const Date& endDate) const
{
    // In a full implementation, this would calculate all occurrences
    // of a recurring event within the given date range

    // For now, just add the base event date if it falls in range
    if (IntersectsPeriod(startDate, endDate))
    {
        occurrences.push_back(mStartTime);
    }
}