#pragma once

#include "Screen.hpp"
#include <map>

class FlashScreen : public Screen
{
public:
    struct FirmwareHeader {
        uint32_t version;
        uint32_t blockSize;
        uint32_t sequencePerSession;
        uint32_t imageSize;
    };

public:
    FlashScreen();
    virtual ~FlashScreen();

    void Draw();

    bool Update(VPADStatus& input);

    void OnUpdateCompleted(int32_t result);

private:
    enum State {
        STATE_SELECT_FILE,
        STATE_CONFIRM,
        STATE_PREPARE,
        STATE_CONFIRM2,
        STATE_UPDATE,
        STATE_FLASHING,
        STATE_ACTIVATE,
        STATE_DONE,
        STATE_ERROR,
    } mState = STATE_SELECT_FILE;

    enum FileID {
        FILE_ORIGINAL,
        FILE_SDCARD,
    } mFile = FILE_ORIGINAL;
    struct FileEntry {
        uint16_t icon;
        const char* name;
    };
    std::map<FileID, FileEntry> mFileEntries;

    std::string mErrorString;
    std::string mFirmwarePath;
    FirmwareHeader mFirmwareHeader;

    int32_t mFlashingProgress;
    bool mUpdateComplete;
    int32_t mUpdateResult;
};
