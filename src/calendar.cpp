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
    : mCurrentView(DayView), mCurrentDate(Today()), mNextID(1), mDirty(false)
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

void Calendar::LoadFromPreferences()
{
    // Load calendar configuration and events from preferences
    // This would typically read from System Folder:Preferences

    // A few sample events in the current month so the grid has something
    // to show before real .ics import is wired up.
    Event meeting("Team Meeting", Date(mCurrentDate.year, mCurrentDate.month, 5),
                  Date(mCurrentDate.year, mCurrentDate.month, 5));
    meeting.SetCategory("Work");
    meeting.SetStartMinute(10 * 60);
    meeting.SetEndMinute(11 * 60);
    AddEvent(meeting);

    Event trip("Weekend Trip", Date(mCurrentDate.year, mCurrentDate.month, 15),
               Date(mCurrentDate.year, mCurrentDate.month, 16));
    trip.SetCategory("Home");
    trip.SetAllDay(true);
    AddEvent(trip);

    Event meetup("Retro Meetup", Date(mCurrentDate.year, mCurrentDate.month, 22),
                 Date(mCurrentDate.year, mCurrentDate.month, 22));
    meetup.SetCategory("Home");
    meetup.SetStartMinute(18 * 60);
    meetup.SetEndMinute(20 * 60);
    AddEvent(meetup);
}

void Calendar::SaveToPreferences()
{
    // Save calendar configuration and events to preferences
    // This would typically write to System Folder:Preferences
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
    mDirty = true;
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