/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * iCalendar (.ics) file parser
 */

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include "ical_parser.h"
#include "event.h"
#include "calendar.h"

// Simple parser for iCalendar format
ICALParser::ICALParser()
{
}

ICALParser::~ICALParser()
{
}

std::vector<Event> ICALParser::ParseFile(const std::string& filename)
{
    std::vector<Event> events;
    std::ifstream file(filename);

    if (!file.is_open())
    {
        return events; // Return empty vector on error
    }

    std::string line;
    Event currentEvent;
    std::string currentProperty;
    bool inVCalendar = false;
    bool inVEvent = false;

    while (std::getline(file, line))
    {
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);

        if (line.empty())
            continue;

        // Parse different sections of the iCalendar file
        if (line == "BEGIN:VCALENDAR")
        {
            inVCalendar = true;
            continue;
        }
        else if (line == "END:VCALENDAR")
        {
            inVCalendar = false;
            continue;
        }
        else if (line == "BEGIN:VEVENT")
        {
            inVEvent = true;
            currentEvent = Event(); // Reset for new event
            continue;
        }
        else if (line == "END:VEVENT")
        {
            inVEvent = false;

            // Add completed event to list
            if (!currentEvent.GetTitle().empty())
            {
                events.push_back(currentEvent);
            }
            continue;
        }

        if (inVEvent)
        {
            // Parse properties of the VEVENT
            size_t colonPos = line.find(':');
            if (colonPos != std::string::npos)
            {
                std::string property = line.substr(0, colonPos);
                std::string value = line.substr(colonPos + 1);

                // Handle different properties
                if (property == "SUMMARY")
                {
                    currentEvent.SetTitle(value);
                }
                else if (property == "DTSTART")
                {
                    // Parse date-time (simplified)
                    Date date = ParseDate(value);
                    currentEvent.SetStartTime(date);
                }
                else if (property == "DTEND")
                {
                    // Parse date-time (simplified)
                    Date date = ParseDate(value);
                    currentEvent.SetEndTime(date);
                }
                else if (property == "LOCATION")
                {
                    currentEvent.SetLocation(value);
                }
                else if (property == "DESCRIPTION")
                {
                    currentEvent.SetDescription(value);
                }
                else if (property == "RRULE")
                {
                    currentEvent.SetRecurrenceRule(value);
                }
                else if (property == "CATEGORIES")
                {
                    // Handle category
                    currentEvent.SetCategory(value);
                }
            }
        }
    }

    file.close();
    return events;
}

Date ICALParser::ParseDate(const std::string& dateString)
{
    // Simple date parsing - this would be more robust in a real implementation
    // Format typically looks like: 20260831T140000 (YYYYMMDDTHHMMSS)

    Date date(2026, 8, 31); // Default date for demo

    if (dateString.length() >= 8)
    {
        try
        {
            // Extract year
            int year = std::stoi(dateString.substr(0, 4));
            // Extract month
            int month = std::stoi(dateString.substr(4, 2));
            // Extract day
            int day = std::stoi(dateString.substr(6, 2));

            date = Date(year, month, day);
        }
        catch (...)
        {
            // If parsing fails, keep default date
        }
    }

    return date;
}

void ICALParser::ExportCalendar(const Calendar& calendar, const std::string& filename)
{
    // Export calendar data to iCalendar format
    std::ofstream file(filename);

    if (file.is_open())
    {
        file << "BEGIN:VCALENDAR\n";
        file << "VERSION:2.0\n";
        file << "PRODID:-//Kalendae//Mac OS 9 Calendar//EN\n";

        // Export each event
        std::vector<Event> events = calendar.GetEventsForDate(Date(2026, 8, 31)); // Simplified

        // In a real implementation, you would iterate over all events
        // For this demo, let's just add a sample event

        file << "BEGIN:VEVENT\n";
        file << "UID:123456789@example.com\n";
        file << "DTSTART:20260831T140000\n";
        file << "DTEND:20260831T150000\n";
        file << "SUMMARY:Sample Event\n";
        file << "LOCATION:Office\n";
        file << "DESCRIPTION:This is a sample event\n";
        file << "END:VEVENT\n";

        file << "END:VCALENDAR\n";
        file.close();
    }
}