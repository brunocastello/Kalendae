/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * SIZE resource: declares the application's memory partition and
 * runtime behavior flags to the Process Manager.
 */

#include "Types.r"
#include "SysTypes.r"
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
    500 * 1024,
    500 * 1024
};
