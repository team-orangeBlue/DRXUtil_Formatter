#include "FormatScreen.hpp"
#include "Gfx.hpp"
#include "ProcUI.hpp"
#include "Utils.hpp"
#include <cstdio>

#include <coreinit/mcp.h>
#include <coreinit/thread.h>
#include <nsysccr/cdc.h>
#include <nsysccr/cfg.h>
#include <nn/ccr.h>

extern "C" uint32_t CCRSysInitializeSettings();
namespace {}

FormatScreen::FormatScreen(){}
FormatScreen::~FormatScreen(){}
void FormatScreen::Draw()
{
    DrawTopBar("FormatScreen");

    switch (mState)
    {
        case STATE_UPDATE: {
            Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2, 64, Gfx::COLOR_TEXT, "Resetting data", Gfx::ALIGN_CENTER);
            break;
        }
        case STATE_DONE: {
            Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2, 64, Gfx::COLOR_TEXT,
                Utils::sprintf("Done!\nPlease hold POWER on the DRC."),
                Gfx::ALIGN_CENTER);
            break;
        }
        case STATE_ERROR: {
            Gfx::Print(Gfx::SCREEN_WIDTH / 2, Gfx::SCREEN_HEIGHT / 2, 64, Gfx::COLOR_ERROR, "Error:\n" + mErrorString, Gfx::ALIGN_CENTER);
            break;
        }
    }
}

bool FormatScreen::Update(VPADStatus& input) // This is the core logic part
{
    switch (mState)
    {
        case STATE_UPDATE: {
            //ProcUI::SetHomeButtonMenuEnabled(false);


            // Abort any potential pending software updates
            CCRCDCSoftwareAbort(CCR_CDC_DESTINATION_DRC0);

            // Erase DRC
            if(CCRSysInitializeSettings() != 0){
            	mErrorString = "Erase failed.";
                mState = STATE_ERROR;
                break;
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

void FormatScreen::OnEraseCompleted()
{
    mEraseComplete = true;
}
