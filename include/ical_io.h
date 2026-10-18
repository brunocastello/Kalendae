/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * Whole-file read and safe-save write via the File Manager
 */

#ifndef ICAL_IO_H
#define ICAL_IO_H

#include <string>
#include <Files.h>

// Reads the data fork into `out`. fnfErr (no such file) is passed through;
// memFullErr means the file is too big for the free memory (not read).
OSErr ReadFileToString(const FSSpec& spec, std::string& out);

// Writes `data` to a temporary sibling ("<name>~") and swaps it in with
// FSpExchangeFiles (or delete + rename), so a failure mid-write leaves the
// existing file intact.
OSErr WriteStringToFile(const FSSpec& spec, const std::string& data, OSType type, OSType creator);

#endif // ICAL_IO_H
