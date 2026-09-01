/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * Recurrence rule evaluation engine
 */

#include "recurrence.h"
#include "calendar.h"

// Simple recurrence rule evaluator
RecurrenceEngine::RecurrenceEngine()
{
}

RecurrenceEngine::~RecurrenceEngine()
{
}

std::vector<Date> RecurrenceEngine::EvaluateRecurrence(const std::string& rule, const Date& startDate, const Date& endDate)
{
    std::vector<Date> occurrences;

    // Parse recurrence rule and generate occurrences
    // This is a simplified implementation - a full implementation would parse
    // the RRULE format properly according to RFC 5545

    if (rule.find("FREQ=DAILY") != std::string::npos)
    {
        // Daily recurrence
        Date current = startDate;
        while (current <= endDate)
        {
            occurrences.push_back(current);
            // Advance by 1 day (simplified)
            current = AdvanceDate(current, 1);
        }
    }
    else if (rule.find("FREQ=WEEKLY") != std::string::npos)
    {
        // Weekly recurrence
        Date current = startDate;
        while (current <= endDate)
        {
            occurrences.push_back(current);
            // Advance by 7 days
            current = AdvanceDate(current, 7);
        }
    }
    else if (rule.find("FREQ=MONTHLY") != std::string::npos)
    {
        // Monthly recurrence
        Date current = startDate;
        while (current <= endDate)
        {
            occurrences.push_back(current);
            // Advance by 1 month (simplified)
            current = AdvanceMonth(current, 1);
        }
    }
    else if (rule.find("FREQ=YEARLY") != std::string::npos)
    {
        // Yearly recurrence
        Date current = startDate;
        while (current <= endDate)
        {
            occurrences.push_back(current);
            // Advance by 1 year (simplified)
            current = AdvanceYear(current, 1);
        }
    }

    return occurrences;
}

Date RecurrenceEngine::AdvanceDate(const Date& date, int days)
{
    // Simple date advancement (this would be more sophisticated)
    Date newDate = date;

    // For this simple example, we'll just advance the day
    // In a real implementation, we'd handle month/year boundaries properly
    newDate.day += days;

    return newDate;
}

Date RecurrenceEngine::AdvanceMonth(const Date& date, int months)
{
    // Advance by months
    Date newDate = date;
    newDate.month += months;

    // Handle year overflow
    while (newDate.month > 12)
    {
        newDate.month -= 12;
        newDate.year++;
    }

    return newDate;
}

Date RecurrenceEngine::AdvanceYear(const Date& date, int years)
{
    Date newDate = date;
    newDate.year += years;
    return newDate;
}

bool RecurrenceEngine::IsRecurringEvent(const std::string& rule)
{
    // Check if rule contains recurrence information
    return !rule.empty() &&
           (rule.find("FREQ=") != std::string::npos ||
            rule.find("RRULE") != std::string::npos);
}