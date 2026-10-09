// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The audio session. The category follows the streams there are: output
// only Playback, input only Record, both PlayAndRecord with the speaker
// as the default output and Bluetooth allowed. Playback and
// PlayAndRecord mix with others until the host asks for focus: lasting
// or brief focus stops or pauses them, brief mixed focus ducks them.
// Record takes no mixing option and never mixes. With iosSilencedBySwitch, output alone
// is Ambient (or SoloAmbient with focus), which the silent switch
// silences. Interruptions and route changes come as notifications,
// which the observer turns into the context's signals. Manual retain and
// release; every call but the observer's from the control thread.

#include "ios_session.h"

#import <AVFAudio/AVFAudio.h>
#include <stdio.h>

// AVAudioSessionCategoryOptionAllowBluetooth by its value: the iOS 26
// SDK renames it, and either name is deprecated in one SDK or missing in
// another.
#define ALLOW_BLUETOOTH_HFP ((AVAudioSessionCategoryOptions)0x4)

void maudIosSessionFormat(uint32_t* rate, uint32_t* channels)
{
    @autoreleasepool
    {
        AVAudioSession* session = [AVAudioSession sharedInstance];
        double sampleRate = session.sampleRate;
        NSInteger outputs = session.outputNumberOfChannels;
        *rate = sampleRate >= 8000.0 ? (uint32_t)(sampleRate + 0.5) : 48000u;
        *channels = outputs > 0 ? (uint32_t)outputs : 2u;
    }
}

// Turns the session's notifications into the context's signals. The
// pointer is cleared under the object's lock before the observer is
// removed, so no notification in flight reaches a closed context.
@interface MaudAudioSessionObserver : NSObject
{
  @public
    maudIosSignals* signals;
}
- (void)interrupted:(NSNotification*)note;
- (void)routeChanged:(NSNotification*)note;
@end

@implementation MaudAudioSessionObserver

- (void)interrupted:(NSNotification*)note
{
    NSNumber* type = note.userInfo[AVAudioSessionInterruptionTypeKey];
    if (type == nil)
    {
        return;
    }
    int value = MAUD_IOS_INTERRUPTION_BEGAN;
    if (type.unsignedIntegerValue == AVAudioSessionInterruptionTypeEnded)
    {
        NSNumber* options = note.userInfo[AVAudioSessionInterruptionOptionKey];
        bool resume =
            (options.unsignedIntegerValue & AVAudioSessionInterruptionOptionShouldResume) != 0;
        value = resume ? MAUD_IOS_INTERRUPTION_RESUME : MAUD_IOS_INTERRUPTION_STOPPED;
    }
    @synchronized(self)
    {
        if (signals != nullptr)
        {
            if (value == MAUD_IOS_INTERRUPTION_BEGAN)
            {
                atomic_store_explicit(&signals->began, true, memory_order_release);
            }
            atomic_store_explicit(&signals->interruption, value, memory_order_release);
        }
    }
}

- (void)routeChanged:(NSNotification*)note
{
    (void)note;
    @synchronized(self)
    {
        if (signals != nullptr)
        {
            atomic_store_explicit(&signals->routeChanged, true, memory_order_release);
        }
    }
}

@end

void* maudIosSessionObserve(maudIosSignals* signals)
{
    @autoreleasepool
    {
        MaudAudioSessionObserver* observer = [[MaudAudioSessionObserver alloc] init];
        if (observer == nil)
        {
            return nullptr;
        }
        observer->signals = signals;
        NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
        AVAudioSession* session = [AVAudioSession sharedInstance];
        [center addObserver:observer
                   selector:@selector(interrupted:)
                       name:AVAudioSessionInterruptionNotification
                     object:session];
        [center addObserver:observer
                   selector:@selector(routeChanged:)
                       name:AVAudioSessionRouteChangeNotification
                     object:session];
        return observer;
    }
}

void maudIosSessionUnobserve(void* observer)
{
    MaudAudioSessionObserver* held = observer;
    if (held == nil)
    {
        return;
    }
    @synchronized(held)
    {
        held->signals = nullptr;
    }
    [[NSNotificationCenter defaultCenter] removeObserver:held];
    [held release];
}

// The form a route's port leads to.
static maudDeviceForm FormOf(AVAudioSessionPortDescription* port)
{
    AVAudioSessionPort type = port.portType;
    if ([type isEqualToString:AVAudioSessionPortBuiltInSpeaker])
    {
        return maud_formSpeakers;
    }
    if ([type isEqualToString:AVAudioSessionPortBuiltInReceiver])
    {
        return maud_formHandset;
    }
    if ([type isEqualToString:AVAudioSessionPortHeadphones] ||
        [type isEqualToString:AVAudioSessionPortBluetoothA2DP])
    {
        return maud_formHeadphones;
    }
    if ([type isEqualToString:AVAudioSessionPortHeadsetMic] ||
        [type isEqualToString:AVAudioSessionPortBluetoothHFP])
    {
        return maud_formHeadset;
    }
    if ([type isEqualToString:AVAudioSessionPortBuiltInMic])
    {
        return maud_formMicrophone;
    }
    if ([type isEqualToString:AVAudioSessionPortLineOut] ||
        [type isEqualToString:AVAudioSessionPortLineIn])
    {
        return maud_formLine;
    }
    if ([type isEqualToString:AVAudioSessionPortHDMI])
    {
        return maud_formDigital;
    }
    return maud_formUnknown;
}

uint32_t maudIosSessionInputs(maudIosPort* ports, uint32_t capacity)
{
    @autoreleasepool
    {
        uint32_t count = 0;
        for (AVAudioSessionPortDescription* port in [AVAudioSession sharedInstance].availableInputs)
        {
            if (count >= capacity)
            {
                break;
            }
            maudIosPort* out = &ports[count];
            snprintf(out->key, sizeof(out->key), "port:%s", port.UID.UTF8String);
            snprintf(out->name, sizeof(out->name), "%s", port.portName.UTF8String);
            out->form = FormOf(port);
            count++;
        }
        return count;
    }
}

bool maudIosSessionPreferInput(const char* uid, size_t length)
{
    @autoreleasepool
    {
        NSString* wanted = [[[NSString alloc] initWithBytes:uid
                                                     length:length
                                                   encoding:NSUTF8StringEncoding] autorelease];
        AVAudioSession* session = [AVAudioSession sharedInstance];
        for (AVAudioSessionPortDescription* port in session.availableInputs)
        {
            if ([port.UID isEqualToString:wanted])
            {
                return [session setPreferredInput:port error:nil];
            }
        }
        return false;
    }
}

void maudIosSessionRoute(maudDeviceForm* output, maudDeviceForm* input)
{
    @autoreleasepool
    {
        AVAudioSessionRouteDescription* route = [AVAudioSession sharedInstance].currentRoute;
        AVAudioSessionPortDescription* played = route.outputs.firstObject;
        AVAudioSessionPortDescription* heard = route.inputs.firstObject;
        *output = played != nil ? FormOf(played) : maud_formUnknown;
        *input = heard != nil ? FormOf(heard) : maud_formUnknown;
    }
}

int64_t maudIosSessionLatency(bool input)
{
    @autoreleasepool
    {
        AVAudioSession* session = [AVAudioSession sharedInstance];
        NSTimeInterval latency = input ? session.inputLatency : session.outputLatency;
        return (int64_t)((latency + session.IOBufferDuration) * 1e9);
    }
}

static AVAudioSessionCategory CategoryOf(const maudIos* ios, AVAudioSessionCategoryOptions* options)
{
    const maudIosSession* state = &ios->session;
    bool focused = state->focus == maud_focusLasting || state->focus == maud_focusBrief;
    bool silenced = ios->context->def.iosSilencedBySwitch;
    *options = 0;
    if (!focused)
    {
        *options |= AVAudioSessionCategoryOptionMixWithOthers;
    }
    if (state->focus == maud_focusBriefMixed)
    {
        *options |= AVAudioSessionCategoryOptionDuckOthers;
    }
    if (state->use.voiced || (state->use.inputs && state->use.outputs))
    {
        *options |= AVAudioSessionCategoryOptionDefaultToSpeaker | ALLOW_BLUETOOTH_HFP |
                    AVAudioSessionCategoryOptionAllowBluetoothA2DP;
        return AVAudioSessionCategoryPlayAndRecord;
    }
    if (state->use.inputs)
    {
        // Record takes no mixing options (only PlayAndRecord, Playback and
        // MultiRoute do): a session that only records never mixes.
        *options = ALLOW_BLUETOOTH_HFP;
        return AVAudioSessionCategoryRecord;
    }
    if (silenced)
    {
        // Ambient always mixes; SoloAmbient never does.
        AVAudioSessionCategory category =
            focused ? AVAudioSessionCategorySoloAmbient : AVAudioSessionCategoryAmbient;
        *options &= ~AVAudioSessionCategoryOptionMixWithOthers;
        return category;
    }
    return AVAudioSessionCategoryPlayback;
}

bool maudIosSessionUpdate(maudIos* ios, maudIosUse use)
{
    @autoreleasepool
    {
        maudIosSession* state = &ios->session;
        AVAudioSession* session = [AVAudioSession sharedInstance];
        bool wanted = use.running || state->focus != maud_focusRelease;
        bool changed = !state->configured || state->use.outputs != use.outputs ||
                       state->use.inputs != use.inputs || state->use.voiced != use.voiced;
        state->use = use;
        // A voiced duplex stream's unit cancels the echo of what it plays,
        // in the session's voice chat mode.
        AVAudioSessionMode mode =
            use.voiced ? AVAudioSessionModeVoiceChat : AVAudioSessionModeDefault;
        AVAudioSessionCategoryOptions options = 0;
        AVAudioSessionCategory category = CategoryOf(ios, &options);
        if (changed || ![session.category isEqualToString:category] ||
            ![session.mode isEqualToString:mode] || session.categoryOptions != options)
        {
            if (![session setCategory:category mode:mode options:options error:nil])
            {
                return false;
            }
            state->configured = true;
        }
        if (wanted && !state->active)
        {
            state->active = [session setActive:YES error:nil];
            return state->active;
        }
        if (!wanted && state->active)
        {
            BOOL done = [session setActive:NO
                               withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation
                                     error:nil];
            (void)done;
            state->active = false;
        }
        return true;
    }
}
