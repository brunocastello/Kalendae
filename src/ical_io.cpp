/*
 * Kalendae - Classic Mac OS 9 Calendar Application
 *
 * Whole-file read and safe-save write via the File Manager
 */

#include <cstring>
#include "ical_io.h"

OSErr ReadFileToString(const FSSpec& spec, std::string& out)
{
    out.clear();

    short refNum;
    OSErr err = FSpOpenDF(&spec, fsRdPerm, &refNum);
    if (err != noErr)
        return err;

    long eof = 0;
    err = GetEOF(refNum, &eof);
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
static OSErr WriteNewFile(const FSSpec& temp, const std::string& data, OSType type, OSType creator)
{
    OSErr err = FSpCreate(&temp, creator, type, smSystemScript);
    if (err == dupFNErr)
    {
        // Leftover from an interrupted save
        FSpDelete(&temp);
        err = FSpCreate(&temp, creator, type, smSystemScript);
    }
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

OSErr WriteStringToFile(const FSSpec& spec, const std::string& data, OSType type, OSType creator)
{
    // Temp sibling "<name>~", clamped to the 31-character HFS limit
    FSSpec temp = spec;
    unsigned char len = spec.name[0];
    if (len > 30)
        len = 30;
    memcpy(&temp.name[1], &spec.name[1], len);
    temp.name[len + 1] = '~';
    temp.name[0] = (unsigned char)(len + 1);

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
