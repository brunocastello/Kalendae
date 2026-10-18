/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * iCalendar (.ics) parser and writer (RFC 5545 subset)
 */

#include <cstdio>
#include "ical_parser.h"

// Unicode code points for MacRoman 0x80..0xFF (0xDB is the pre-8.5 currency
// sign, so the euro sign is unmappable and becomes '?').
static const unsigned short kMacRomanTable[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1,
    0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
    0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
    0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
    0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF,
    0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
    0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211,
    0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
    0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
    0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
    0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA,
    0x00FF, 0x0178, 0x2044, 0x00A4, 0x2039, 0x203A, 0xFB01, 0xFB02,
    0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,
    0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
    0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
    0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7
};

static void AppendUTF8(std::string& out, unsigned long cp)
{
    if (cp < 0x80)
    {
        out += (char)cp;
    }
    else if (cp < 0x800)
    {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    }
    else
    {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

std::string MacRomanToUTF8(const std::string& macRoman)
{
    std::string out;
    out.reserve(macRoman.size());
    for (size_t i = 0; i < macRoman.size(); i++)
    {
        unsigned char c = (unsigned char)macRoman[i];
        if (c < 0x80)
            out += (char)c;
        else
            AppendUTF8(out, kMacRomanTable[c - 0x80]);
    }
    return out;
}

std::string UTF8ToMacRoman(const std::string& utf8)
{
    std::string out;
    out.reserve(utf8.size());
    size_t n = utf8.size();
    size_t i = 0;
    while (i < n)
    {
        unsigned char c = (unsigned char)utf8[i];
        if (c < 0x80)
        {
            out += (char)c;
            i++;
            continue;
        }

        int extra = 0;
        unsigned long cp = 0;
        unsigned long minCP = 0;
        if (c >= 0xC2 && c <= 0xDF) { extra = 1; cp = c & 0x1F; minCP = 0x80; }
        else if (c >= 0xE0 && c <= 0xEF) { extra = 2; cp = c & 0x0F; minCP = 0x800; }
        else if (c >= 0xF0 && c <= 0xF4) { extra = 3; cp = c & 0x07; minCP = 0x10000; }

        bool ok = extra > 0 && i + (size_t)extra < n;
        if (ok)
        {
            for (int k = 1; k <= extra; k++)
            {
                unsigned char cc = (unsigned char)utf8[i + k];
                if ((cc & 0xC0) != 0x80) { ok = false; break; }
                cp = (cp << 6) | (cc & 0x3F);
            }
        }
        if (ok && (cp < minCP || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)))
            ok = false;

        if (!ok)
        {
            out += '?';
            i++;
            continue;
        }

        char mapped = '?';
        if (cp <= 0xFFFF)
        {
            for (int t = 0; t < 128; t++)
            {
                if (kMacRomanTable[t] == cp)
                {
                    mapped = (char)(0x80 + t);
                    break;
                }
            }
        }
        out += mapped;
        i += extra + 1;
    }
    return out;
}

// ------------------------------------------------------------------ helpers

static char UpperChar(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static std::string Upper(const std::string& s)
{
    std::string r(s);
    for (size_t i = 0; i < r.size(); i++)
        r[i] = UpperChar(r[i]);
    return r;
}

static bool IsDigit(char c)
{
    return c >= '0' && c <= '9';
}

// Parses `count` digits at s[pos]; returns false if any is missing.
static bool ParseDigits(const std::string& s, size_t pos, size_t count, long& out)
{
    if (pos + count > s.size())
        return false;
    long v = 0;
    for (size_t i = 0; i < count; i++)
    {
        if (!IsDigit(s[pos + i]))
            return false;
        v = v * 10 + (s[pos + i] - '0');
    }
    out = v;
    return true;
}

static long FloorDiv(long a, long b)
{
    long q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        q--;
    return q;
}

struct DateTimeValue
{
    Date date;
    long secondOfDay;
    bool isDate;      // date-only form
    bool valid;
    DateTimeValue() : secondOfDay(0), isDate(false), valid(false) {}
};

// YYYYMMDD or YYYYMMDDTHHMMSS[Z]. UTC values are shifted into local time.
static DateTimeValue ParseDateTimeValue(const std::string& v, long gmtOffset)
{
    DateTimeValue r;
    long y, m, d;
    if (!ParseDigits(v, 0, 4, y) || !ParseDigits(v, 4, 2, m) || !ParseDigits(v, 6, 2, d))
        return r;
    if (y < 1 || m < 1 || m > 12 || d < 1 || d > Date::DaysInMonth((int)y, (int)m))
        return r;
    r.date = Date((int)y, (int)m, (int)d);

    if (v.size() < 9 || (v[8] != 'T' && v[8] != 't'))
    {
        r.isDate = true;
        r.valid = true;
        return r;
    }

    long hh, mm, ss = 0;
    if (!ParseDigits(v, 9, 2, hh) || !ParseDigits(v, 11, 2, mm))
        return r;
    size_t next = 13;
    if (ParseDigits(v, 13, 2, ss))
        next = 15;
    else
        ss = 0;
    if (hh > 24 || mm > 59 || ss > 60)
        return r;

    r.secondOfDay = hh * 3600L + mm * 60L + ss;
    bool utc = next < v.size() && (v[next] == 'Z' || v[next] == 'z');
    if (utc)
    {
        long total = r.date.ToSerial() * 86400L + r.secondOfDay + gmtOffset;
        long day = FloorDiv(total, 86400L);
        r.secondOfDay = total - day * 86400L;
        r.date = Date::FromSerial(day);
    }
    r.valid = true;
    return r;
}

// Parses an RFC 5545 duration ([+-]P[nW][nD][T[nH][nM][nS]]) into signed seconds.
static bool ParseDurationSeconds(const std::string& v, long& out)
{
    size_t i = 0;
    long sign = 1;
    if (i < v.size() && (v[i] == '+' || v[i] == '-'))
    {
        if (v[i] == '-') sign = -1;
        i++;
    }
    if (i >= v.size() || (v[i] != 'P' && v[i] != 'p'))
        return false;
    i++;

    long total = 0;
    bool inTime = false;
    bool any = false;
    while (i < v.size())
    {
        char c = UpperChar(v[i]);
        if (c == 'T')
        {
            inTime = true;
            i++;
            continue;
        }
        if (!IsDigit(c))
            return false;
        long n = 0;
        while (i < v.size() && IsDigit(v[i]))
        {
            if (n < 100000000L)
                n = n * 10 + (v[i] - '0');
            i++;
        }
        if (i >= v.size())
            return false;
        char unit = UpperChar(v[i]);
        i++;
        long mult = 0;
        if (unit == 'W' && !inTime) mult = 604800L;
        else if (unit == 'D' && !inTime) mult = 86400L;
        else if (unit == 'H' && inTime) mult = 3600L;
        else if (unit == 'M' && inTime) mult = 60L;
        else if (unit == 'S' && inTime) mult = 1L;
        else return false;
        if (n > 2000000000L / mult)
            n = 2000000000L / mult;
        total += n * mult;
        if (total > 2000000000L)
            total = 2000000000L;
        any = true;
    }
    if (!any)
        return false;
    out = sign * total;
    return true;
}

// Unescapes TEXT: \\ \; \, \n \N
static std::string UnescapeText(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++)
    {
        if (s[i] == '\\' && i + 1 < s.size())
        {
            char n = s[i + 1];
            if (n == 'n' || n == 'N')
                out += '\n';
            else
                out += n;
            i++;
        }
        else
        {
            out += s[i];
        }
    }
    return out;
}

static std::string Trim(const std::string& s)
{
    size_t a = 0;
    size_t b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
    return s.substr(a, b - a);
}

// First comma-separated value, honouring backslash escapes.
static std::string FirstListValue(const std::string& s)
{
    for (size_t i = 0; i < s.size(); i++)
    {
        if (s[i] == '\\')
            i++;
        else if (s[i] == ',')
            return s.substr(0, i);
    }
    return s;
}

struct Param
{
    std::string name;   // upper case
    std::string value;  // quotes removed
};

struct ContentLine
{
    std::string name;   // upper case
    std::vector<Param> params;
    std::string value;
};

// name *(";" param) ":" value. Returns false if the line has no colon.
static bool ParseContentLine(const std::string& line, ContentLine& out)
{
    out.name.clear();
    out.params.clear();
    out.value.clear();

    size_t n = line.size();
    size_t i = 0;
    while (i < n && line[i] != ':' && line[i] != ';')
        i++;
    if (i >= n)
        return false;
    out.name = Upper(Trim(line.substr(0, i)));

    while (i < n && line[i] == ';')
    {
        i++;
        size_t nameStart = i;
        while (i < n && line[i] != '=' && line[i] != ';' && line[i] != ':')
            i++;
        Param p;
        p.name = Upper(line.substr(nameStart, i - nameStart));
        if (i < n && line[i] == '=')
        {
            i++;
            bool quoted = false;
            while (i < n)
            {
                char c = line[i];
                if (c == '"')
                    quoted = !quoted;
                else if (!quoted && (c == ';' || c == ':'))
                    break;
                else
                    p.value += c;
                i++;
            }
        }
        out.params.push_back(p);
    }

    if (i >= n || line[i] != ':')
        return false;
    out.value = line.substr(i + 1);
    return !out.name.empty();
}

static std::string ParamValue(const ContentLine& l, const char* name)
{
    for (size_t i = 0; i < l.params.size(); i++)
        if (l.params[i].name == name)
            return Upper(l.params[i].value);
    return std::string();
}

// Splits into logical lines: CR, LF or CRLF ends a line, and a following
// SPACE or HTAB folds the next physical line into this one.
static void UnfoldLines(const std::string& text, std::vector<std::string>& lines)
{
    std::string cur;
    size_t n = text.size();
    size_t i = 0;
    while (i < n)
    {
        char c = text[i];
        if (c == '\r' || c == '\n')
        {
            if (c == '\r' && i + 1 < n && text[i + 1] == '\n')
                i += 2;
            else
                i++;
            if (i < n && (text[i] == ' ' || text[i] == '\t'))
            {
                i++;    // continuation: drop the break and one whitespace
            }
            else
            {
                if (!cur.empty())
                    lines.push_back(cur);
                cur.clear();
            }
        }
        else
        {
            cur += c;
            i++;
        }
    }
    if (!cur.empty())
        lines.push_back(cur);
}

// ------------------------------------------------------------------- parse

struct EventBuilder
{
    std::string title, location, description, category, uid, rrule;
    DateTimeValue start;
    DateTimeValue end;
    bool hasEnd;
    bool hasDuration;
    long durationSeconds;
    short alert;
    bool haveAlert;
    std::vector<Date> exDates;

    EventBuilder() { Reset(); }

    void Reset()
    {
        title.clear(); location.clear(); description.clear();
        category.clear(); uid.clear(); rrule.clear();
        start = DateTimeValue();
        end = DateTimeValue();
        hasEnd = false;
        hasDuration = false;
        durationSeconds = 0;
        alert = -1;
        haveAlert = false;
        exDates.clear();
    }
};

static std::string TextValue(const std::string& raw)
{
    return UTF8ToMacRoman(UnescapeText(raw));
}

static void ApplyEventProperty(EventBuilder& b, const ContentLine& l, long gmt)
{
    if (l.name == "SUMMARY") b.title = TextValue(l.value);
    else if (l.name == "LOCATION") b.location = TextValue(l.value);
    else if (l.name == "DESCRIPTION") b.description = TextValue(l.value);
    else if (l.name == "CATEGORIES")
        b.category = Trim(UTF8ToMacRoman(UnescapeText(FirstListValue(l.value))));
    else if (l.name == "UID") b.uid = l.value;
    else if (l.name == "RRULE") b.rrule = l.value;
    else if (l.name == "DTSTART") b.start = ParseDateTimeValue(Trim(l.value), gmt);
    else if (l.name == "DTEND")
    {
        b.end = ParseDateTimeValue(Trim(l.value), gmt);
        b.hasEnd = b.end.valid;
    }
    else if (l.name == "DURATION")
    {
        b.hasDuration = ParseDurationSeconds(Trim(l.value), b.durationSeconds);
    }
    else if (l.name == "EXDATE")
    {
        size_t pos = 0;
        const std::string& v = l.value;
        while (pos <= v.size())
        {
            size_t comma = v.find(',', pos);
            if (comma == std::string::npos)
                comma = v.size();
            DateTimeValue d = ParseDateTimeValue(Trim(v.substr(pos, comma - pos)), gmt);
            if (d.valid)
                b.exDates.push_back(d.date);
            pos = comma + 1;
        }
    }
}

static void ApplyAlarmProperty(EventBuilder& b, const ContentLine& l)
{
    if (l.name != "TRIGGER" || b.haveAlert)
        return;
    std::string valueType = ParamValue(l, "VALUE");
    if (!valueType.empty() && valueType != "DURATION")
        return;
    std::string related = ParamValue(l, "RELATED");
    if (!related.empty() && related != "START")
        return;
    long secs;
    if (!ParseDurationSeconds(Trim(l.value), secs))
        return;
    long minutes = secs < 0 ? (-secs) / 60 : 0;
    if (minutes > 32000)
        minutes = 32000;
    b.alert = (short)minutes;
    b.haveAlert = true;
}

// Builds the model event; returns false if it has no usable DTSTART.
static bool FinishEvent(const EventBuilder& b, Event& e)
{
    if (!b.start.valid)
        return false;

    e = Event();
    e.SetTitle(b.title);
    e.SetLocation(b.location);
    e.SetDescription(b.description);
    e.SetCategory(b.category);
    e.SetUID(b.uid);
    e.SetRecurrenceRule(b.rrule);
    e.SetAlertMinutes(b.alert);
    for (size_t i = 0; i < b.exDates.size(); i++)
        e.AddExDate(b.exDates[i]);

    e.SetStartTime(b.start.date);

    if (b.start.isDate)
    {
        e.SetAllDay(true);
        Date endDate = b.start.date;
        if (b.hasEnd)
        {
            // DTEND is exclusive
            Date candidate = b.end.date.AddDays(-1);
            if (candidate > endDate)
                endDate = candidate;
        }
        else if (b.hasDuration && b.durationSeconds >= 2 * 86400L)
        {
            endDate = b.start.date.AddDays((int)(b.durationSeconds / 86400L) - 1);
        }
        e.SetEndTime(endDate);
        return true;
    }

    e.SetAllDay(false);
    long startTotal = b.start.date.ToSerial() * 86400L + b.start.secondOfDay;
    long endTotal = startTotal;
    if (b.hasEnd)
        endTotal = b.end.date.ToSerial() * 86400L + b.end.secondOfDay;
    else if (b.hasDuration && b.durationSeconds > 0)
        endTotal = startTotal + b.durationSeconds;
    if (endTotal < startTotal)
        endTotal = startTotal;

    long endDay = FloorDiv(endTotal, 86400L);
    long endSod = endTotal - endDay * 86400L;
    Date endDate = Date::FromSerial(endDay);
    short endMinute = (short)(endSod / 60);
    if (endSod == 0 && endDate > b.start.date)
    {
        // Ends at midnight: belongs to the previous day as minute 1440
        endDate = endDate.AddDays(-1);
        endMinute = 1440;
    }

    e.SetStartMinute((short)(b.start.secondOfDay / 60 > 1440 ? 1440 : b.start.secondOfDay / 60));
    e.SetEndTime(endDate);
    e.SetEndMinute(endMinute);
    return true;
}

std::vector<Event> ICALParser::Parse(const std::string& utf8Text, long gmtOffsetSeconds)
{
    std::vector<Event> result;
    std::vector<std::string> lines;
    UnfoldLines(utf8Text, lines);

    bool inCalendar = false;
    bool inEvent = false;
    bool inAlarm = false;
    int skipDepth = 0;      // inside an ignored component
    EventBuilder builder;
    ContentLine line;

    for (size_t li = 0; li < lines.size(); li++)
    {
        if (!ParseContentLine(lines[li], line))
            continue;

        if (line.name == "BEGIN")
        {
            std::string what = Upper(Trim(line.value));
            if (skipDepth > 0)
                skipDepth++;
            else if (what == "VCALENDAR" && !inCalendar)
                inCalendar = true;
            else if (what == "VEVENT" && inCalendar && !inEvent)
            {
                inEvent = true;
                inAlarm = false;
                builder.Reset();
            }
            else if (what == "VALARM" && inEvent && !inAlarm)
                inAlarm = true;
            else
                skipDepth = 1;
            continue;
        }

        if (line.name == "END")
        {
            std::string what = Upper(Trim(line.value));
            if (skipDepth > 0)
            {
                skipDepth--;
            }
            else if (what == "VALARM")
            {
                inAlarm = false;
            }
            else if (what == "VEVENT" && inEvent)
            {
                Event e;
                if (FinishEvent(builder, e))
                    result.push_back(e);
                inEvent = false;
                inAlarm = false;
            }
            else if (what == "VCALENDAR")
            {
                inCalendar = false;
            }
            continue;
        }

        if (skipDepth > 0 || !inEvent)
            continue;

        if (inAlarm)
            ApplyAlarmProperty(builder, line);
        else
            ApplyEventProperty(builder, line, gmtOffsetSeconds);
    }

    // Truncated file: keep a final unterminated event if it is usable
    if (inEvent && skipDepth == 0)
    {
        Event e;
        if (FinishEvent(builder, e))
            result.push_back(e);
    }
    return result;
}

// --------------------------------------------------------------- serialize

static std::string Num(const char* fmt, long a, long b = 0, long c = 0)
{
    char buf[32];
    snprintf(buf, sizeof(buf), fmt, a, b, c);
    return std::string(buf);
}

static std::string FormatDate(const Date& d)
{
    return Num("%04ld%02ld%02ld", d.year, d.month, d.day);
}

static std::string FormatLocal(const Date& d, long minute)
{
    return FormatDate(d) + Num("T%02ld%02ld00", minute / 60, minute % 60);
}

static std::string EscapeText(const std::string& macRoman)
{
    std::string utf8 = MacRomanToUTF8(macRoman);
    std::string out;
    out.reserve(utf8.size());
    for (size_t i = 0; i < utf8.size(); i++)
    {
        char c = utf8[i];
        if (c == '\\' || c == ';' || c == ',')
        {
            out += '\\';
            out += c;
        }
        else if (c == '\n')
            out += "\\n";
        else if (c != '\r')
            out += c;
    }
    return out;
}

// Appends one content line, folded at 75 octets, never inside a UTF-8 sequence.
static void AppendFolded(std::string& out, const std::string& line)
{
    size_t pos = 0;
    size_t limit = 75;
    while (line.size() - pos > limit)
    {
        size_t cut = pos + limit;
        while (cut > pos + 1 && ((unsigned char)line[cut] & 0xC0) == 0x80)
            cut--;
        out.append(line, pos, cut - pos);
        out += "\r\n ";
        pos = cut;
        limit = 74;
    }
    out.append(line, pos, std::string::npos);
    out += "\r\n";
}

std::string ICALParser::Serialize(const std::vector<Event>& events, long gmtOffsetSeconds,
                                  unsigned long utcNowMacSeconds)
{
    (void)gmtOffsetSeconds;     // times are written as floating local time

    std::string out;
    AppendFolded(out, "BEGIN:VCALENDAR");
    AppendFolded(out, "VERSION:2.0");
    AppendFolded(out, "PRODID:-//Kalendae//Mac OS 9//EN");
    AppendFolded(out, "CALSCALE:GREGORIAN");

    long nowDay = (long)(utcNowMacSeconds / 86400UL);
    long nowSod = (long)(utcNowMacSeconds % 86400UL);
    std::string stamp = FormatDate(Date::FromSerial(nowDay)) +
        Num("T%02ld%02ld%02ldZ", nowSod / 3600, (nowSod / 60) % 60, nowSod % 60);

    for (size_t i = 0; i < events.size(); i++)
    {
        const Event& e = events[i];
        bool allDay = e.IsAllDay();

        AppendFolded(out, "BEGIN:VEVENT");
        if (!e.GetUID().empty())
            AppendFolded(out, "UID:" + e.GetUID());
        AppendFolded(out, "DTSTAMP:" + stamp);

        if (allDay)
        {
            Date endDate = e.GetEndTime();
            if (endDate < e.GetStartTime())
                endDate = e.GetStartTime();
            AppendFolded(out, "DTSTART;VALUE=DATE:" + FormatDate(e.GetStartTime()));
            AppendFolded(out, "DTEND;VALUE=DATE:" + FormatDate(endDate.AddDays(1)));
        }
        else
        {
            long sm = e.GetStartMinute();
            if (sm < 0) sm = 0;
            if (sm > 1439) sm = 1439;
            long em = e.GetEndMinute();
            if (em < 0) em = 0;
            if (em > 1440) em = 1440;
            Date endDate = e.GetEndTime();
            if (em == 1440)
            {
                endDate = endDate.AddDays(1);
                em = 0;
            }
            AppendFolded(out, "DTSTART:" + FormatLocal(e.GetStartTime(), sm));
            AppendFolded(out, "DTEND:" + FormatLocal(endDate, em));
        }

        AppendFolded(out, "SUMMARY:" + EscapeText(e.GetTitle()));
        if (!e.GetLocation().empty())
            AppendFolded(out, "LOCATION:" + EscapeText(e.GetLocation()));
        if (!e.GetDescription().empty())
            AppendFolded(out, "DESCRIPTION:" + EscapeText(e.GetDescription()));
        if (!e.GetCategory().empty())
            AppendFolded(out, "CATEGORIES:" + EscapeText(e.GetCategory()));
        if (!e.GetRecurrenceRule().empty())
            AppendFolded(out, "RRULE:" + e.GetRecurrenceRule());

        const std::vector<Date>& ex = e.GetExDates();
        for (size_t k = 0; k < ex.size(); k++)
        {
            if (allDay)
                AppendFolded(out, "EXDATE;VALUE=DATE:" + FormatDate(ex[k]));
            else
            {
                long sm = e.GetStartMinute();
                if (sm < 0) sm = 0;
                if (sm > 1439) sm = 1439;
                AppendFolded(out, "EXDATE:" + FormatLocal(ex[k], sm));
            }
        }

        if (e.GetAlertMinutes() >= 0)
        {
            AppendFolded(out, "BEGIN:VALARM");
            AppendFolded(out, "ACTION:DISPLAY");
            AppendFolded(out, "DESCRIPTION:Reminder");
            if (e.GetAlertMinutes() == 0)
                AppendFolded(out, "TRIGGER:PT0S");
            else
                AppendFolded(out, "TRIGGER:" + Num("-PT%ldM", e.GetAlertMinutes()));
            AppendFolded(out, "END:VALARM");
        }
        AppendFolded(out, "END:VEVENT");
    }

    AppendFolded(out, "END:VCALENDAR");
    return out;
}
