/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * SIZE resource: declares the application's memory partition and
 * runtime behavior flags to the Process Manager. Kept generous (4MB)
 * for headroom under the C++ runtime and std::string/std::vector/
 * std::map heap usage, not because of any offscreen buffer -- there
 * isn't one; UIRenderer draws straight into each window's own port.
 */

#include "Processes.r"
#include "Dialogs.r"

resource 'SIZE' (-1) {
    reserved,
    acceptSuspendResumeEvents,
    reserved,
    canBackground,
    doesActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreChildDiedEvents,
    is32BitCompatible,
    notHighLevelEventAware,
    onlyLocalHLEvents,
    notStationeryAware,
    dontUseTextEditServices,
    reserved,
    reserved,
    reserved,
    4096 * 1024,
    4096 * 1024
};

/*
 * Event inspector popover (src/event_dialog.cpp). Item numbers, in
 * declaration order, must match the k*Item enum in that file:
 *   1 OK   2 Cancel   3 Delete   4 Title   5 Location   6 all-day check
 *   7 start date   8 start time   9 end date   10 end time
 *   11 calendar popup   12 alert popup   13 repeat popup   14 Notes
 *   15 label column (user item; drawn right-aligned by the inspector)
 * The popup controls point at MENU ids 200/201/202, which are built at
 * runtime (categories are not known at compile time), so there are no
 * MENU resources. The window is created invisible; the inspector moves it
 * next to its anchor and then shows it.
 */
resource 'DLOG' (128) {
    {0, 0, 318, 310},
    altDBoxProc,
    invisible,
    noGoAway,
    0,
    128,
    "",
    0
};

resource 'DITL' (128) {
    {
        {284, 232, 304, 298},
        Button { enabled, "OK" };

        {284, 156, 304, 222},
        Button { enabled, "Cancel" };

        {284, 12, 304, 78},
        Button { enabled, "Delete" };

        {12, 8, 28, 302},
        EditText { enabled, "" };

        {40, 8, 56, 302},
        EditText { enabled, "" };

        {66, 84, 82, 200},
        CheckBox { enabled, "all-day" };

        {90, 84, 106, 172},
        EditText { enabled, "" };

        {90, 180, 106, 260},
        EditText { enabled, "" };

        {116, 84, 132, 172},
        EditText { enabled, "" };

        {116, 180, 132, 260},
        EditText { enabled, "" };

        {142, 84, 162, 260},
        Control { enabled, 129 };

        {168, 84, 188, 260},
        Control { enabled, 130 };

        {194, 84, 214, 260},
        Control { enabled, 131 };

        {224, 84, 272, 302},
        EditText { enabled, "" };

        {62, 0, 276, 82},
        UserItem { disabled };
    }
};

/*
 * Popup controls, as raw data: this toolchain's RIncludes have no 'CNTL'
 * type. Layout: rect {0,0,20,176}, value 0 (no title), visible, max 0,
 * min = MENU id (200/201/202), procID 1009 (popupMenuProc + fixed width),
 * refCon 0, title "".
 */
data 'CNTL' (129) {
    $"0000 0000 0014 00B0 0000 0100 0000 00C8 03F1 0000 0000 00"
};

data 'CNTL' (130) {
    $"0000 0000 0014 00B0 0000 0100 0000 00C9 03F1 0000 0000 00"
};

data 'CNTL' (131) {
    $"0000 0000 0014 00B0 0000 0100 0000 00CA 03F1 0000 0000 00"
};

/* "Invalid input" alert: ParamText(^0 = message) */
resource 'DITL' (140) {
    {
        {64, 258, 84, 324},
        Button { enabled, "OK" };

        {12, 70, 54, 324},
        StaticText { disabled, "^0" };
    }
};

resource 'ALRT' (140) {
    {80, 100, 164, 440},
    140,
    {
        OK, visible, silent,
        OK, visible, silent,
        OK, visible, silent,
        OK, visible, silent
    },
    alertPositionMainScreen
};

/* Delete confirmation: ParamText(^0 = title). Delete (default) / Cancel. */
resource 'DITL' (129) {
    {
        {86, 258, 106, 324},
        Button { enabled, "Delete" };

        {86, 180, 106, 246},
        Button { enabled, "Cancel" };

        {12, 70, 76, 324},
        StaticText { disabled, "Are you sure you want to delete \322^0\323?" };
    }
};

resource 'ALRT' (129) {
    {80, 100, 196, 440},
    129,
    {
        OK, visible, silent,
        OK, visible, silent,
        OK, visible, silent,
        OK, visible, silent
    },
    alertPositionMainScreen
};

/* Recurring delete: Only This Event (default) / Cancel / Delete All */
resource 'DITL' (130) {
    {
        {92, 252, 112, 440},
        Button { enabled, "Delete Only This Event" };

        {92, 166, 112, 244},
        Button { enabled, "Cancel" };

        {92, 70, 112, 158},
        Button { enabled, "Delete All" };

        {12, 70, 76, 440},
        StaticText { disabled, "Do you want to delete all occurrences of \322^0\323, or only this occurrence?" };
    }
};

resource 'ALRT' (130) {
    {80, 60, 206, 510},
    130,
    {
        OK, visible, silent,
        OK, visible, silent,
        OK, visible, silent,
        OK, visible, silent
    },
    alertPositionMainScreen
};

/* Quit with unsaved changes that couldn't be written: ^0 = error number.
   Quit Anyway (default) / Cancel. */
resource 'DITL' (141) {
    {
        {86, 238, 106, 324},
        Button { enabled, "Quit Anyway" };

        {86, 160, 106, 226},
        Button { enabled, "Cancel" };

        {12, 70, 76, 324},
        StaticText { disabled, "Your changes couldn\325t be saved (error ^0)." };
    }
};

resource 'ALRT' (141) {
    {80, 100, 196, 440},
    141,
    {
        OK, visible, silent,
        OK, visible, silent,
        OK, visible, silent,
        OK, visible, silent
    },
    alertPositionMainScreen
};

/* Plain message: ParamText(^0 = text) */
resource 'DITL' (142) {
    {
        {112, 258, 132, 324},
        Button { enabled, "OK" };

        {12, 70, 100, 324},
        StaticText { disabled, "^0" };
    }
};

resource 'ALRT' (142) {
    {80, 100, 224, 440},
    142,
    {
        OK, visible, silent,
        OK, visible, silent,
        OK, visible, silent,
        OK, visible, silent
    },
    alertPositionMainScreen
};

/* Note alert: ParamText(^0 = text). Import results. */
resource 'DITL' (143) {
    {
        {112, 258, 132, 324},
        Button { enabled, "OK" };

        {12, 70, 100, 324},
        StaticText { disabled, "^0" };
    }
};

resource 'ALRT' (143) {
    {80, 100, 224, 440},
    143,
    {
        OK, visible, silent,
        OK, visible, silent,
        OK, visible, silent,
        OK, visible, silent
    },
    alertPositionMainScreen
};

/*
 * Calendar Info dialog (src/calendar_dialog.cpp). Item numbers, in
 * declaration order, must match the k*Item enum in that file:
 *   1 OK   2 Cancel   3 name   4 colour popup   5 colour swatch (user item)
 *   6 "Name:"   7 "Color:"
 * The popup points at MENU id 203, built at runtime.
 */
resource 'DLOG' (145) {
    {0, 0, 102, 300},
    movableDBoxProc,
    invisible,
    noGoAway,
    0,
    145,
    "Calendar Info",
    0
};

resource 'DITL' (145) {
    {
        {72, 226, 92, 292},
        Button { enabled, "OK" };

        {72, 150, 92, 216},
        Button { enabled, "Cancel" };

        {14, 72, 30, 292},
        EditText { enabled, "" };

        {42, 72, 62, 232},
        Control { enabled, 132 };

        {44, 244, 60, 260},
        UserItem { disabled };

        {14, 12, 30, 68},
        StaticText { disabled, "Name:" };

        {44, 12, 60, 68},
        StaticText { disabled, "Color:" };
    }
};

/* Colour popup, same layout as 'CNTL' 129..131 but menu id 203 (0x00CB). */
data 'CNTL' (132) {
    $"0000 0000 0014 00B0 0000 0100 0000 00CB 03F1 0000 0000 00"
};

/* Delete calendar confirmation: ParamText(^0 = name, ^1 = event count text).
   Delete (default) / Cancel. */
resource 'DITL' (144) {
    {
        {86, 258, 106, 324},
        Button { enabled, "Delete" };

        {86, 180, 106, 246},
        Button { enabled, "Cancel" };

        {12, 70, 76, 324},
        StaticText { disabled, "Are you sure you want to delete the calendar \322^0\323? ^1" };
    }
};

resource 'ALRT' (144) {
    {80, 100, 196, 440},
    144,
    {
        OK, visible, silent,
        OK, visible, silent,
        OK, visible, silent,
        OK, visible, silent
    },
    alertPositionMainScreen
};
