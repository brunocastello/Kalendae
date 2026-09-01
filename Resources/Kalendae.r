/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * SIZE resource: declares the application's memory partition and
 * runtime behavior flags to the Process Manager.
 *
 * The partition must be generous: UIRenderer keeps a full-window color
 * GWorld alive for offscreen double-buffering. At 16-bit display depth
 * a ~550x450 window's pixel buffer alone is already ~470KB, which would
 * leave a 500KB partition with almost nothing for the C++ runtime, the
 * std::string/std::vector heap, or Toolbox bookkeeping.
 */

#include "Processes.r"

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
