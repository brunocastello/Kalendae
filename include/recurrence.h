/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * Recurrence rule evaluation engine interface
 */

#ifndef RECURRENCE_H
#define RECURRENCE_H

#include <string>
#include <vector>
#include "calendar.h"

class RecurrenceEngine
{
public:
    RecurrenceEngine();
    ~RecurrenceEngine();

    // Evaluate recurrence rules and return list of occurrence dates
    std::vector<Date> EvaluateRecurrence(const std::string& rule, const Date& startDate, const Date& endDate);

    // Check if an event is recurring
    bool IsRecurringEvent(const std::string& rule);

private:
    // Helper methods for date advancement
    Date AdvanceDate(const Date& date, int days);
    Date AdvanceMonth(const Date& date, int months);
    Date AdvanceYear(const Date& date, int years);
};

#endif // RECURRENCE_H