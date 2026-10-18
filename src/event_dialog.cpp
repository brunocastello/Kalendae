/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * Event inspector popover. See include/event_dialog.h and the DLOG/DITL 128
 * block in Resources/Kalendae.r (item numbers below must match it).
 */

#include "event_dialog.h"
#include <Quickdraw.h>
#include <Windows.h>
#include <Menus.h>
#include <Controls.h>
#include <Dialogs.h>
#include <TextEdit.h>
#include <Events.h>
#include <OSUtils.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

enum
{
    kDialogID = 128,
    kInvalidAlertID = 140,

    kCalendarMenuID = 200,
    kAlertMenuID = 201,
    kRepeatMenuID = 202
};

enum
{
    kOKItem = 1,
    kCancelItem = 2,
    kDeleteItem = 3,
    kTitleItem = 4,
    kLocationItem = 5,
    kAllDayItem = 6,
    kStartDateItem = 7,
    kStartTimeItem = 8,
    kEndDateItem = 9,
    kEndTimeItem = 10,
    kCalendarItem = 11,
    kAlertItem = 12,
    kRepeatItem = 13,
    kNotesItem = 14,
    kLabelsItem = 15
};

// Alert popup: menu item i (1-based) maps to kAlertValues[i - 1]; -2 marks
// the disabled separator. Values are minutes before start, -1 = none.
static const short kAlertValues[] = { -1, -2, 0, 5, 15, 30, 60, 1440 };
static const int kAlertCount = 8;

// Repeat popup items: None, -, Day, Week, Month, Year, [Custom]
static const char* const kRepeatRules[] = {
    "", "", "FREQ=DAILY", "FREQ=WEEKLY", "FREQ=MONTHLY", "FREQ=YEARLY"
};
static const int kRepeatCustomItem = 7;

// Created once (POD statics; no global constructors on this toolchain).
static ModalFilterUPP sFilterUPP = NULL;
static UserItemUPP sLabelsUPP = NULL;
static InspectorUpdateProc sUpdateProc = NULL;

// ---------------------------------------------------------------- strings

static void ToPascal(const std::string& s, unsigned char* out)
{
    size_t len = s.size();
    if (len > 255) len = 255;
    out[0] = (unsigned char)len;
    if (len > 0)
        memcpy(&out[1], s.data(), len);
}

static std::string FromPascal(const unsigned char* p)
{
    return std::string((const char*)&p[1], p[0]);
}

static void SetItemText(DialogPtr dlg, short item, const std::string& text)
{
    short type;
    Handle h;
    Rect r;
    Str255 p;
    ToPascal(text, p);
    GetDialogItem(dlg, item, &type, &h, &r);
    SetDialogItemText(h, p);
}

static std::string GetItemText(DialogPtr dlg, short item)
{
    short type;
    Handle h;
    Rect r;
    Str255 p;
    GetDialogItem(dlg, item, &type, &h, &r);
    GetDialogItemText(h, p);
    return FromPascal(p);
}

static ControlHandle GetItemControl(DialogPtr dlg, short item)
{
    short type;
    Handle h;
    Rect r;
    GetDialogItem(dlg, item, &type, &h, &r);
    return (ControlHandle)h;
}

// ------------------------------------------------------- date / time text

static std::string FormatDate(const Date& d)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "%d/%d/%d", d.month, d.day, d.year);
    return buf;
}

static std::string FormatTime(short minute)
{
    if (minute < 0) minute = 0;
    minute = (short)(minute % 1440);
    int h = minute / 60;
    int h12 = h % 12;
    if (h12 == 0) h12 = 12;
    char buf[24];
    snprintf(buf, sizeof(buf), "%d:%02d %s", h12, minute % 60, h < 12 ? "AM" : "PM");
    return buf;
}

static bool IsDigit(char c) { return c >= '0' && c <= '9'; }

// Reads a run of digits at s[pos]; fails if there are none or more than 5.
static bool ReadNumber(const std::string& s, size_t& pos, int* value, int* digits)
{
    int v = 0;
    int n = 0;
    while (pos < s.size() && IsDigit(s[pos]))
    {
        if (n >= 5)
            return false;
        v = v * 10 + (s[pos] - '0');
        n++;
        pos++;
    }
    if (n == 0)
        return false;
    *value = v;
    if (digits)
        *digits = n;
    return true;
}

static void SkipSpaces(const std::string& s, size_t& pos)
{
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t'))
        pos++;
}

// M/D/YYYY or M/D/YY (-> 20YY); '-' and '.' also accepted as separators.
static bool ParseDate(const std::string& s, Date* out)
{
    size_t pos = 0;
    int m, d, y, yDigits;
    SkipSpaces(s, pos);
    if (!ReadNumber(s, pos, &m, NULL)) return false;
    if (pos >= s.size() || (s[pos] != '/' && s[pos] != '-' && s[pos] != '.')) return false;
    pos++;
    if (!ReadNumber(s, pos, &d, NULL)) return false;
    if (pos >= s.size() || (s[pos] != '/' && s[pos] != '-' && s[pos] != '.')) return false;
    pos++;
    if (!ReadNumber(s, pos, &y, &yDigits)) return false;
    SkipSpaces(s, pos);
    if (pos != s.size()) return false;

    if (yDigits <= 2) y += 2000;
    else if (yDigits != 4) return false;
    if (y < 1904 || y > 9999) return false;
    if (m < 1 || m > 12) return false;
    if (d < 1 || d > Date::DaysInMonth(y, m)) return false;

    *out = Date(y, m, d);
    return true;
}

// "H", "H:MM", "H AM", "H:MM PM" (case-insensitive) or 24-hour "HH:MM".
static bool ParseTime(const std::string& s, short* minute)
{
    size_t pos = 0;
    int h, mm = 0;
    SkipSpaces(s, pos);
    if (!ReadNumber(s, pos, &h, NULL)) return false;
    if (pos < s.size() && s[pos] == ':')
    {
        pos++;
        int digits;
        if (!ReadNumber(s, pos, &mm, &digits) || digits != 2) return false;
    }
    SkipSpaces(s, pos);

    int suffix = 0; // 0 none, 1 AM, 2 PM
    if (pos < s.size())
    {
        char c = s[pos];
        if (c == 'a' || c == 'A') suffix = 1;
        else if (c == 'p' || c == 'P') suffix = 2;
        else return false;
        pos++;
        if (pos < s.size() && (s[pos] == 'm' || s[pos] == 'M')) pos++;
        SkipSpaces(s, pos);
        if (pos != s.size()) return false;
    }

    if (mm > 59) return false;
    if (suffix != 0)
    {
        if (h < 1 || h > 12) return false;
        if (h == 12) h = 0;
        if (suffix == 2) h += 12;
    }
    else if (h > 23)
    {
        return false;
    }

    *minute = (short)(h * 60 + mm);
    return true;
}

// -------------------------------------------------------------- menus

static void BuildMenu(short id, const std::vector<std::string>& items)
{
    // A stale menu with this id (a previous run that bailed out) is replaced.
    MenuHandle old = GetMenuHandle(id);
    if (old)
    {
        DeleteMenu(id);
        DisposeMenu(old);
    }

    MenuHandle menu = NewMenu(id, "\p");
    if (!menu)
        return;

    short n = 0;
    for (size_t i = 0; i < items.size(); i++)
    {
        if (items[i] == "-")
        {
            AppendMenu(menu, "\p(-");
            n++;
        }
        else
        {
            // Placeholder, then SetMenuItemText so metacharacters in
            // category names ("-", "(", ";", "/") are taken literally.
            AppendMenu(menu, "\pX");
            n++;
            Str255 p;
            ToPascal(items[i], p);
            SetMenuItemText(menu, n, p);
        }
    }
    InsertMenu(menu, -1);
}

static void DestroyMenu(short id)
{
    MenuHandle menu = GetMenuHandle(id);
    if (menu)
    {
        DeleteMenu(id);
        DisposeMenu(menu);
    }
}

// ----------------------------------------------------------- dialog bits

static void FlashItem(DialogPtr dlg, short item)
{
    ControlHandle c = GetItemControl(dlg, item);
    if (!c)
        return;
    unsigned long ignored;
    HiliteControl(c, 1);
    Delay(8, &ignored);
    HiliteControl(c, 0);
}

// Right-aligned labels in the left column, baselines taken from the rows
// they annotate so layout lives only in the DITL.
static pascal void DrawLabels(DialogPtr dlg, short)
{
    struct LabelDef { short rowItem; const char* text; bool topAligned; };
    static const LabelDef kLabels[] = {
        { kStartDateItem, "starts:", false },
        { kEndDateItem, "ends:", false },
        { kCalendarItem, "calendar:", false },
        { kAlertItem, "alert:", false },
        { kRepeatItem, "repeat:", false },
        { kNotesItem, "notes:", true }
    };

    short type;
    Handle h;
    Rect labelRect;
    GetDialogItem(dlg, kLabelsItem, &type, &h, &labelRect);

    FontInfo fi;
    GetFontInfo(&fi);

    for (size_t i = 0; i < sizeof(kLabels) / sizeof(kLabels[0]); i++)
    {
        Rect row;
        GetDialogItem(dlg, kLabels[i].rowItem, &type, &h, &row);

        short baseline;
        if (kLabels[i].topAligned)
            baseline = row.top + fi.ascent;
        else
            baseline = row.top + ((row.bottom - row.top) - (fi.ascent + fi.descent)) / 2 + fi.ascent;

        Str255 p;
        ToPascal(kLabels[i].text, p);
        MoveTo(labelRect.right - 6 - StringWidth(p), baseline);
        DrawString(p);
    }
}

static pascal Boolean InspectorFilter(DialogPtr dlg, EventRecord* event, short* itemHit)
{
    switch (event->what)
    {
        case mouseDown:
        {
            // A click anywhere outside the popover saves and closes it, and
            // is consumed so it doesn't also hit the window underneath.
            WindowPtr w = NULL;
            short part = FindWindow(event->where, &w);
            if (part == inMenuBar || part == inDesk || w != (WindowPtr)dlg)
            {
                *itemHit = kOKItem;
                return true;
            }
            return false;
        }

        case keyDown:
        case autoKey:
        {
            char ch = (char)(event->message & charCodeMask);

            if (event->modifiers & cmdKey)
            {
                switch (ch)
                {
                    case '.':
                        FlashItem(dlg, kCancelItem);
                        *itemHit = kCancelItem;
                        return true;
                    case 'x': case 'X': DialogCut(dlg); break;
                    case 'c': case 'C': DialogCopy(dlg); break;
                    case 'v': case 'V': DialogPaste(dlg); break;
                    default: break;
                }
                // Other command keys are swallowed (no menu actions here).
                *itemHit = 0;
                return true;
            }

            if (ch == 0x1B)
            {
                FlashItem(dlg, kCancelItem);
                *itemHit = kCancelItem;
                return true;
            }

            if (ch == 0x0D || ch == 0x03)
            {
                // In Notes, Return is a line break.
                if (((DialogPeek)dlg)->editField + 1 == kNotesItem)
                    return false;
                FlashItem(dlg, kOKItem);
                *itemHit = kOKItem;
                return true;
            }
            return false;
        }

        case updateEvt:
        {
            WindowPtr w = (WindowPtr)event->message;
            if (w == (WindowPtr)dlg)
                return false;

            GrafPtr saved;
            GetPort(&saved);
            if (sUpdateProc)
            {
                sUpdateProc(event);
            }
            else
            {
                SetPort(w);
                BeginUpdate(w);
                EndUpdate(w);
            }
            SetPort(saved);
            *itemHit = 0;
            return true;
        }
    }
    return false;
}

static void PlaceDialog(DialogPtr dlg, const Rect& anchor)
{
    short width = dlg->portRect.right - dlg->portRect.left;
    short height = dlg->portRect.bottom - dlg->portRect.top;
    Rect screen = qd.screenBits.bounds;

    short left = anchor.right + 8;
    if (left + width > screen.right - 4)
        left = anchor.left - 8 - width;
    short top = anchor.top + ((anchor.bottom - anchor.top) - height) / 2;

    if (left + width > screen.right - 4) left = screen.right - 4 - width;
    if (left < screen.left + 4) left = screen.left + 4;
    short minTop = GetMBarHeight() + 4;
    if (top + height > screen.bottom - 4) top = screen.bottom - 4 - height;
    if (top < minTop) top = minTop;

    MoveWindow(dlg, left, top, false);
}

static void ShowInvalid(DialogPtr dlg, const char* message, short fieldItem)
{
    Str255 p;
    ToPascal(message, p);
    ParamText(p, "\p", "\p", "\p");
    StopAlert(kInvalidAlertID, NULL);
    SetPort(dlg);
    SelectDialogItemText(dlg, fieldItem, 0, 32767);
}

static void SetTimeFieldsVisible(DialogPtr dlg, bool visible, bool* state)
{
    if (*state == visible)
        return;
    *state = visible;
    if (visible)
    {
        ShowDialogItem(dlg, kStartTimeItem);
        ShowDialogItem(dlg, kEndTimeItem);
    }
    else
    {
        short focus = ((DialogPeek)dlg)->editField + 1;
        HideDialogItem(dlg, kStartTimeItem);
        HideDialogItem(dlg, kEndTimeItem);
        if (focus == kStartTimeItem || focus == kEndTimeItem)
            SelectDialogItemText(dlg, kStartDateItem, 0, 32767);
    }
}

static std::string NormalizeRule(const std::string& rule)
{
    std::string r = rule;
    if (r.compare(0, 6, "RRULE:") == 0)
        r = r.substr(6);
    for (size_t i = 0; i < r.size(); i++)
        if (r[i] >= 'a' && r[i] <= 'z')
            r[i] = (char)(r[i] - 'a' + 'A');
    return r;
}

// Validates every field and writes them into `out` (a copy of the incoming
// event). On failure shows the alert, selects the field and returns false.
static bool ReadFields(DialogPtr dlg, bool isNew, Event& out,
                       const std::vector<std::string>& calendarNames,
                       bool hasCustomRepeat, const std::string& originalRule)
{
    bool allDay = GetControlValue(GetItemControl(dlg, kAllDayItem)) != 0;

    Date sd, ed;
    short sm = out.GetStartMinute(), em = out.GetEndMinute();

    if (!ParseDate(GetItemText(dlg, kStartDateItem), &sd))
    {
        ShowInvalid(dlg, "The start date is not valid.", kStartDateItem);
        return false;
    }
    if (!allDay && !ParseTime(GetItemText(dlg, kStartTimeItem), &sm))
    {
        ShowInvalid(dlg, "The start time is not valid.", kStartTimeItem);
        return false;
    }
    if (!ParseDate(GetItemText(dlg, kEndDateItem), &ed))
    {
        ShowInvalid(dlg, "The end date is not valid.", kEndDateItem);
        return false;
    }
    if (!allDay && !ParseTime(GetItemText(dlg, kEndTimeItem), &em))
    {
        ShowInvalid(dlg, "The end time is not valid.", kEndTimeItem);
        return false;
    }

    if (allDay)
    {
        if (ed < sd)
            ed = sd;
    }
    else
    {
        long startKey = sd.ToSerial() * 1440L + sm;
        long endKey = ed.ToSerial() * 1440L + em;
        if (endKey < startKey)
            endKey = startKey + 60;
        long endDay = endKey / 1440L;
        ed = Date::FromSerial(endDay);
        em = (short)(endKey - endDay * 1440L);
        // An end at exactly midnight is stored as 24:00 of the previous day.
        if (em == 0 && ed > sd)
        {
            ed = ed.AddDays(-1);
            em = 1440;
        }
    }

    std::string title = GetItemText(dlg, kTitleItem);
    if (title.empty())
        title = isNew ? "New Event" : out.GetTitle();

    out.SetTitle(title);
    out.SetLocation(GetItemText(dlg, kLocationItem));

    std::string notes = GetItemText(dlg, kNotesItem);
    for (size_t i = 0; i < notes.size(); i++)
        if (notes[i] == '\r')
            notes[i] = '\n';
    out.SetDescription(notes);

    out.SetAllDay(allDay);
    out.SetStartTime(sd);
    out.SetEndTime(ed);
    if (!allDay)
    {
        out.SetStartMinute(sm);
        out.SetEndMinute(em);
    }

    short calIndex = GetControlValue(GetItemControl(dlg, kCalendarItem));
    if (calIndex >= 1 && calIndex <= (short)calendarNames.size())
        out.SetCategory(calendarNames[calIndex - 1]);

    short alertIndex = GetControlValue(GetItemControl(dlg, kAlertItem));
    if (alertIndex >= 1 && alertIndex <= kAlertCount && kAlertValues[alertIndex - 1] != -2)
        out.SetAlertMinutes(kAlertValues[alertIndex - 1]);

    short repeatIndex = GetControlValue(GetItemControl(dlg, kRepeatItem));
    if (repeatIndex == 1)
        out.SetRecurrenceRule("");
    else if (repeatIndex >= 3 && repeatIndex <= 6)
        out.SetRecurrenceRule(kRepeatRules[repeatIndex - 1]);
    else if (repeatIndex == kRepeatCustomItem && hasCustomRepeat)
        out.SetRecurrenceRule(originalRule);

    return true;
}

short ShowEventInspector(Event& inOut, bool isNew, const Rect& anchorGlobal,
                         const Calendar& calendar, InspectorUpdateProc updateProc)
{
    GrafPtr savedPort;
    GetPort(&savedPort);

    if (!sFilterUPP)
        sFilterUPP = NewModalFilterUPP(InspectorFilter);
    if (!sLabelsUPP)
        sLabelsUPP = NewUserItemUPP(DrawLabels);
    sUpdateProc = updateProc;

    // Popup menus must exist before the dialog's controls are created.
    std::vector<std::string> calendarNames;
    const std::map<std::string, ColorRGB>& cats = calendar.GetCategories();
    for (std::map<std::string, ColorRGB>::const_iterator it = cats.begin(); it != cats.end(); ++it)
        calendarNames.push_back(it->first);
    if (calendarNames.empty())
        calendarNames.push_back(inOut.GetCategory().empty() ? "Work" : inOut.GetCategory());
    BuildMenu(kCalendarMenuID, calendarNames);

    std::vector<std::string> alertItems;
    alertItems.push_back("None");
    alertItems.push_back("-");
    alertItems.push_back("At time of event");
    alertItems.push_back("5 minutes before");
    alertItems.push_back("15 minutes before");
    alertItems.push_back("30 minutes before");
    alertItems.push_back("1 hour before");
    alertItems.push_back("1 day before");
    BuildMenu(kAlertMenuID, alertItems);

    std::string originalRule = inOut.GetRecurrenceRule();
    std::string rule = NormalizeRule(originalRule);
    int repeatIndex = 1;
    bool hasCustomRepeat = false;
    if (!rule.empty())
    {
        repeatIndex = 0;
        for (int i = 2; i < 6; i++)
            if (rule == kRepeatRules[i])
                repeatIndex = i + 1;
        if (repeatIndex == 0)
        {
            hasCustomRepeat = true;
            repeatIndex = kRepeatCustomItem;
        }
    }
    std::vector<std::string> repeatItems;
    repeatItems.push_back("None");
    repeatItems.push_back("-");
    repeatItems.push_back("Every Day");
    repeatItems.push_back("Every Week");
    repeatItems.push_back("Every Month");
    repeatItems.push_back("Every Year");
    if (hasCustomRepeat)
        repeatItems.push_back("Custom");
    BuildMenu(kRepeatMenuID, repeatItems);

    short result = kInspectorCancelled;

    DialogPtr dlg = GetNewDialog(kDialogID, NULL, (WindowPtr)-1L);
    if (!dlg)
    {
        DestroyMenu(kCalendarMenuID);
        DestroyMenu(kAlertMenuID);
        DestroyMenu(kRepeatMenuID);
        SetPort(savedPort);
        return result;
    }
    SetPort(dlg);

    // Label column
    {
        short type;
        Handle h;
        Rect r;
        GetDialogItem(dlg, kLabelsItem, &type, &h, &r);
        SetDialogItem(dlg, kLabelsItem, userItem, (Handle)sLabelsUPP, &r);
    }

    // Fill the fields
    SetItemText(dlg, kTitleItem, isNew ? std::string("New Event") : inOut.GetTitle());
    SetItemText(dlg, kLocationItem, inOut.GetLocation());

    std::string notes = inOut.GetDescription();
    for (size_t i = 0; i < notes.size(); i++)
        if (notes[i] == '\n')
            notes[i] = '\r';
    SetItemText(dlg, kNotesItem, notes);

    bool allDay = inOut.IsAllDay();
    SetControlValue(GetItemControl(dlg, kAllDayItem), allDay ? 1 : 0);

    Date sd = inOut.GetStartTime();
    Date ed = inOut.GetEndTime();
    short sm = inOut.GetStartMinute();
    short em = inOut.GetEndMinute();
    if (!allDay && em >= 1440)
    {
        ed = ed.AddDays(1);
        em = 0;
    }
    SetItemText(dlg, kStartDateItem, FormatDate(sd));
    SetItemText(dlg, kStartTimeItem, FormatTime(sm));
    SetItemText(dlg, kEndDateItem, FormatDate(ed));
    SetItemText(dlg, kEndTimeItem, FormatTime(em));

    short calIndex = 1;
    for (size_t i = 0; i < calendarNames.size(); i++)
        if (calendarNames[i] == inOut.GetCategory())
            calIndex = (short)(i + 1);
    SetControlValue(GetItemControl(dlg, kCalendarItem), calIndex);

    short alertIndex = 1;
    short alertMinutes = inOut.GetAlertMinutes();
    if (alertMinutes >= 0)
    {
        for (int i = 0; i < kAlertCount; i++)
            if (kAlertValues[i] >= 0 && kAlertValues[i] <= alertMinutes)
                alertIndex = (short)(i + 1);
    }
    SetControlValue(GetItemControl(dlg, kAlertItem), alertIndex);
    SetControlValue(GetItemControl(dlg, kRepeatItem), (short)repeatIndex);

    if (isNew)
        HideDialogItem(dlg, kDeleteItem);

    bool timeVisible = true;
    SetTimeFieldsVisible(dlg, !allDay, &timeVisible);

    SetDialogDefaultItem(dlg, kOKItem);
    SetDialogCancelItem(dlg, kCancelItem);

    if (isNew)
        SelectDialogItemText(dlg, kTitleItem, 0, 32767);

    PlaceDialog(dlg, anchorGlobal);
    ShowWindow(dlg);
    SelectWindow(dlg);
    SetPort(dlg);

    for (;;)
    {
        short item = 0;
        ModalDialog(sFilterUPP, &item);
        SetPort(dlg);

        if (item == kOKItem)
        {
            Event edited = inOut;
            if (ReadFields(dlg, isNew, edited, calendarNames, hasCustomRepeat, originalRule))
            {
                inOut = edited;
                result = kInspectorSaved;
                break;
            }
        }
        else if (item == kCancelItem)
        {
            result = kInspectorCancelled;
            break;
        }
        else if (item == kDeleteItem)
        {
            result = kInspectorDeleted;
            break;
        }
        else if (item == kAllDayItem)
        {
            ControlHandle c = GetItemControl(dlg, kAllDayItem);
            SetControlValue(c, GetControlValue(c) ? 0 : 1);
            SetTimeFieldsVisible(dlg, GetControlValue(c) == 0, &timeVisible);
        }
    }

    DisposeDialog(dlg);
    DestroyMenu(kCalendarMenuID);
    DestroyMenu(kAlertMenuID);
    DestroyMenu(kRepeatMenuID);
    SetPort(savedPort);
    return result;
}
