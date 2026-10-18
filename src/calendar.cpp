/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * Calendar management and view rendering
 */

#include <algorithm>
#include <cstdio>
#include <OSUtils.h>
#include "calendar.h"
#include "event.h"
#include "ical_parser.h"
#include "ui_renderer.h"

// Calendar implementation
Calendar::Calendar()
    : mCurrentView(DayView), mCurrentDate(Today()), mNextID(1), mDirty(false), mPrefsDirty(false)
{
    InitializeDefaultCategories();
}

Calendar::~Calendar()
{
    // Clean up
}

Date Calendar::Today()
{
    unsigned long secs = 0;
    DateTimeRec rec;
    GetDateTime(&secs);
    SecondsToDate(secs, &rec);
    return Date(rec.year, rec.month, rec.day);
}

bool Calendar::LoadEventsFromText(const std::string& utf8, long gmtOffsetSeconds)
{
    ICALParser parser;
    std::vector<Event> parsed = parser.Parse(utf8, gmtOffsetSeconds);

    for (size_t i = 0; i < parsed.size(); i++)
    {
        Event& e = parsed[i];
        if (e.GetCategory().empty() && !mCategories.empty())
            e.SetCategory(mCategories.find("Home") != mCategories.end() ?
                          std::string("Home") : mCategories.begin()->first);
        if (!e.GetCategory().empty() && mCategories.find(e.GetCategory()) == mCategories.end())
            AddCategory(e.GetCategory(), NextPaletteColor());   // visible by default
        AddEvent(e);
    }

    mDirty = false;
    return !parsed.empty();
}

std::string Calendar::SerializeEvents(long gmtOffsetSeconds, unsigned long utcNowMacSeconds) const
{
    ICALParser parser;
    return parser.Serialize(mEvents, gmtOffsetSeconds, utcNowMacSeconds);
}

ColorRGB Calendar::NextPaletteColor() const
{
    // Apple palette: red, orange, yellow, green, blue, purple, brown
    static const unsigned char palette[7][3] = {
        {255, 59, 48}, {255, 149, 0}, {255, 204, 0}, {52, 199, 89},
        {0, 122, 255}, {175, 82, 222}, {162, 132, 94}
    };
    for (int i = 0; i < 7; i++)
    {
        bool used = false;
        for (std::map<std::string, ColorRGB>::const_iterator it = mCategories.begin();
             it != mCategories.end(); ++it)
        {
            if (it->second.r == palette[i][0] && it->second.g == palette[i][1] &&
                it->second.b == palette[i][2])
            {
                used = true;
                break;
            }
        }
        if (!used)
            return ColorRGB(palette[i][0], palette[i][1], palette[i][2]);
    }
    int n = (int)(mCategories.size() % 7);
    return ColorRGB(palette[n][0], palette[n][1], palette[n][2]);
}

unsigned long Calendar::AddEvent(const Event& event)
{
    Event e(event);
    unsigned long id = mNextID++;
    e.SetID(id);
    if (e.GetUID().empty())
    {
        unsigned long secs = 0;
        GetDateTime(&secs);
        char buf[64];
        snprintf(buf, sizeof(buf), "%lu-%lu@kalendae.local", secs, id);
        e.SetUID(buf);
    }
    mEvents.push_back(e);
    SortEvents();
    mDirty = true;
    return id;
}

Event* Calendar::FindEvent(unsigned long id)
{
    for (size_t i = 0; i < mEvents.size(); i++)
        if (mEvents[i].GetID() == id) return &mEvents[i];
    return NULL;
}

const Event* Calendar::FindEvent(unsigned long id) const
{
    for (size_t i = 0; i < mEvents.size(); i++)
        if (mEvents[i].GetID() == id) return &mEvents[i];
    return NULL;
}

bool Calendar::UpdateEvent(const Event& event)
{
    Event* existing = FindEvent(event.GetID());
    if (!existing) return false;
    *existing = event;
    SortEvents();
    mDirty = true;
    return true;
}

bool Calendar::RemoveEvent(unsigned long id)
{
    for (size_t i = 0; i < mEvents.size(); i++)
    {
        if (mEvents[i].GetID() == id)
        {
            mEvents.erase(mEvents.begin() + i);
            mDirty = true;
            return true;
        }
    }
    return false;
}

void Calendar::AddExDate(unsigned long id, const Date& date)
{
    Event* e = FindEvent(id);
    if (!e) return;
    e->AddExDate(date);
    mDirty = true;
}

void Calendar::SetCategoryVisible(const std::string& name, bool visible)
{
    mCategoryVisible[name] = visible;
    mPrefsDirty = true;
}

bool Calendar::IsCategoryVisible(const std::string& name) const
{
    std::map<std::string, bool>::const_iterator it = mCategoryVisible.find(name);
    return it == mCategoryVisible.end() ? true : it->second;
}

// Appends one occurrence per qualifying event for `date`, unsorted.
// Recurrence is a stub: an event occurs on every date it spans.
void Calendar::AppendOccurrences(std::vector<Occurrence>& out, const Date& date) const
{
    for (size_t i = 0; i < mEvents.size(); i++)
    {
        const Event& e = mEvents[i];
        if (!e.IsOnDate(date)) continue;
        if (!IsCategoryVisible(e.GetCategory())) continue;
        if (e.IsExDate(date)) continue;

        Occurrence occ;
        occ.eventID = e.GetID();
        occ.date = date;
        occ.event = &e;
        out.push_back(occ);
    }
}

// All-day first, then start minute, then title; date is the primary key.
static bool OccurrenceLess(const Occurrence& a, const Occurrence& b)
{
    if (!(a.date == b.date)) return a.date < b.date;
    bool aAll = a.event->IsAllDay();
    bool bAll = b.event->IsAllDay();
    if (aAll != bAll) return aAll;
    if (!aAll && a.event->GetStartMinute() != b.event->GetStartMinute())
        return a.event->GetStartMinute() < b.event->GetStartMinute();
    return a.event->GetTitle() < b.event->GetTitle();
}

std::vector<Occurrence> Calendar::GetOccurrencesForDate(const Date& date) const
{
    std::vector<Occurrence> out;
    AppendOccurrences(out, date);
    std::stable_sort(out.begin(), out.end(), OccurrenceLess);
    return out;
}

std::vector<Occurrence> Calendar::GetOccurrencesInRange(const Date& start, const Date& end) const
{
    std::vector<Occurrence> out;
    long last = end.ToSerial();
    for (long s = start.ToSerial(); s <= last; s++)
        AppendOccurrences(out, Date::FromSerial(s));
    std::stable_sort(out.begin(), out.end(), OccurrenceLess);
    return out;
}

void Calendar::SetCurrentView(ViewType view)
{
    mCurrentView = view;
}

ViewType Calendar::GetCurrentView() const
{
    return mCurrentView;
}

void Calendar::SetCurrentDate(const Date& date)
{
    mCurrentDate = date;
}

Date Calendar::GetCurrentDate() const
{
    return mCurrentDate;
}

void Calendar::ProcessBackgroundTasks()
{
    // Handle any background tasks like reminders
    CheckForUpcomingReminders();
}

void Calendar::InitializeDefaultCategories()
{
    // Set up default color categories
    mCategories["Home"] = ColorRGB(0, 122, 255);   // Apple blue
    mCategories["Work"] = ColorRGB(255, 59, 48);   // Apple red
}

void Calendar::CheckForUpcomingReminders()
{
    // Check for events that need reminders
    // This would integrate with Mac OS 9 Notification Manager
}

void Calendar::SortEvents()
{
    // Sort events by start time
    std::sort(mEvents.begin(), mEvents.end(),
        [](const Event& a, const Event& b) {
            return a.GetStartTime() < b.GetStartTime();
        });
}