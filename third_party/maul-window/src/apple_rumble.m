// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Gamepad motors on macOS and iOS through CoreHaptics (apple_pad.h). A pad whose
// haptics reach each grip has two engines: the left grip's is the heavy,
// low motor and the right's the light, high one, as on Xbox pads. One
// that reaches only its whole body has one engine, which runs at the
// stronger of the two. Each engine plays one continuous event of no end
// whose intensity follows the motor's; a motor at 0 stops its player.
// The engines are made when a pad first rumbles and kept, per pad, until
// the pad is gone or the pads stop. CoreHaptics would tell of a reset
// or a stop on a queue of its own; instead a call that fails drops its
// engine, and the motor is made again once, on the main thread.

#include "apple_pad.h"

#import <CoreHaptics/CoreHaptics.h>
#import <GameController/GameController.h>

// A motor: its engine and the player of its endless event, made when it
// first runs.
API_AVAILABLE(macos(11.0), ios(14.0))
@interface MwinAppleMotor : NSObject
{
  @public
    GCController* controller;
    GCHapticsLocality locality;
    CHHapticEngine* engine;
    id<CHHapticPatternPlayer> player;
    bool running;
}
@end

API_AVAILABLE(macos(11.0), ios(14.0))
@implementation MwinAppleMotor
- (void)drop
{
    if (running)
    {
        (void)[player stopAtTime:CHHapticTimeImmediate error:nullptr];
    }
    [(id)player release];
    player = nil;
    [engine stopWithCompletionHandler:nil];
    [engine release];
    engine = nil;
    running = false;
}

- (void)dealloc
{
    [self drop];
    [super dealloc];
}

// Makes the engine and the player of an endless event at full strength.
- (bool)make
{
    engine = [[controller.haptics createEngineWithLocality:locality] retain];
    if (engine == nil || ![engine startAndReturnError:nullptr])
    {
        [self drop];
        return false;
    }
    CHHapticEventParameter* strength =
        [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity
                                                      value:1.0f];
    CHHapticEvent* event =
        [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous
                                      parameters:@[ strength ]
                                    relativeTime:0.0
                                        duration:(NSTimeInterval)GCHapticDurationInfinite];
    CHHapticPattern* pattern = [[CHHapticPattern alloc] initWithEvents:@[ event ]
                                                            parameters:@[]
                                                                 error:nullptr];
    player = pattern != nil ? [[engine createPlayerWithPattern:pattern error:nullptr] retain] : nil;
    [pattern release];
    [event release];
    [strength release];
    if (player == nil)
    {
        [self drop];
        return false;
    }
    return true;
}

// Sets the intensity and starts the player, or stops it at 0.
- (bool)apply:(float)intensity
{
    if (intensity <= 0.0f)
    {
        bool stopped = !running || [player stopAtTime:CHHapticTimeImmediate error:nullptr];
        running = false;
        return stopped;
    }
    if (player == nil && ![self make])
    {
        return false;
    }
    CHHapticDynamicParameter* control = [[CHHapticDynamicParameter alloc]
        initWithParameterID:CHHapticDynamicParameterIDHapticIntensityControl
                      value:intensity
               relativeTime:0.0];
    bool sent = [player sendParameters:@[ control ] atTime:CHHapticTimeImmediate error:nullptr];
    [control release];
    if (sent && !running)
    {
        sent = [player startAtTime:CHHapticTimeImmediate error:nullptr];
        running = sent;
    }
    return sent;
}

- (bool)run:(float)intensity
{
    if ([self apply:intensity])
    {
        return true;
    }
    // The engine was reset or stopped under it: made again once.
    [self drop];
    return [self apply:intensity];
}
@end

// A pad's motors: two, one per grip, or one.
API_AVAILABLE(macos(11.0), ios(14.0))
@interface MwinAppleMotors : NSObject
{
  @public
    MwinAppleMotor* low;
    MwinAppleMotor* high;
}
@end

API_AVAILABLE(macos(11.0), ios(14.0))
@implementation MwinAppleMotors
- (void)dealloc
{
    [low release];
    [high release];
    [super dealloc];
}
@end

static bool HasGrips(GCController* controller) API_AVAILABLE(macos(11.0), ios(14.0))
{
    NSSet<GCHapticsLocality>* places = controller.haptics.supportedLocalities;
    return [places containsObject:GCHapticsLocalityLeftHandle] &&
           [places containsObject:GCHapticsLocalityRightHandle];
}

static MwinAppleMotor* MotorOf(GCController* controller, GCHapticsLocality locality)
    API_AVAILABLE(macos(11.0), ios(14.0))
{
    MwinAppleMotor* motor = [[MwinAppleMotor alloc] init];
    motor->controller = controller;
    motor->locality = locality;
    return motor;
}

bool mwinAppleCanRumble(id controller)
{
    if (@available(macOS 11.0, iOS 14.0, *))
    {
        GCDeviceHaptics* haptics = ((GCController*)controller).haptics;
        return haptics != nil && haptics.supportedLocalities.count > 0;
    }
    return false;
}

bool mwinAppleRumble(mwinApplePads* pads, id controller, float low, float high)
{
    if (@available(macOS 11.0, iOS 14.0, *))
    {
        if (pads->rumbles == nil)
        {
            pads->rumbles = [[NSMapTable strongToStrongObjectsMapTable] retain];
        }
        NSMapTable* rumbles = pads->rumbles;
        MwinAppleMotors* motors = [rumbles objectForKey:controller];
        if (motors == nil)
        {
            motors = [[MwinAppleMotors alloc] init];
            bool grips = HasGrips(controller);
            motors->low =
                MotorOf(controller, grips ? GCHapticsLocalityLeftHandle : GCHapticsLocalityDefault);
            motors->high = grips ? MotorOf(controller, GCHapticsLocalityRightHandle) : nil;
            [rumbles setObject:motors forKey:controller];
            [motors release];
        }
        if (motors->high == nil)
        {
            return [motors->low run:low > high ? low : high];
        }
        bool lowRan = [motors->low run:low];
        return [motors->high run:high] && lowRan;
    }
    return false;
}

void mwinAppleForgetRumbles(mwinApplePads* pads, NSArray* kept)
{
    NSMapTable* rumbles = pads->rumbles;
    for (id controller in [[rumbles keyEnumerator] allObjects])
    {
        if (![kept containsObject:controller])
        {
            [rumbles removeObjectForKey:controller];
        }
    }
    if (kept == nil)
    {
        [pads->rumbles release];
        pads->rumbles = nil;
    }
}
