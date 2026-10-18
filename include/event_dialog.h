/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * Event inspector: an Apple Calendar style popover (DLOG/DITL 128) shown
 * beside the clicked event / slot, driven by ModalDialog with a filter proc.
 */

#ifndef EVENT_DIALOG_H
#define EVENT_DIALOG_H

#include <Quickdraw.h>
#include <Events.h>
#include "calendar.h"
#include "event.h"

enum
{
    kInspectorCancelled = 0,
    kInspectorSaved = 1,
    kInspectorDeleted = 2
};

// Called for updateEvts of windows other than the popover while it is up.
typedef void (*InspectorUpdateProc)(EventRecord* event);

// Shows the popover beside anchorGlobal (global coordinates). inOut should be
// loaded from *Calendar::FindEvent(id) for edits so ID/UID/ExDates survive;
// it is only modified when kInspectorSaved is returned.
short ShowEventInspector(Event& inOut, bool isNew, const Rect& anchorGlobal,
                         const Calendar& calendar, InspectorUpdateProc updateProc);

#endif // EVENT_DIALOG_H
