/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * iCalendar (.ics) parser and writer interface. Buffer based and free of
 * Toolbox calls (the GMT offset and "now" are passed in), so it can be
 * compiled and tested on a host.
 */

#ifndef ICAL_PARSER_H
#define ICAL_PARSER_H

#include <string>
#include <vector>
#include "event.h"

class ICALParser
{
public:
    // Parses UTF-8 iCalendar text into events (IDs left 0; text converted to
    // MacRoman). gmtOffsetSeconds: local = UTC + offset. Malformed events
    // (no DTSTART) are skipped.
    std::vector<Event> Parse(const std::string& utf8Text, long gmtOffsetSeconds);

    // Writes UTF-8 iCalendar text with CRLF line ends and 75-octet folding.
    // utcNowMacSeconds (seconds since 1904-01-01, UTC) stamps DTSTAMP.
    std::string Serialize(const std::vector<Event>& events, long gmtOffsetSeconds,
                          unsigned long utcNowMacSeconds);
};

// MacRoman <-> UTF-8. Unmappable code points and invalid UTF-8 become '?'.
std::string MacRomanToUTF8(const std::string& macRoman);
std::string UTF8ToMacRoman(const std::string& utf8);

#endif // ICAL_PARSER_H
