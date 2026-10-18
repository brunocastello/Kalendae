/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * Calendar Info: a movable modal dialog (DLOG/DITL 145) to rename a calendar
 * and change its colour.
 */

#ifndef CALENDAR_DIALOG_H
#define CALENDAR_DIALOG_H

#include <string>
#include "calendar.h"
#include "event_dialog.h"       // InspectorUpdateProc

// Edits the calendar `name`. On OK the rename (Calendar::RenameCategory, which
// rewrites the events) and the colour change are applied to `cal`, the final
// name is stored in *outName (if given), and true is returned. Cancel returns
// false and changes nothing.
bool ShowCalendarInfo(Calendar& cal, const std::string& name, InspectorUpdateProc updateProc,
                      std::string* outName = NULL);

#endif // CALENDAR_DIALOG_H
