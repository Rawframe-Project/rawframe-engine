// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Gamepads on macOS and iOS through GameController (apple_pad.h), for
// the pad tracker (pad_tracker.h): the controllers with an extended gamepad
// profile, which GameController maps by place, so every one is mapped.
// A controller's name is its vendor name; GameController gives no USB
// ids. Its battery is the charge GameController reports, -1 while the
// state is unknown (a wired pad). Its reading's time is the profile's
// last event's. Connections and disconnections come as notifications on
// the main thread, which only mark that the pads should be looked for.
// On macOS pads are read whether or not the program is in front, as on
// the other desktop platforms; iOS gives an application in the
// background none, and runs no frames there. It needs GameController of
// macOS 11.3 or iOS 14.5; before it, no pad is found. The motors are
// CoreHaptics' (apple_rumble.m). Motion is GCMotion's: its handler,
// called on the main queue, posts each sample, acceleration from g to
// m/s^2 and both turned into the contract's frame as SDL turns them.

#include "apple_pad.h"

#import <GameController/GameController.h>
#include <math.h>
#include <string.h>
#include <time.h>

#define STANDARD_GRAVITY 9.80665

static int32_t List(void* self, void** pads, uint32_t capacity)
    API_AVAILABLE(macos(11.0), ios(14.0))
{
    (void)self;
    NSArray<GCController*>* controllers = [GCController controllers];
    // The motors of pads gone go with them.
    mwinAppleForgetRumbles(self, controllers);
    uint32_t count = 0;
    for (GCController* controller in controllers)
    {
        if (count < capacity && controller.extendedGamepad != nil)
        {
            pads[count++] = [controller retain];
        }
    }
    return (int32_t)count;
}

static void Release(void* self, void* pad)
{
    (void)self;
    [(GCController*)pad release];
}

static uint32_t Bit(bool held, mwinGamepadButton button)
{
    return held ? 1u << button : 0;
}

static bool Read(void* self, void* pad, mwinPadReading* reading)
    API_AVAILABLE(macos(11.0), ios(14.0))
{
    (void)self;
    GCExtendedGamepad* profile = ((GCController*)pad).extendedGamepad;
    if (profile == nil)
    {
        return false;
    }
    // The time's bits: the tracker only asks whether it moved.
    double time = profile.lastEventTimestamp;
    *reading = (mwinPadReading){0};
    memcpy(&reading->timestamp, &time, sizeof(time));
    GCControllerDirectionPad* dpad = profile.dpad;
    reading->buttons =
        Bit(dpad.up.pressed, mwin_padDpadUp) | Bit(dpad.down.pressed, mwin_padDpadDown) |
        Bit(dpad.left.pressed, mwin_padDpadLeft) | Bit(dpad.right.pressed, mwin_padDpadRight) |
        Bit(profile.buttonA.pressed, mwin_padFaceSouth) |
        Bit(profile.buttonB.pressed, mwin_padFaceEast) |
        Bit(profile.buttonX.pressed, mwin_padFaceWest) |
        Bit(profile.buttonY.pressed, mwin_padFaceNorth) |
        Bit(profile.leftShoulder.pressed, mwin_padShoulderLeft) |
        Bit(profile.rightShoulder.pressed, mwin_padShoulderRight) |
        Bit(profile.leftThumbstickButton.pressed, mwin_padStickLeft) |
        Bit(profile.rightThumbstickButton.pressed, mwin_padStickRight) |
        Bit(profile.buttonMenu.pressed, mwin_padStart) |
        Bit(profile.buttonOptions.pressed, mwin_padSelect) |
        Bit(profile.buttonHome.pressed, mwin_padGuide);
    // GameController counts up as positive.
    reading->axes[mwin_padStickLeftX] = profile.leftThumbstick.xAxis.value;
    reading->axes[mwin_padStickLeftY] = -profile.leftThumbstick.yAxis.value;
    reading->axes[mwin_padStickRightX] = profile.rightThumbstick.xAxis.value;
    reading->axes[mwin_padStickRightY] = -profile.rightThumbstick.yAxis.value;
    reading->axes[mwin_padTriggerLeft] = profile.leftTrigger.value;
    reading->axes[mwin_padTriggerRight] = profile.rightTrigger.value;
    return true;
}

// The grips' motors; no pad is granted the triggers' yet.
static bool Vibrate(void* self, void* pad, const float motors[4])
{
    return mwinAppleRumble(self, (id)pad, motors);
}

// The vendor name as UTF-8 that fits, a character never split.
static void Describe(void* self, void* pad, mwinGamepadInfo* info)
    API_AVAILABLE(macos(11.0), ios(14.0))
{
    (void)self;
    NSString* name = ((GCController*)pad).vendorName;
    if (name.length == 0)
    {
        name = @"Gamepad";
    }
    NSUInteger used = 0;
    [name getBytes:info->name
             maxLength:sizeof(info->name)
            usedLength:&used
              encoding:NSUTF8StringEncoding
               options:0
                 range:NSMakeRange(0, name.length)
        remainingRange:nullptr];
    info->nameLength = (uint32_t)used;
    GCController* controller = pad;
    info->capabilities =
        (mwinGamepadCapabilities)((mwinAppleCanRumble(controller) ? mwin_padRumble : 0) |
                                  (mwinAppleCanRumbleTriggers(controller) ? mwin_padTriggerRumble
                                                                          : 0) |
                                  (controller.motion != nil && controller.motion.hasRotationRate
                                       ? mwin_padMotion
                                       : 0));
}

static int8_t Battery(void* self, void* pad) API_AVAILABLE(macos(11.0), ios(14.0))
{
    (void)self;
    GCDeviceBattery* battery = ((GCController*)pad).battery;
    if (battery == nil || battery.batteryState == GCDeviceBatteryStateUnknown)
    {
        return -1;
    }
    float level = battery.batteryLevel;
    return (int8_t)(level <= 0.0f ? 0 : level >= 1.0f ? 100 : lroundf(level * 100.0f));
}

static bool Changed(void* self)
{
    mwinApplePads* pads = self;
    bool changed = pads->changed;
    pads->changed = false;
    return changed;
}

static id Watch(mwinApplePads* pads, NSNotificationName name)
{
    return [[[NSNotificationCenter defaultCenter] addObserverForName:name
                                                              object:nil
                                                               queue:nil
                                                          usingBlock:^(NSNotification* note) {
                                                            (void)note;
                                                            pads->changed = true;
                                                          }] retain];
}

void mwinAppleStartPads(mwinApplePads* pads, mwinContext* context)
{
    if (@available(macOS 11.3, iOS 14.5, *))
    {
        // Set before the application finishes launching, as it must be.
        GCController.shouldMonitorBackgroundEvents = YES;
        pads->observers[0] = Watch(pads, GCControllerDidConnectNotification);
        pads->observers[1] = Watch(pads, GCControllerDidDisconnectNotification);
        const mwinPadRuntime runtime = {pads,    List,     Release, Read,
                                        Vibrate, Describe, Battery, Changed};
        mwinPadTrackerStart(&pads->tracker, context, &runtime);
        pads->started = true;
    }
}

void mwinAppleStopPads(mwinApplePads* pads)
{
    if (!pads->started)
    {
        return;
    }
    mwinPadTrackerStop(&pads->tracker);
    mwinAppleForgetRumbles(pads, nil);
    for (int i = 0; i < 2; i++)
    {
        [[NSNotificationCenter defaultCenter] removeObserver:pads->observers[i]];
        [pads->observers[i] release];
    }
    pads->started = false;
}

void mwinApplePumpPads(mwinApplePads* pads, uint64_t nowNs)
{
    if (pads->started)
    {
        mwinPadTrackerPump(&pads->tracker, nowNs);
    }
}

mwinResult mwinAppleSetMotion(mwinApplePads* pads, uint32_t slot, bool enabled)
{
    if (@available(macOS 11.3, iOS 14.5, *))
    {
        GCController* controller = nil;
        for (uint32_t i = 0; i < pads->tracker.count; i++)
        {
            if (pads->tracker.pads[i].slot == slot)
            {
                controller = (GCController*)pads->tracker.pads[i].pad;
            }
        }
        GCMotion* motion = controller.motion;
        if (motion == nil)
        {
            return mwin_errorPlatform;
        }
        if (motion.sensorsRequireManualActivation)
        {
            motion.sensorsActive = enabled;
        }
        if (!enabled)
        {
            motion.valueChangedHandler = nil;
            return mwin_success;
        }
        // The pad by id: a sample after it is gone posts nothing.
        mwinContext* context = pads->tracker.context;
        mwinGamepadId gamepad = mwinGamepadIdOf(context, slot);
        motion.valueChangedHandler = ^(GCMotion* moved) {
          int32_t live = mwinFindGamepad(context, gamepad);
          if (live < 0)
          {
              return;
          }
          GCAcceleration a = moved.acceleration;
          GCRotationRate r = moved.rotationRate;
          const float acceleration[3] = {(float)(-a.x * STANDARD_GRAVITY),
                                         (float)(-a.y * STANDARD_GRAVITY),
                                         (float)(-a.z * STANDARD_GRAVITY)};
          const float rate[3] = {(float)r.x, (float)r.z, (float)-r.y};
          mwinPostGamepadMotion(context, (uint32_t)live, acceleration, rate,
                                clock_gettime_nsec_np(CLOCK_UPTIME_RAW));
        };
        return mwin_success;
    }
    return mwin_errorPlatform;
}
