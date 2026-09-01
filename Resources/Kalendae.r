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
 * New/Edit Event dialog. Item numbers (1-indexed, in declaration order):
 *   1  OK button
 *   2  Cancel button
 *   3  "Title:" label       4  Title edit field
 *   5  "Location:" label    6  Location edit field
 *   7  "Category:" label
 *   8  Work radio  9  Personal radio  10  Retro radio  11  Vintage radio
 * Categories are hardcoded here rather than generated from
 * Calendar::GetCategories() since DITL items are fixed at compile time;
 * see the Roadmap in CLAUDE.md for revisiting this once categories are
 * user-editable.
 */
resource 'DLOG' (128) {
    {0, 0, 220, 360},
    dBoxProc,
    visible,
    noGoAway,
    0,
    128,
    "New Event",
    centerMainScreen
};

resource 'DITL' (128) {
    {
        {180, 260, 205, 340},
        Button { enabled, "OK" };

        {180, 170, 205, 250},
        Button { enabled, "Cancel" };

        {10, 10, 26, 80},
        StaticText { disabled, "Title:" };

        {10, 85, 26, 345},
        EditText { enabled, "" };

        {36, 10, 52, 80},
        StaticText { disabled, "Location:" };

        {36, 85, 52, 345},
        EditText { enabled, "" };

        {62, 10, 78, 100},
        StaticText { disabled, "Category:" };

        {84, 15, 100, 95},
        RadioButton { enabled, "Work" };

        {84, 100, 100, 190},
        RadioButton { enabled, "Personal" };

        {104, 15, 120, 95},
        RadioButton { enabled, "Retro" };

        {104, 100, 120, 190},
        RadioButton { enabled, "Vintage" };
    }
};
