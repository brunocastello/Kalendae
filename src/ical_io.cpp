/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * Whole-file read and safe-save write via the File Manager
 */

#include <cstring>
#include <MacMemory.h>
#include "ical_io.h"

// Multiversal's FSp* routines take a non-const FSSpecPtr, so each function
// works on a local copy of its const FSSpec argument.
OSErr ReadFileToString(const FSSpec& specIn, std::string& out)
{
    FSSpec spec = specIn;
    out.clear();

    short refNum;
    OSErr err = FSpOpenDF(&spec, fsRdPerm, &refNum);
    if (err != noErr)
        return err;

    long eof = 0;
    err = GetEOF(refNum, &eof);
    // Leave room for the parsed copy of the data. Small files are always
    // fine in our partition, so only large ones are checked.
    const long kAlwaysFits = 256L * 1024;
    if (err == noErr && eof > kAlwaysFits && eof > MaxBlock() / 3)
        err = memFullErr;
    if (err == noErr && eof > 0)
    {
        out.resize((size_t)eof);
        long count = eof;
        err = FSRead(refNum, &count, &out[0]);
        if (err == eofErr)
            err = noErr;    // short read: keep what arrived
        if (err == noErr)
            out.resize((size_t)count);
        else
            out.clear();
    }

    OSErr closeErr = FSClose(refNum);
    return err != noErr ? err : closeErr;
}

// Creates `temp` and writes `data` into it in one FSWrite.
static OSErr WriteNewFile(const FSSpec& tempIn, const std::string& data, OSType type, OSType creator)
{
    FSSpec temp = tempIn;
    OSErr err = FSpCreate(&temp, creator, type, smSystemScript);
    if (err != noErr)
        return err;

    short refNum;
    err = FSpOpenDF(&temp, fsRdWrPerm, &refNum);
    if (err != noErr)
    {
        FSpDelete(&temp);
        return err;
    }

    long count = (long)data.size();
    err = SetEOF(refNum, count);
    if (err == noErr && count > 0)
        err = FSWrite(refNum, &count, data.data());
    if (err == noErr && count != (long)data.size())
        err = ioErr;

    OSErr closeErr = FSClose(refNum);
    if (err == noErr)
        err = closeErr;
    if (err != noErr)
        FSpDelete(&temp);
    return err;
}

OSErr WriteStringToFile(const FSSpec& specIn, const std::string& data, OSType type, OSType creator)
{
    FSSpec spec = specIn;
    // Temp sibling "<name>~" (then "<name>~1".."~9"), clamped to the
    // 31-character HFS limit. Never one that exists or equals the target, so
    // an unrelated file is never overwritten or deleted.
    FSSpec temp = spec;
    bool found = false;
    for (int attempt = 0; attempt <= 9 && !found; attempt++)
    {
        unsigned char suffixLen = (unsigned char)(attempt == 0 ? 1 : 2);
        unsigned char len = spec.name[0];
        if (len > 31 - suffixLen)
            len = (unsigned char)(31 - suffixLen);
        memcpy(&temp.name[1], &spec.name[1], len);
        temp.name[len + 1] = '~';
        if (attempt > 0)
            temp.name[len + 2] = (unsigned char)('0' + attempt);
        temp.name[0] = (unsigned char)(len + suffixLen);

        if (temp.name[0] == spec.name[0] && memcmp(&temp.name[1], &spec.name[1], temp.name[0]) == 0)
            continue;
        FSSpec probeTemp;
        if (FSMakeFSSpec(spec.vRefNum, spec.parID, temp.name, &probeTemp) == fnfErr)
            found = true;
    }
    if (!found)
        return dupFNErr;

    OSErr err = WriteNewFile(temp, data, type, creator);
    if (err != noErr)
        return err;

    // Does the target already exist?
    FSSpec probe;
    OSErr existsErr = FSMakeFSSpec(spec.vRefNum, spec.parID, spec.name, &probe);
    if (existsErr == fnfErr)
    {
        err = FSpRename(&temp, spec.name);
        if (err != noErr)
            FSpDelete(&temp);
        return err;
    }
    if (existsErr != noErr)
    {
        FSpDelete(&temp);
        return existsErr;
    }

    err = FSpExchangeFiles(&temp, &spec);
    if (err == noErr)
    {
        FSpDelete(&temp);   // now holds the old contents
        return noErr;
    }

    // Volume without exchange support: replace the long way
    err = FSpDelete(&spec);
    if (err != noErr)
    {
        FSpDelete(&temp);
        return err;
    }
    err = FSpRename(&temp, spec.name);
    if (err != noErr)
        FSpDelete(&temp);
    return err;
}
