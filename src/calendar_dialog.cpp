/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * Calendar Info dialog. See include/calendar_dialog.h and the DLOG/DITL 145
 * block in Resources/Kalendae.r (item numbers below must match it).
 */

#include "calendar_dialog.h"
#include <Quickdraw.h>
#include <Windows.h>
#include <Menus.h>
#include <Dialogs.h>
#include <TextEdit.h>
#include <Events.h>
#include <OSUtils.h>
#include <cstring>
#include <string>

enum
{
    kDialogID = 145,
    kInvalidAlertID = 140,
    kColorMenuID = 203
};

enum
{
    kOKItem = 1,
    kCancelItem = 2,
    kNameItem = 3,
    kColorItem = 4,
    kSwatchItem = 5
    // 6 and 7 are the "Name:" / "Color:" labels
};

// Colour popup: 1..7 the Apple palette, 8 Custom (enabled only while the
// colour is not a palette one), 9 separator, 10 Other...
static const int kPaletteCount = 7;
static const short kCustomItem = 8;
static const short kOtherItem = 10;

static const char* const kPaletteNames[kPaletteCount] = {
    "Red", "Orange", "Yellow", "Green", "Blue", "Purple", "Brown"
};
// Same values as Calendar::NextPaletteColor
static const unsigned char kPalette[kPaletteCount][3] = {
    {255, 59, 48}, {255, 149, 0}, {255, 204, 0}, {52, 199, 89},
    {0, 122, 255}, {175, 82, 222}, {162, 132, 94}
};

// Created once (POD statics; no global constructors on this toolchain).
static ModalFilterUPP sFilterUPP = NULL;
static UserItemUPP sSwatchUPP = NULL;
static InspectorUpdateProc sUpdateProc = NULL;
static ColorRGB sColor;             // the colour currently chosen (POD members only)
static unsigned char sCustom[3];    // the non-palette colour behind popup item 8

// ---------------------------------------------------------------- strings

static void ToPascal(const std::string& s, unsigned char* out)
{
    size_t len = s.size();
    if (len > 255) len = 255;
    out[0] = (unsigned char)len;
    if (len > 0)
        memcpy(&out[1], s.data(), len);
}

static ControlHandle GetItemControl(DialogPtr dlg, short item)
{
    short type;
    Handle h;
    Rect r;
    GetDialogItem(dlg, item, &type, &h, &r);
    return (ControlHandle)h;
}

static std::string Trim(const std::string& s)
{
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r'))
        b++;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r'))
        e--;
    return s.substr(b, e - b);
}

// ---------------------------------------------------------------- menu

static void DestroyColorMenu()
{
    MenuHandle menu = GetMenuHandle(kColorMenuID);
    if (menu)
    {
        DeleteMenu(kColorMenuID);
        DisposeMenu(menu);
    }
}

static void BuildColorMenu()
{
    DestroyColorMenu();     // a stale menu from a run that bailed out

    MenuHandle menu = NewMenu(kColorMenuID, "\p");
    if (!menu)
        return;
    for (int i = 0; i < kPaletteCount; i++)
    {
        AppendMenu(menu, "\pX");
        Str255 p;
        ToPascal(kPaletteNames[i], p);
        SetMenuItemText(menu, (short)(i + 1), p);
    }
    AppendMenu(menu, "\pCustom");
    AppendMenu(menu, "\p(-");
    AppendMenu(menu, "\pOther\311");
    InsertMenu(menu, -1);
}

// Palette index (0..6) of the colour, or -1.
static int PaletteIndexOf(const unsigned char* rgb)
{
    for (int i = 0; i < kPaletteCount; i++)
        if (kPalette[i][0] == rgb[0] && kPalette[i][1] == rgb[1] && kPalette[i][2] == rgb[2])
            return i;
    return -1;
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

// 16x16 framed swatch in the current colour.
static pascal void DrawSwatch(DialogPtr dlg, short item)
{
    short type;
    Handle h;
    Rect r;
    GetDialogItem(dlg, item, &type, &h, &r);

    RGBColor saved;
    GetForeColor(&saved);

    RGBColor fill = {(unsigned short)(sColor.r * 257), (unsigned short)(sColor.g * 257),
                     (unsigned short)(sColor.b * 257)};
    RGBColor black = {0, 0, 0};
    RGBForeColor(&fill);
    PaintRect(&r);
    RGBForeColor(&black);
    FrameRect(&r);
    RGBForeColor(&saved);
}

static void RedrawSwatch(DialogPtr dlg)
{
    short type;
    Handle h;
    Rect r;
    GetDialogItem(dlg, kSwatchItem, &type, &h, &r);
    SetPort(dlg);
    InvalRect(&r);
}

static pascal Boolean CalendarInfoFilter(DialogPtr dlg, EventRecord* event, short* itemHit)
{
    switch (event->what)
    {
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

// Centred on the main window (the front window when the dialog is made), or
// on the main screen if there is none; kept clear of the menu bar.
static void PlaceDialog(DialogPtr dlg, WindowPtr parent)
{
    short width = dlg->portRect.right - dlg->portRect.left;
    short height = dlg->portRect.bottom - dlg->portRect.top;

    Rect area = qd.screenBits.bounds;
    if (parent)
        area = (**((WindowPeek)parent)->contRgn).rgnBBox;

    short left = area.left + ((area.right - area.left) - width) / 2;
    short top = area.top + ((area.bottom - area.top) - height) / 2;

    Rect screen = qd.screenBits.bounds;
    if (left + width > screen.right - 4) left = screen.right - 4 - width;
    if (left < screen.left + 4) left = screen.left + 4;
    short minTop = screen.top + GetMBarHeight() + 19 + 4;     // 19 = title bar
    if (top + height > screen.bottom - 4) top = screen.bottom - 4 - height;
    if (top < minTop) top = minTop;

    MoveWindow(dlg, left, top, false);
}

static void ShowInvalid(DialogPtr dlg, const std::string& message)
{
    Str255 p;
    ToPascal(message, p);
    ParamText(p, "\p", "\p", "\p");
    StopAlert(kInvalidAlertID, NULL);
    SetPort(dlg);
    SelectDialogItemText(dlg, kNameItem, 0, 32767);
}

// Colour popup choice "Other...": runs the Color Picker. Returns true and
// updates sColor / the popup if the user picked a colour.
static bool ChooseOtherColor(DialogPtr dlg, ControlHandle popup)
{
    RGBColor in = {(unsigned short)(sColor.r * 257), (unsigned short)(sColor.g * 257),
                   (unsigned short)(sColor.b * 257)};
    RGBColor out = in;
    Point where;
    where.h = 0;
    where.v = 0;        // (0,0): the picker centres itself

    InitCursor();
    Str255 prompt;
    ToPascal("Choose a calendar color:", prompt);
    Boolean ok = GetColor(where, prompt, &in, &out);   // declared in Quickdraw.h
    SetPort(dlg);
    if (!ok)
        return false;

    unsigned char rgb[3] = { (unsigned char)(out.red >> 8), (unsigned char)(out.green >> 8),
                             (unsigned char)(out.blue >> 8) };
    sColor = ColorRGB(rgb[0], rgb[1], rgb[2]);
    int idx = PaletteIndexOf(rgb);
    MenuHandle menu = GetMenuHandle(kColorMenuID);
    if (idx >= 0)
    {
        SetControlValue(popup, (short)(idx + 1));
    }
    else
    {
        memcpy(sCustom, rgb, 3);
        if (menu)
            EnableItem(menu, kCustomItem);
        SetControlValue(popup, kCustomItem);
    }
    return true;
}

bool ShowCalendarInfo(Calendar& cal, const std::string& name, InspectorUpdateProc updateProc,
                      std::string* outName)
{
    const std::map<std::string, ColorRGB>& cats = cal.GetCategories();
    std::map<std::string, ColorRGB>::const_iterator found = cats.find(name);
    if (found == cats.end())
        return false;
    const std::string originalName = name;
    const ColorRGB originalColor = found->second;

    GrafPtr savedPort;
    GetPort(&savedPort);
    WindowPtr parent = FrontWindow();

    if (!sFilterUPP)
        sFilterUPP = NewModalFilterUPP(CalendarInfoFilter);
    if (!sSwatchUPP)
        sSwatchUPP = NewUserItemUPP(DrawSwatch);
    sUpdateProc = updateProc;
    sColor = originalColor;

    // The popup's menu must exist before the dialog's control is created.
    BuildColorMenu();
    MenuHandle menu = GetMenuHandle(kColorMenuID);

    DialogPtr dlg = GetNewDialog(kDialogID, NULL, (WindowPtr)-1L);
    if (!dlg || !menu)
    {
        if (dlg)
            DisposeDialog(dlg);
        DestroyColorMenu();
        SetPort(savedPort);
        return false;
    }
    SetPort(dlg);
    SetDialogTracksCursor(dlg, true);

    {
        short type;
        Handle h;
        Rect r;
        GetDialogItem(dlg, kSwatchItem, &type, &h, &r);
        SetDialogItem(dlg, kSwatchItem, userItem, (Handle)sSwatchUPP, &r);
    }

    // Name (clamped to the 255 chars an EditText takes; calendar names are <= 63)
    {
        short type;
        Handle h;
        Rect r;
        Str255 p;
        ToPascal(originalName, p);
        GetDialogItem(dlg, kNameItem, &type, &h, &r);
        SetDialogItemText(h, p);
    }

    ControlHandle popup = GetItemControl(dlg, kColorItem);
    sCustom[0] = originalColor.r;
    sCustom[1] = originalColor.g;
    sCustom[2] = originalColor.b;
    short previousValue;
    {
        unsigned char rgb[3] = { originalColor.r, originalColor.g, originalColor.b };
        int idx = PaletteIndexOf(rgb);
        if (idx >= 0)
        {
            previousValue = (short)(idx + 1);
            DisableItem(menu, kCustomItem);
        }
        else
        {
            previousValue = kCustomItem;
            EnableItem(menu, kCustomItem);
        }
    }
    SetControlValue(popup, previousValue);

    SetDialogDefaultItem(dlg, kOKItem);
    SetDialogCancelItem(dlg, kCancelItem);
    TEFromScrap();
    SelectDialogItemText(dlg, kNameItem, 0, 32767);

    PlaceDialog(dlg, parent);
    ShowWindow(dlg);
    SelectWindow(dlg);
    SetPort(dlg);

    bool result = false;
    std::string finalName = originalName;

    for (;;)
    {
        short item = 0;
        ModalDialog(sFilterUPP, &item);
        SetPort(dlg);

        if (item == kOKItem)
        {
            short type;
            Handle h;
            Rect r;
            Str255 p;
            GetDialogItem(dlg, kNameItem, &type, &h, &r);
            GetDialogItemText(h, p);
            std::string newName = Trim(std::string((const char*)&p[1], p[0]));
            if (newName.size() > 63)
                newName.resize(63);

            if (newName.empty())
            {
                ShowInvalid(dlg, "Please enter a name for the calendar.");
                continue;
            }
            if (newName != originalName && cal.GetCategories().find(newName) != cal.GetCategories().end())
            {
                ShowInvalid(dlg, "A calendar named \322" + newName + "\323 already exists.");
                continue;
            }

            if (newName != originalName && !cal.RenameCategory(originalName, newName))
            {
                ShowInvalid(dlg, "The calendar couldn\325t be renamed.");
                continue;
            }
            if (sColor.r != originalColor.r || sColor.g != originalColor.g ||
                sColor.b != originalColor.b)
                cal.SetCategoryColor(newName, sColor);

            finalName = newName;
            result = true;
            break;
        }
        else if (item == kCancelItem)
        {
            break;
        }
        else if (item == kColorItem)
        {
            short value = GetControlValue(popup);
            if (value == kOtherItem)
            {
                if (ChooseOtherColor(dlg, popup))
                {
                    previousValue = GetControlValue(popup);
                    RedrawSwatch(dlg);
                }
                else
                {
                    SetControlValue(popup, previousValue);
                }
            }
            else if (value >= 1 && value <= kPaletteCount)
            {
                sColor = ColorRGB(kPalette[value - 1][0], kPalette[value - 1][1],
                                  kPalette[value - 1][2]);
                previousValue = value;
                RedrawSwatch(dlg);
            }
            else if (value == kCustomItem)
            {
                sColor = ColorRGB(sCustom[0], sCustom[1], sCustom[2]);
                previousValue = value;
                RedrawSwatch(dlg);
            }
            else
            {
                SetControlValue(popup, previousValue);      // the separator
            }
        }
    }

    DisposeDialog(dlg);
    DestroyColorMenu();
    SetPort(savedPort);
    if (result && outName)
        *outName = finalName;
    return result;
}
