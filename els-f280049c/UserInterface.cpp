// Clough42 Electronic Leadscrew
// https://github.com/clough42/electronic-leadscrew
//
// MIT License
//
// Copyright (c) 2019 James Clough
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.


#include "UserInterface.h"

const MESSAGE STARTUP_MESSAGE_2 =
{
  .message = { LETTER_E, LETTER_L, LETTER_S, DASH, ONE | POINT, FOUR | POINT, ZERO, ONE },
  .displayTime = UI_REFRESH_RATE_HZ * 1.5
};

const MESSAGE STARTUP_MESSAGE_1 =
{
 .message = { LETTER_C, LETTER_L, LETTER_O, LETTER_U, LETTER_G, LETTER_H, FOUR, TWO },
 .displayTime = UI_REFRESH_RATE_HZ * 1.5,
 .next = &STARTUP_MESSAGE_2
};

const MESSAGE ANGLE_ZERO_CONFIRM_MESSAGE =
{
 .message = { LETTER_Z, LETTER_E, LETTER_R, LETTER_O, BLANK, LETTER_O, LETTER_K, BLANK },
 .displayTime = UI_REFRESH_RATE_HZ / 2,
 .next = NULL
};

static const Uint16 ANGLE_ZERO_HOLD_TICKS = UI_REFRESH_RATE_HZ * 2;

const Uint16 SETTINGS_MENU_BRIGHTNESS[8] =
{
    LETTER_B, LETTER_R, LETTER_I, LETTER_G, LETTER_H, LETTER_T, BLANK, BLANK
};

const Uint16 SETTINGS_MENU_EXIT[8] =
{
    LETTER_E, LETTER_X, LETTER_I, LETTER_T, BLANK, BLANK, BLANK, BLANK
};

const Uint16 SETTINGS_MENU_ANGLE[8] =
{
    LETTER_A, LETTER_N, LETTER_G, LETTER_L, LETTER_E, BLANK, BLANK, BLANK
};

static Uint16 SETTINGS_ANGLE_EDIT[8] =
{
    LETTER_A, LETTER_N, LETTER_G, BLANK, LETTER_O, LETTER_F, LETTER_F, BLANK
};

static Uint16 SETTINGS_BRIGHTNESS_EDIT[8] =
{
    LETTER_B, LETTER_R, LETTER_I, LETTER_G, LETTER_H, LETTER_T, BLANK, ZERO
};

extern const MESSAGE BACKLOG_PANIC_MESSAGE_2;
const MESSAGE BACKLOG_PANIC_MESSAGE_1 =
{
 .message = { LETTER_T, LETTER_O, LETTER_O, BLANK, LETTER_F, LETTER_A, LETTER_S, LETTER_T },
 .displayTime = UI_REFRESH_RATE_HZ * .5,
 .next = &BACKLOG_PANIC_MESSAGE_2
};
const MESSAGE BACKLOG_PANIC_MESSAGE_2 =
{
 .message = { BLANK, LETTER_R, LETTER_E, LETTER_S, LETTER_E, LETTER_T, BLANK, BLANK },
 .displayTime = UI_REFRESH_RATE_HZ * .5,
 .next = &BACKLOG_PANIC_MESSAGE_1
};



const Uint16 VALUE_BLANK[4] = { BLANK, BLANK, BLANK, BLANK };

UserInterface :: UserInterface(ControlPanel *controlPanel, Core *core, FeedTableFactory *feedTableFactory, Encoder *encoder)
{
    this->controlPanel = controlPanel;
    this->core = core;
    this->feedTableFactory = feedTableFactory;
    this->encoder = encoder;

    this->metric = false; // start out with imperial
    this->thread = false; // start out with feeds
    this->reverse = false; // start out going forward

    this->feedTable = NULL;

    this->keys.all = 0xff;

    this->settingsMode = SETTINGS_NONE;
    this->settingsIndex = 0;
    this->pendingBrightness = 0;
    this->pendingAngle = 0;
    this->showAngleWhenPowerOff = false;
    this->setHoldTicks = 0;
    this->setHeldLastLoop = false;
    this->setLongPressFired = false;
    this->ignoreNextSetRelease = false;

    // initialize the core so we start up correctly
    core->setReverse(this->reverse);
    core->setFeed(loadFeedTable());

    setMessage(&STARTUP_MESSAGE_1);
}

const FEED_THREAD *UserInterface::loadFeedTable()
{
    this->feedTable = this->feedTableFactory->getFeedTable(this->metric, this->thread);
    return this->feedTable->current();
}

LED_REG UserInterface::calculateLEDs()
{
    // get the LEDs for this feed
    LED_REG leds = feedTable->current()->leds;

    if( this->core->isPowerOn() )
    {
        // and add a few of our own
        leds.bit.POWER = 1;
        leds.bit.REVERSE = this->reverse;
        leds.bit.FORWARD = ! this->reverse;
    }
    else
    {
        // power is off
        leds.all = 0;
    }

    return leds;
}

static Uint16 digitSegments(Uint16 value)
{
    switch( value )
    {
        case 0: return ZERO;
        case 1: return ONE;
        case 2: return TWO;
        case 3: return THREE;
        case 4: return FOUR;
        case 5: return FIVE;
        case 6: return SIX;
        case 7: return SEVEN;
        case 8: return EIGHT;
        case 9: return NINE;
        default: return BLANK;
    }
}

void UserInterface :: handleSettings(void)
{
    const Uint16 settingsCount = 3;

    if( this->settingsMode == SETTINGS_MENU )
    {
        if( keys.bit.UP )
        {
            if( this->settingsIndex == 0 )
                this->settingsIndex = settingsCount - 1;
            else
                this->settingsIndex--;
        }
        if( keys.bit.DOWN )
        {
            this->settingsIndex = (this->settingsIndex + 1) % settingsCount;
        }
        if( keys.bit.SET )
        {
            if( this->settingsIndex == 0 )
            {
                this->pendingBrightness = controlPanel->getBrightness();
                if( this->pendingBrightness < 1 ) this->pendingBrightness = 1;
                this->settingsMode = SETTINGS_BRIGHTNESS;
            }
            else if( this->settingsIndex == 1 )
            {
                this->pendingAngle = this->showAngleWhenPowerOff ? 1 : 0;
                this->settingsMode = SETTINGS_ANGLE;
            }
            else
            {
                this->settingsMode = SETTINGS_NONE;
                controlPanel->setMessage(NULL);
                this->ignoreNextSetRelease = true;
                return;
            }
        }

        if( this->settingsIndex == 0 )
            controlPanel->setMessage(SETTINGS_MENU_BRIGHTNESS);
        else if( this->settingsIndex == 1 )
            controlPanel->setMessage(SETTINGS_MENU_ANGLE);
        else
            controlPanel->setMessage(SETTINGS_MENU_EXIT);
    }
    else if( this->settingsMode == SETTINGS_BRIGHTNESS )
    {
        if( keys.bit.UP )
        {
            if( this->pendingBrightness < 8 ) this->pendingBrightness++;
        }
        if( keys.bit.DOWN )
        {
            if( this->pendingBrightness > 1 ) this->pendingBrightness--;
        }
        if( keys.bit.SET )
        {
            controlPanel->setBrightness(this->pendingBrightness);
            this->settingsMode = SETTINGS_MENU;
        }

        SETTINGS_BRIGHTNESS_EDIT[7] = digitSegments(this->pendingBrightness);
        controlPanel->setMessage(SETTINGS_BRIGHTNESS_EDIT);
    }
    else if( this->settingsMode == SETTINGS_ANGLE )
    {
        if( keys.bit.UP || keys.bit.DOWN )
            this->pendingAngle = this->pendingAngle ? 0 : 1;
        if( keys.bit.SET )
        {
            this->showAngleWhenPowerOff = (this->pendingAngle != 0);
            this->settingsMode = SETTINGS_MENU;
        }

        if( this->pendingAngle )
        {
            SETTINGS_ANGLE_EDIT[4] = LETTER_O;
            SETTINGS_ANGLE_EDIT[5] = LETTER_N;
            SETTINGS_ANGLE_EDIT[6] = BLANK;
            SETTINGS_ANGLE_EDIT[7] = BLANK;
        }
        else
        {
            SETTINGS_ANGLE_EDIT[4] = LETTER_O;
            SETTINGS_ANGLE_EDIT[5] = LETTER_F;
            SETTINGS_ANGLE_EDIT[6] = LETTER_F;
            SETTINGS_ANGLE_EDIT[7] = BLANK;
        }
        controlPanel->setMessage(SETTINGS_ANGLE_EDIT);
    }
}

void UserInterface :: setMessage(const MESSAGE *message)
{
    this->message = message;
    this->messageTime = message->displayTime;
}

void UserInterface :: overrideMessage( void )
{
    if( this->message != NULL )
    {
        if( this->messageTime > 0 ) {
            this->messageTime--;
            controlPanel->setMessage(this->message->message);
        }
        else {
            this->message = this->message->next;
            if( this->message == NULL )
                controlPanel->setMessage(NULL);
            else
                this->messageTime = this->message->displayTime;
        }
    }
}

void UserInterface :: clearMessage( void )
{
    this->message = NULL;
    this->messageTime = 0;
    controlPanel->setMessage(NULL);
}

void UserInterface :: panicStepBacklog( void )
{
    setMessage(&BACKLOG_PANIC_MESSAGE_1);
}

void UserInterface :: loop( void )
{
    // read the RPM up front so we can use it to make decisions
    Uint16 currentRpm = core->getRPM();

    // display an override message, if there is one
    overrideMessage();

    // read keypresses from the control panel
    keys = controlPanel->getKeys();
    KEY_REG heldKeys = controlPanel->getLatchedKeys();
    bool setHeldNow = heldKeys.bit.SET != 0;

    if( this->settingsMode == SETTINGS_NONE )
    {
        bool angleZeroEligible = (!core->isPowerOn() && this->showAngleWhenPowerOff && currentRpm == 0);

        if( setHeldNow )
        {
            if( !this->setHeldLastLoop )
            {
                this->setHoldTicks = 0;
                this->setLongPressFired = false;
            }

            if( this->setHoldTicks < ANGLE_ZERO_HOLD_TICKS )
            {
                this->setHoldTicks++;
            }

            if( angleZeroEligible && this->setHoldTicks >= ANGLE_ZERO_HOLD_TICKS && !this->setLongPressFired )
            {
                encoder->setAngleZero();
                this->setLongPressFired = true;
                setMessage(&ANGLE_ZERO_CONFIRM_MESSAGE);
            }
        }
        else if( this->setHeldLastLoop )
        {
            if( !this->setLongPressFired && currentRpm == 0 && !this->ignoreNextSetRelease )
            {
                this->settingsMode = SETTINGS_MENU;
                this->settingsIndex = 0;
                clearMessage();
            }

            this->setHoldTicks = 0;
            this->setLongPressFired = false;
            this->ignoreNextSetRelease = false;
        }

        this->setHeldLastLoop = setHeldNow;
    }
    else
    {
        this->setHeldLastLoop = false;
        this->setHoldTicks = 0;
        this->setLongPressFired = false;
    }

    if( this->settingsMode != SETTINGS_NONE )
    {
        handleSettings();
        controlPanel->refresh(false);
        return;
    }

    // respond to keypresses
    if( currentRpm == 0 )
    {
        // these keys should only be sensitive when the machine is stopped
        if( keys.bit.POWER ) {
            this->core->setPowerOn(!this->core->isPowerOn());
            clearMessage();
        }

        // these should only work when the power is on
        if( this->core->isPowerOn() ) {
            if( keys.bit.IN_MM )
            {
                this->metric = ! this->metric;
                core->setFeed(loadFeedTable());
            }
            if( keys.bit.FEED_THREAD )
            {
                this->thread = ! this->thread;
                core->setFeed(loadFeedTable());
            }
            if( keys.bit.FWD_REV )
            {
                this->reverse = ! this->reverse;
                core->setReverse(this->reverse);
            }
        }
    }

#ifdef IGNORE_ALL_KEYS_WHEN_RUNNING
    if( currentRpm == 0 )
        {
#endif // IGNORE_ALL_KEYS_WHEN_RUNNING

        // these should only work when the power is on
        if( this->core->isPowerOn() ) {
            // these keys can be operated when the machine is running
            if( keys.bit.UP )
            {
                core->setFeed(feedTable->next());
            }
            if( keys.bit.DOWN )
            {
                core->setFeed(feedTable->previous());
            }
        }

#ifdef IGNORE_ALL_KEYS_WHEN_RUNNING
    }
#endif // IGNORE_ALL_KEYS_WHEN_RUNNING

    // update the control panel
    controlPanel->setLEDs(calculateLEDs());
    controlPanel->setValue(feedTable->current()->display);
    controlPanel->setRPM(currentRpm);

    bool showAngle = (!core->isPowerOn() && this->showAngleWhenPowerOff);
    if( showAngle )
    {
        controlPanel->setSpindleAngle(encoder->getSpindleAngle());
    }

    if( ! core->isPowerOn() )
    {
        controlPanel->setValue(VALUE_BLANK);
    }

    controlPanel->refresh(showAngle);
}
