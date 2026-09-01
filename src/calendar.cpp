/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * Calendar management and view rendering
 */

#include <Carbon/Carbon.h>
#include "calendar.h"
#include "event.h"
#include "ical_parser.h"
#include "ui_renderer.h"

// Calendar implementation
Calendar::Calendar() : mCurrentView(DayView), mCurrentDate(GetCurrentDate())
{
    // Initialize calendar with default settings
    mCategories["Work"] = ColorRGB(255, 0, 0);     // Red
    mCategories["Personal"] = ColorRGB(0, 255, 0); // Green
    mCategories["Retro"] = ColorRGB(0, 0, 255);    // Blue
}

Calendar::~Calendar()
{
    // Clean up
}

void Calendar::LoadFromPreferences()
{
    // Load calendar configuration and events from preferences
    // This would typically read from System Folder:Preferences

    // For now, just initialize with default categories
    InitializeDefaultCategories();
}

void Calendar::SaveToPreferences()
{
    // Save calendar configuration and events to preferences
    // This would typically write to System Folder:Preferences
}

void Calendar::AddEvent(const Event& event)
{
    mEvents.push_back(event);
    SortEvents();
}

void Calendar::RemoveEvent(const Event& event)
{
    // Find and remove the event
    auto it = std::find(mEvents.begin(), mEvents.end(), event);
    if (it != mEvents.end())
    {
        mEvents.erase(it);
    }
}

std::vector<Event> Calendar::GetEventsForDate(const Date& date) const
{
    std::vector<Event> events;

    for (const auto& event : mEvents)
    {
        if (event.IsOnDate(date))
        {
            events.push_back(event);
        }
    }

    return events;
}

std::vector<Event> Calendar::GetEventsForPeriod(const Date& startDate, const Date& endDate) const
{
    std::vector<Event> events;

    for (const auto& event : mEvents)
    {
        if (event.IntersectsPeriod(startDate, endDate))
        {
            events.push_back(event);
        }
    }

    return events;
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
    // In a real implementation, this would get the current system date
    return Date(2026, 8, 31); // Default to current date for demo
}

void Calendar::ProcessBackgroundTasks()
{
    // Handle any background tasks like reminders
    CheckForUpcomingReminders();
}

void Calendar::InitializeDefaultCategories()
{
    // Set up default color categories
    mCategories["Work"] = ColorRGB(255, 0, 0);     // Red
    mCategories["Personal"] = ColorRGB(0, 255, 0); // Green
    mCategories["Retro"] = ColorRGB(0, 0, 255);    // Blue
    mCategories["Vintage"] = ColorRGB(255, 255, 0); // Yellow
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