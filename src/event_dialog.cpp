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
// Whether the start/end time fields are shown (read by the filter for Tab).
static bool sTimeVisible = true;

// Everything ReadFields needs to know about how the popover was filled.
struct InspectorState
{
    std::vector<std::string> calendarNames;  // calendar popup item i+1
    short calendarInitial;
    short alertInitial;
    short alertCustomMinutes;                // alert popup item kAlertCount+1, if any
    short repeatInitial;
    bool hasCustomRepeat;
    std::string originalRule;
    // Text fields are capped at 255 chars; keep the full originals so an
    // untouched field is saved losslessly.
    std::string titleLoaded, titleFull;
    std::string locationLoaded, locationFull;
    std::string notesLoaded, notesFull;      // notesLoaded uses '\r', notesFull '\n'
};

struct MenuItemDef
{
    std::string text;
    bool separator;
};

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
    if (y < 1904 || y > 2100) return false;
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

static void AddItem(std::vector<MenuItemDef>& items, const std::string& text)
{
    MenuItemDef d;
    d.text = text;
    // A leading '-' would still be safe via SetMenuItemText, but looks like a
    // separator; use an en dash (MacRoman 0xD0) instead.
    if (!d.text.empty() && d.text[0] == '-')
        d.text[0] = (char)0xD0;
    d.separator = false;
    items.push_back(d);
}

static void AddSeparator(std::vector<MenuItemDef>& items)
{
    MenuItemDef d;
    d.separator = true;
    items.push_back(d);
}

static void BuildMenu(short id, const std::vector<MenuItemDef>& items)
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
        if (items[i].separator)
        {
            AppendMenu(menu, "\p(-");
            n++;
        }
        else
        {
            // Placeholder, then SetMenuItemText so metacharacters in
            // category names ("(", ";", "/") are taken literally.
            AppendMenu(menu, "\pX");
            n++;
            Str255 p;
            ToPascal(items[i].text, p);
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
    long ignored;
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

// Tab / shift-Tab through the visible edit fields, wrapping around.
static void MoveFocus(DialogPtr dlg, int direction)
{
    static const short kOrder[] = {
        kTitleItem, kLocationItem, kStartDateItem, kStartTimeItem,
        kEndDateItem, kEndTimeItem, kNotesItem
    };
    const int count = 7;

    short focus = ((DialogPeek)dlg)->editField + 1;
    int index = 0;
    for (int i = 0; i < count; i++)
        if (kOrder[i] == focus)
            index = i;

    for (int step = 0; step < count; step++)
    {
        index = (index + direction + count) % count;
        short item = kOrder[index];
        if (!sTimeVisible && (item == kStartTimeItem || item == kEndTimeItem))
            continue;
        SelectDialogItemText(dlg, item, 0, 32767);
        return;
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
                    case 'x': case 'X': DialogCut(dlg); ZeroScrap(); TEToScrap(); break;
                    case 'c': case 'C': DialogCopy(dlg); ZeroScrap(); TEToScrap(); break;
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

            if (ch == '\t')
            {
                MoveFocus(dlg, (event->modifiers & shiftKey) ? -1 : 1);
                *itemHit = 0;
                return true;
            }

            if (ch == 0x0D && ((DialogPeek)dlg)->editField + 1 == kNotesItem)
            {
                // In Notes, Return is a line break (Enter still saves).
                TEKey('\r', ((DialogPeek)dlg)->textH);
                *itemHit = 0;
                return true;
            }

            if (ch == 0x0D || ch == 0x03)
            {
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

    // The screen containing the anchor's centre (main screen as fallback).
    Rect screen = qd.screenBits.bounds;
    GDHandle mainDevice = GetMainDevice();
    GDHandle onDevice = mainDevice;
    Point centre;
    centre.h = (anchor.left + anchor.right) / 2;
    centre.v = (anchor.top + anchor.bottom) / 2;
    for (GDHandle gd = GetDeviceList(); gd; gd = GetNextDevice(gd))
    {
        Rect r = (**gd).gdRect;
        if (PtInRect(centre, &r))
        {
            screen = r;
            onDevice = gd;
            break;
        }
    }

    short left = anchor.right + 8;
    if (left + width > screen.right - 4)
        left = anchor.left - 8 - width;
    short top = anchor.top + ((anchor.bottom - anchor.top) - height) / 2;

    if (left + width > screen.right - 4) left = screen.right - 4 - width;
    if (left < screen.left + 4) left = screen.left + 4;
    short minTop = screen.top + 4;
    if (onDevice == mainDevice)
        minTop = screen.top + GetMBarHeight() + 4;
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

static void SetTimeFieldsVisible(DialogPtr dlg, bool visible)
{
    if (sTimeVisible == visible)
        return;
    sTimeVisible = visible;
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
        if (focus == kStartTimeItem)
            SelectDialogItemText(dlg, kStartDateItem, 0, 32767);
        else if (focus == kEndTimeItem)
            SelectDialogItemText(dlg, kEndDateItem, 0, 32767);
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
static bool ReadFields(DialogPtr dlg, bool isNew, Event& out, const InspectorState& st)
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
    if (title == st.titleLoaded)
        title = st.titleFull;
    if (title.empty())
        title = isNew ? "New Event" : out.GetTitle();

    out.SetTitle(title);
    std::string location = GetItemText(dlg, kLocationItem);
    out.SetLocation(location == st.locationLoaded ? st.locationFull : location);

    std::string notes = GetItemText(dlg, kNotesItem);
    if (notes == st.notesLoaded)
    {
        notes = st.notesFull;
    }
    else
    {
        for (size_t i = 0; i < notes.size(); i++)
            if (notes[i] == '\r')
                notes[i] = '\n';
    }
    out.SetDescription(notes);

    out.SetAllDay(allDay);
    out.SetStartTime(sd);
    out.SetEndTime(ed);
    if (!allDay)
    {
        out.SetStartMinute(sm);
        out.SetEndMinute(em);
    }

    // Popups only write back when the user changed them, so values the
    // popup can't represent exactly are never rewritten.
    short calIndex = GetControlValue(GetItemControl(dlg, kCalendarItem));
    if (calIndex != st.calendarInitial && calIndex >= 1 &&
        calIndex <= (short)st.calendarNames.size())
        out.SetCategory(st.calendarNames[calIndex - 1]);

    short alertIndex = GetControlValue(GetItemControl(dlg, kAlertItem));
    if (alertIndex != st.alertInitial)
    {
        if (alertIndex >= 1 && alertIndex <= kAlertCount && kAlertValues[alertIndex - 1] != -2)
            out.SetAlertMinutes(kAlertValues[alertIndex - 1]);
        else if (alertIndex == kAlertCount + 1)
            out.SetAlertMinutes(st.alertCustomMinutes);
    }

    short repeatIndex = GetControlValue(GetItemControl(dlg, kRepeatItem));
    if (repeatIndex != st.repeatInitial)
    {
        if (repeatIndex == 1)
            out.SetRecurrenceRule("");
        else if (repeatIndex >= 3 && repeatIndex <= 6)
            out.SetRecurrenceRule(kRepeatRules[repeatIndex - 1]);
        else if (repeatIndex == kRepeatCustomItem && st.hasCustomRepeat)
            out.SetRecurrenceRule(st.originalRule);
    }

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
    InspectorState st;
    std::vector<MenuItemDef> calendarItems;
    const std::map<std::string, ColorRGB>& cats = calendar.GetCategories();
    for (std::map<std::string, ColorRGB>::const_iterator it = cats.begin(); it != cats.end(); ++it)
        st.calendarNames.push_back(it->first);
    if (st.calendarNames.empty() && inOut.GetCategory().empty())
        st.calendarNames.push_back("Work");

    // Unknown category: append it so the popup can show (and keep) it.
    st.calendarInitial = 1;
    bool foundCategory = false;
    for (size_t i = 0; i < st.calendarNames.size(); i++)
        if (st.calendarNames[i] == inOut.GetCategory())
        {
            st.calendarInitial = (short)(i + 1);
            foundCategory = true;
        }
    if (!foundCategory && !inOut.GetCategory().empty())
    {
        st.calendarNames.push_back(inOut.GetCategory());
        st.calendarInitial = (short)st.calendarNames.size();
    }
    for (size_t i = 0; i < st.calendarNames.size(); i++)
        AddItem(calendarItems, st.calendarNames[i]);
    BuildMenu(kCalendarMenuID, calendarItems);

    std::vector<MenuItemDef> alertItems;
    AddItem(alertItems, "None");
    AddSeparator(alertItems);
    AddItem(alertItems, "At time of event");
    AddItem(alertItems, "5 minutes before");
    AddItem(alertItems, "15 minutes before");
    AddItem(alertItems, "30 minutes before");
    AddItem(alertItems, "1 hour before");
    AddItem(alertItems, "1 day before");

    st.alertInitial = 1;
    st.alertCustomMinutes = 0;
    short alertMinutes = inOut.GetAlertMinutes();
    if (alertMinutes >= 0)
    {
        st.alertInitial = 0;
        for (int i = 0; i < kAlertCount; i++)
            if (kAlertValues[i] == alertMinutes)
                st.alertInitial = (short)(i + 1);
        if (st.alertInitial == 0)
        {
            char buf[40];
            if (alertMinutes % 1440 == 0)
                snprintf(buf, sizeof(buf), "%d days before", alertMinutes / 1440);
            else if (alertMinutes % 60 == 0)
                snprintf(buf, sizeof(buf), "%d hours before", alertMinutes / 60);
            else
                snprintf(buf, sizeof(buf), "%d minutes before", alertMinutes);
            AddItem(alertItems, buf);
            st.alertCustomMinutes = alertMinutes;
            st.alertInitial = kAlertCount + 1;
        }
    }
    BuildMenu(kAlertMenuID, alertItems);

    st.originalRule = inOut.GetRecurrenceRule();
    std::string rule = NormalizeRule(st.originalRule);
    st.repeatInitial = 1;
    st.hasCustomRepeat = false;
    if (!rule.empty())
    {
        st.repeatInitial = 0;
        for (int i = 2; i < 6; i++)
            if (rule == kRepeatRules[i])
                st.repeatInitial = (short)(i + 1);
        if (st.repeatInitial == 0)
        {
            st.hasCustomRepeat = true;
            st.repeatInitial = kRepeatCustomItem;
        }
    }
    std::vector<MenuItemDef> repeatItems;
    AddItem(repeatItems, "None");
    AddSeparator(repeatItems);
    AddItem(repeatItems, "Every Day");
    AddItem(repeatItems, "Every Week");
    AddItem(repeatItems, "Every Month");
    AddItem(repeatItems, "Every Year");
    if (st.hasCustomRepeat)
        AddItem(repeatItems, "Custom");
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
    SetDialogTracksCursor(dlg, true);

    // Label column
    {
        short type;
        Handle h;
        Rect r;
        GetDialogItem(dlg, kLabelsItem, &type, &h, &r);
        SetDialogItem(dlg, kLabelsItem, userItem, (Handle)sLabelsUPP, &r);
    }

    // Fill the fields. The loaded (255-clamped) text is remembered so an
    // untouched field can be saved from the full original.
    Str255 clamp;
    st.titleFull = isNew ? std::string("New Event") : inOut.GetTitle();
    ToPascal(st.titleFull, clamp);
    st.titleLoaded = FromPascal(clamp);
    SetItemText(dlg, kTitleItem, st.titleFull);

    st.locationFull = inOut.GetLocation();
    ToPascal(st.locationFull, clamp);
    st.locationLoaded = FromPascal(clamp);
    SetItemText(dlg, kLocationItem, st.locationFull);

    st.notesFull = inOut.GetDescription();
    std::string notesCR = st.notesFull;
    for (size_t i = 0; i < notesCR.size(); i++)
        if (notesCR[i] == '\n')
            notesCR[i] = '\r';
    ToPascal(notesCR, clamp);
    st.notesLoaded = FromPascal(clamp);
    SetItemText(dlg, kNotesItem, notesCR);

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

    SetControlValue(GetItemControl(dlg, kCalendarItem), st.calendarInitial);
    SetControlValue(GetItemControl(dlg, kAlertItem), st.alertInitial);
    SetControlValue(GetItemControl(dlg, kRepeatItem), st.repeatInitial);

    if (isNew)
        HideDialogItem(dlg, kDeleteItem);

    sTimeVisible = true;
    SetTimeFieldsVisible(dlg, !allDay);

    SetDialogDefaultItem(dlg, kOKItem);
    SetDialogCancelItem(dlg, kCancelItem);
    TEFromScrap();

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
            if (ReadFields(dlg, isNew, edited, st))
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
            SetTimeFieldsVisible(dlg, GetControlValue(c) == 0);
        }
    }

    DisposeDialog(dlg);
    DestroyMenu(kCalendarMenuID);
    DestroyMenu(kAlertMenuID);
    DestroyMenu(kRepeatMenuID);
    SetPort(savedPort);
    return result;
}
