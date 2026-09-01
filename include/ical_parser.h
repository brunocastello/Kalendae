/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * iCalendar (.ics) file parser interface
 */

#ifndef ICAL_PARSER_H
#define ICAL_PARSER_H

#include <string>
#include <vector>
#include "event.h"
#include "calendar.h"

class ICALParser
{
public:
    ICALParser();
    ~ICALParser();

    // Parse iCalendar file and return list of events
    std::vector<Event> ParseFile(const std::string& filename);

    // Export calendar data to iCalendar format
    void ExportCalendar(const Calendar& calendar, const std::string& filename);

private:
    // Parse date string from iCalendar format (YYYYMMDDTHHMMSS)
    Date ParseDate(const std::string& dateString);

    // Helper methods for parsing different iCalendar properties
    std::string GetPropertyValue(const std::string& line, const std::string& property);
};

#endif // ICAL_PARSER_H