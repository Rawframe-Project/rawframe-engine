// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// macOS keys (macos.h): the physical key of each virtual key code, what
// the current layout makes of a key with no modifier (UCKeyTranslate on
// the keyboard input source's layout data), the modifiers of an event,
// the layout's name, and what watches the keyboard: the layout's changes
// and the releases AppKit keeps from the window while Command is held.

#include "macos.h"

#import <Carbon/Carbon.h>
#include <string.h>

// The key code of each virtual key code (Carbon's kVK_ values), from
// 0x00 to 0x7F; 0 where the key has none (Fn, the volume keys). Help,
// where Insert is on other keyboards, is Insert; the keypad's Clear,
// where Num Lock is, is Num Lock. On ISO keyboards macOS gives the key
// left of 1 and the key right of the left Shift each other's codes,
// which CodeOf turns back.
// clang-format off
static const uint8_t s_codes[128] = {
      4,  22,   7,   9,  11,  10,  29,  27,   6,  25, 100,   5,  20,  26,   8,  21,
     28,  23,  30,  31,  32,  33,  35,  34,  46,  38,  36,  45,  37,  39,  48,  18,
     24,  47,  12,  19,  40,  15,  13,  52,  14,  51,  49,  54,  56,  17,  16,  55,
     43,  44,  53,  42,   0,  41, 231, 227, 225,  57, 226, 224, 229, 230, 228,   0,
    108,  99,   0,  85,   0,  87,   0,  83,   0,   0,   0,  84,  88,   0,  86, 109,
    110, 103,  98,  89,  90,  91,  92,  93,  94,  95, 111,  96,  97, 137, 135, 133,
     62,  63,  64,  60,  65,  66, 145,  68, 144, 104, 107, 105,   0,  67, 101,  69,
      0, 106,  73,  74,  75,  76,  61,  77,  59,  78,  58,  80,  79,  81,  82,   0,
};
// clang-format on

// The virtual key codes ISO keyboards swap: the section sign key and
// the grave accent key.
#define VK_ISO_SECTION 0x0A
#define VK_GRAVE       0x32

// Each modifier key's own bit in an event's flags (IOKit's device
// masks), which tells a press from a release in flagsChanged:.
enum
{
    deviceControlLeft = 0x0001,
    deviceShiftLeft = 0x0002,
    deviceShiftRight = 0x0004,
    deviceMetaLeft = 0x0008,
    deviceMetaRight = 0x0010,
    deviceAltLeft = 0x0020,
    deviceAltRight = 0x0040,
    deviceControlRight = 0x2000,
};

static bool IsIso(void)
{
    return KBGetLayoutType(LMGetKbdType()) == kKeyboardISO;
}

static mwinKeyCode CodeOf(uint16_t virtualKey, bool iso)
{
    if (iso && (virtualKey == VK_ISO_SECTION || virtualKey == VK_GRAVE))
    {
        virtualKey = virtualKey == VK_ISO_SECTION ? VK_GRAVE : VK_ISO_SECTION;
    }
    return virtualKey < 128 ? s_codes[virtualKey] : mwin_codeUnknown;
}

mwinKeyCode mwinMacCodeOf(uint16_t virtualKey)
{
    return CodeOf(virtualKey, IsIso());
}

// The layout data of the current keyboard input source, or of the
// current ASCII-capable one when it has none (an input method's).
static CFDataRef LayoutDataOf(TISInputSourceRef source)
{
    return source != nullptr
               ? (CFDataRef)TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData)
               : nullptr;
}

static void ReadLayout(mwinMacPlatform* platform)
{
    if (platform->layout != nullptr)
    {
        CFRelease(platform->layout);
    }
    TISInputSourceRef source = TISCopyCurrentKeyboardLayoutInputSource();
    if (LayoutDataOf(source) == nullptr)
    {
        if (source != nullptr)
        {
            CFRelease(source);
        }
        source = TISCopyCurrentASCIICapableKeyboardLayoutInputSource();
    }
    platform->layout = source;
}

mwinKey mwinMacMeaningOf(const mwinMacPlatform* platform, uint16_t virtualKey, mwinKeyCode code)
{
    CFDataRef data = LayoutDataOf((TISInputSourceRef)platform->layout);
    UniChar text[4];
    UniCharCount count = 0;
    UInt32 deadState = 0;
    // The display action with dead keys off gives a dead key's accent.
    if (data != nullptr &&
        UCKeyTranslate((const UCKeyboardLayout*)CFDataGetBytePtr(data), virtualKey,
                       kUCKeyActionDisplay, 0, LMGetKbdType(), kUCKeyTranslateNoDeadKeysMask,
                       &deadState, 4, &count, text) == noErr &&
        count != 0 && text[0] >= 0x20 && text[0] != 0x7F && (text[0] < 0xD800 || text[0] > 0xDFFF))
    {
        return text[0];
    }
    return MWIN_KEY_NAMED | code;
}

mwinModifiers mwinMacModifiersOf(NSEventModifierFlags flags)
{
    mwinModifiers modifiers = 0;
    modifiers |= (flags & NSEventModifierFlagShift) != 0 ? mwin_modShift : 0;
    modifiers |= (flags & NSEventModifierFlagControl) != 0 ? mwin_modControl : 0;
    modifiers |= (flags & NSEventModifierFlagOption) != 0 ? mwin_modAlt : 0;
    modifiers |= (flags & NSEventModifierFlagCommand) != 0 ? mwin_modMeta : 0;
    modifiers |= (flags & NSEventModifierFlagCapsLock) != 0 ? mwin_modCapsLock : 0;
    return modifiers;
}

int mwinMacModifierChange(mwinKeyCode code, NSEventModifierFlags flags)
{
    uint32_t bit = 0;
    switch (code)
    {
    case mwin_codeControlLeft:
        bit = deviceControlLeft;
        break;
    case mwin_codeShiftLeft:
        bit = deviceShiftLeft;
        break;
    case mwin_codeShiftRight:
        bit = deviceShiftRight;
        break;
    case mwin_codeMetaLeft:
        bit = deviceMetaLeft;
        break;
    case mwin_codeMetaRight:
        bit = deviceMetaRight;
        break;
    case mwin_codeAltLeft:
        bit = deviceAltLeft;
        break;
    case mwin_codeAltRight:
        bit = deviceAltRight;
        break;
    case mwin_codeControlRight:
        bit = deviceControlRight;
        break;
    case mwin_codeCapsLock:
        // macOS tells only that Caps Lock toggled.
        return 2;
    default:
        return -1;
    }
    return (flags & bit) != 0 ? 1 : 0;
}

mwinKey mwinMacMapKeyCode(const mwinMacPlatform* platform, mwinKeyCode code)
{
    bool iso = IsIso();
    for (uint16_t virtualKey = 0; virtualKey < 128 && code != mwin_codeUnknown; virtualKey++)
    {
        if (CodeOf(virtualKey, iso) == code)
        {
            return mwinMacMeaningOf(platform, virtualKey, code);
        }
    }
    return MWIN_KEY_NAMED | code;
}

mwinResult mwinMacKeyboardLayout(const mwinMacPlatform* platform, char* buffer, size_t capacity,
                                 size_t* lengthOut)
{
    size_t length = 0;
    @autoreleasepool
    {
        TISInputSourceRef source = TISCopyCurrentKeyboardLayoutInputSource();
        NSString* name =
            source != nullptr
                ? (NSString*)TISGetInputSourceProperty(source, kTISPropertyLocalizedName)
                : nil;
        const char* bytes = name.UTF8String;
        length = bytes != nullptr ? strlen(bytes) : 0;
        if (capacity > 0 && length > 0)
        {
            memcpy(buffer, bytes, length < capacity ? length : capacity);
        }
        if (source != nullptr)
        {
            CFRelease(source);
        }
    }
    (void)platform;
    *lengthOut = length;
    return length > capacity ? mwin_errorCapacity : mwin_success;
}

void mwinMacWatchKeyboard(mwinMacPlatform* platform)
{
    ReadLayout(platform);
    platform->layoutObserver = [[[NSNotificationCenter defaultCenter]
        addObserverForName:NSTextInputContextKeyboardSelectionDidChangeNotification
                    object:nil
                     queue:nil
                usingBlock:^(NSNotification* note) {
                  (void)note;
                  ReadLayout(platform);
                  mwinEvent event = {.type = mwin_eventKeyboardLayoutChanged,
                                     .timeNs = mwinMacNow()};
                  mwinPostGlobal(platform->context, &event);
                }] retain];
    // AppKit sends no key release to the window while Command is held,
    // taking it for a menu's key equivalent.
    platform->keyUpMonitor = [[NSEvent
        addLocalMonitorForEventsMatchingMask:NSEventMaskKeyUp
                                     handler:^NSEvent*(NSEvent* event) {
                                       if ((event.modifierFlags & NSEventModifierFlagCommand) != 0)
                                       {
                                           [event.window sendEvent:event];
                                       }
                                       return event;
                                     }] retain];
}

void mwinMacUnwatchKeyboard(mwinMacPlatform* platform)
{
    if (platform->keyUpMonitor != nil)
    {
        [NSEvent removeMonitor:platform->keyUpMonitor];
        [platform->keyUpMonitor release];
    }
    if (platform->layoutObserver != nil)
    {
        [[NSNotificationCenter defaultCenter] removeObserver:platform->layoutObserver];
        [platform->layoutObserver release];
    }
    if (platform->layout != nullptr)
    {
        CFRelease(platform->layout);
    }
}
