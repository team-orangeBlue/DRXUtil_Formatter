#include "DrcLangScreen.hpp"
#include "Gfx.hpp"
#include "ProcUI.hpp"
#include "Utils.hpp"
#include <cstdio>

#include <coreinit/mcp.h>
#include <coreinit/thread.h>
#include <coreinit/filesystem_fsa.h>
#include <nsysccr/cdc.h>
#include <nsysccr/cfg.h>
#include <nn/ccr.h>

extern "C" uint32_t CCRCDCSoftwareExtUpdate(CCRCDCDestination dest, CCRCDCExt ext, const char *path, IOSAsyncCallbackFn callback, void *userContext);
namespace {

bool CopyFile(const std::string& srcPath, const std::string& dstPath)
{
    FILE* inf = fopen(srcPath.c_str(), "rb");
    if (!inf) {
        return false;
    }

    FILE* outf = fopen(dstPath.c_str(), "wb");
    if (!outf) {
        fclose(inf);
        return false;
    }

    uint8_t buf[4096];
    size_t bytesRead;
    while ((bytesRead = fread(buf, 1, sizeof(buf), inf)) > 0) {
        if (fwrite(buf, 1, bytesRead, outf) != bytesRead) {
            fclose(inf);
            fclose(outf);
            return false;
        }
    }

    if (ferror(inf)) {
        fclose(inf);
        fclose(outf);
        return false;
    }

    fclose(inf);
    fclose(outf);
    return true;
}

bool CaffeineInvalidate()
{
    CCRCDCSoftwareVersion version;
    CCRCDCSoftwareGetVersion(CCR_CDC_DESTINATION_DRC0, &version);

    // Only newer versions have caffeine
    if (version.runningVersion >= 0x180a0000) {
        return CCRSysCaffeineSetCaffeineSlot(0xff) == 0;
    }

    return true;
}

bool WaitForEeprom(uint32_t drcSlot)
{
    uint8_t val;
    OSTime startTime = OSGetSystemTime();
    while (CCRCFGGetCachedEeprom(drcSlot, 0, &val, sizeof(val)) == -1) {
        // 2 second timeout
        if (OSTicksToSeconds(OSGetSystemTime() - startTime) > 2) {
            return false;
        }

        OSSleepTicks(OSMillisecondsToTicks(200));
    }

    return true;
}

bool ReattachDRC(CCRCDCDestination dest, CCRCDCDrcStateEnum targetState, BOOL unknown)
{
    // Get the current DRC state
    CCRCDCDrcState state;
    int32_t res = CCRCDCSysGetDrcState(dest, &state);
    if (res != 0) {
        return false;
    }

    // Not sure what state 3 is
    if (state.state == CCR_CDC_DRC_STATE_UNK3) {
        state.state = CCR_CDC_DRC_STATE_ACTIVE;
    }

    // Nothing to do if we're already in the target state
    if (state.state == targetState) {
        return true;
    }

    __CCRSysInitReattach(dest - CCR_CDC_DESTINATION_DRC0);

    // Set target state
    state.state = targetState;
    res = CCRCDCSysSetDrcState(dest, &state);
    if (res != 0) {
        return false;
    }

    // Wait for the DRC to reattach
    res = __CCRSysWaitReattach(dest - CCR_CDC_DESTINATION_DRC0, unknown);
    if (res != 0) {
        return false;
    }

    // Wait for EEPROM
    if (!WaitForEeprom(dest - CCR_CDC_DESTINATION_DRC0)) {
        return false;
    }

    // Check if we're in the state we want
    res = CCRCDCSysGetDrcState(dest, &state);
    if (res != 0) {
        return false;
    }

    if (state.state != targetState) {
        return false;
    }

    return true;
}

bool AbortUpdate(CCRCDCDestination dest)
{
    OSTime startTime = OSGetSystemTime();
    while (CCRCDCSoftwareAbort(dest) != 0) {
        // 3 second timeout
        if (OSTicksToSeconds(OSGetSystemTime() - startTime) > 3) {
            return false;
        }

        OSSleepTicks(OSMillisecondsToTicks(200));
    }

    return true;
}

void SoftwareUpdateCallback(IOSError error, void* arg)
{
    DrcLangScreen* drcLangScreen = static_cast<DrcLangScreen*>(arg);

    drcLangScreen->OnUpdateCompleted(error);
}

}

DrcLangScreen::~DrcLangScreen()
{
}

void DrcLangScreen::Draw()
{
    DrawTopBar("DrcLangScreen");

    switch (mState)
    {
        case STATE_PREPARE: {
            Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2, 64, Gfx::COLOR_TEXT, "Preparing...", Gfx::ALIGN_CENTER);
            break;
        }
        case STATE_CONFIRM2: {
            Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2, 64, Gfx::COLOR_ERROR,
                Utils::sprintf("Are you really really really sure?\n"
                               "About to flash langpack.\n"
                               "Lanuage packs mismatching the firmware\nwill brick your GamePad!\n"),
                Gfx::ALIGN_CENTER);
            break;
        }
        case STATE_UPDATE: {
            Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2, 64, Gfx::COLOR_TEXT, "Starting update...", Gfx::ALIGN_CENTER);
            break;
        }
        case STATE_FLASHING: {
            Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2 - 32, 64, Gfx::COLOR_TEXT, Utils::sprintf("Flashing... %d%%", mFlashingProgress), Gfx::ALIGN_CENTER);
            Gfx::DrawRect(64, Gfx::SCREEN_HEIGHT / 2 + 32, Gfx::SCREEN_WIDTH - 128, 64, 5, Gfx::COLOR_ACCENT);
            Gfx::DrawRectFilled(64, Gfx::SCREEN_HEIGHT / 2 + 32, (Gfx::SCREEN_WIDTH - 128) * (mFlashingProgress / 100.0f), 64, Gfx::COLOR_ACCENT);
            break;
        }
        case STATE_ACTIVATE: {
            Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2, 64, Gfx::COLOR_TEXT, "Activating firmware...", Gfx::ALIGN_CENTER);
            break;
        }
        case STATE_DONE: {
            Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2, 64, Gfx::COLOR_TEXT,
                Utils::sprintf("Done!\n"
                               "Flashed new language pack"),
                Gfx::ALIGN_CENTER);
            break;
        }
        case STATE_ERROR: {
            Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2, 64, Gfx::COLOR_ERROR, "Error:\n" + mErrorString, Gfx::ALIGN_CENTER);
            break;
        }
    }

    if (mState == STATE_CONFIRM2) {
        DrawBottomBar(nullptr, "\ue044 Exit", "\ue000 Confirm / \ue001 Back");
    } else if (mState == STATE_PREPARE || STATE_UPDATE || mState == STATE_FLASHING || mState == STATE_ACTIVATE) {
        DrawBottomBar(nullptr, "Please wait...", nullptr);
    } else {
        DrawBottomBar(nullptr, nullptr, "\ue001 Back");
    }
}

bool DrcLangScreen::Update(VPADStatus& input)
{
    switch (mState)
    {
        case STATE_CONFIRM2: {
            if (input.trigger & VPAD_BUTTON_B) {
                return false;
            }

            if (input.trigger & VPAD_BUTTON_A) {
                mState = STATE_UPDATE;
                break;
            }
            break;
        }
        case STATE_PREPARE: {

                // Copy to MLC so IOS-PAD can install it
                mFirmwarePath = "/vol/storage_mlc01/usr/tmp/lang.bin";
                if (!CopyFile("/vol/external01/lang.bin", "storage_mlc01:/usr/tmp/lang.bin")) {
                    mErrorString = "Failed to copy firmware to MLC";
                    mState = STATE_ERROR;
                    break;
                }
            mState = STATE_CONFIRM2;
            break;
        }
        case STATE_UPDATE: {
            ProcUI::SetHomeButtonMenuEnabled(false);

            if (!CaffeineInvalidate()) {
                mErrorString = "Failed to invalidate caffeine.";
                mState = STATE_ERROR;
                break;  
            }

            // Abort any potential pending software updates
            CCRCDCSoftwareAbort(CCR_CDC_DESTINATION_DRC0);

            // Reattach the DRC in update mode
            if (!ReattachDRC(CCR_CDC_DESTINATION_DRC0, CCR_CDC_DRC_STATE_UPDATE, FALSE)) {
                ReattachDRC(CCR_CDC_DESTINATION_DRC0, CCR_CDC_DRC_STATE_ACTIVE, FALSE);
                mErrorString = "Failed to reattach DRC in update mode.";
                mState = STATE_ERROR;
                break;  
            }

            mFlashingProgress = 0;
            mUpdateComplete = false;
            mUpdateResult = 0;
            if (CCRCDCSoftwareExtUpdate(CCR_CDC_DESTINATION_DRC0, CCR_CDC_EXT_LANGUAGE, mFirmwarePath.c_str(), SoftwareUpdateCallback, this) != 0) {
                AbortUpdate(CCR_CDC_DESTINATION_DRC0);
                ReattachDRC(CCR_CDC_DESTINATION_DRC0, CCR_CDC_DRC_STATE_ACTIVE, FALSE);
                mErrorString = "Failed to start software update.";
                mState = STATE_ERROR;
                break;  
            }

            mState = STATE_FLASHING;
            break;
        }
        case STATE_FLASHING: {
            // Update progress
            CCRCDCFWInfo fwInfo{};
            if (CCRCDCGetFWInfo(CCR_CDC_DESTINATION_DRC0, &fwInfo) == 0) {
                mFlashingProgress = fwInfo.updateProgress;
            }

            OSSleepTicks(OSMillisecondsToTicks(200));

            // Check if update complete
            if (mUpdateComplete) {
                if (mUpdateResult == IOS_ERROR_OK) {
                    mState = STATE_ACTIVATE;
                } else {
                    AbortUpdate(CCR_CDC_DESTINATION_DRC0);
                    ReattachDRC(CCR_CDC_DESTINATION_DRC0, CCR_CDC_DRC_STATE_ACTIVE, FALSE);
                    mErrorString = "Software update failed.";
                    mState = STATE_ERROR;
                }
            }
            break;
        }
        case STATE_ACTIVATE: {
            // Activate the newly flashed firmware
            if (CCRCDCSoftwareActivate(CCR_CDC_DESTINATION_DRC0) != 0) {
                AbortUpdate(CCR_CDC_DESTINATION_DRC0);
                ReattachDRC(CCR_CDC_DESTINATION_DRC0, CCR_CDC_DRC_STATE_ACTIVE, FALSE);
                mErrorString = "Failed to activate software update.";
                mState = STATE_ERROR;
                break;  
            }

            // Put the gamepad back into active mode
            OSTime startTime = OSGetSystemTime();
            while (!ReattachDRC(CCR_CDC_DESTINATION_DRC0, CCR_CDC_DRC_STATE_ACTIVE, FALSE)) {
                // 10 second timeout
                if (OSTicksToSeconds(OSGetSystemTime() - startTime) > 10) {
                    // At this point we don't really care if it times out or not
                    break;
                }

                OSSleepTicks(OSMillisecondsToTicks(1000));
            }

            mState = STATE_DONE;
            break;
        }
        case STATE_DONE: {
            if (input.trigger & VPAD_BUTTON_B) {
                ProcUI::SetHomeButtonMenuEnabled(true);
                return false;
            }
            break;
        }
        case STATE_ERROR: {
            if (input.trigger & VPAD_BUTTON_B) {
                ProcUI::SetHomeButtonMenuEnabled(true);
                return false;
            }
            break;
        }
        
        default:
            break;
    }

    return true;
}

void DrcLangScreen::OnUpdateCompleted(int32_t result)
{
    mUpdateComplete = true;
    mUpdateResult = result;
}
