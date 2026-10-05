// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Keys, text and the on-screen keyboard on iOS (ios.h).
//
// - A hardware keyboard's presses reach the window's view controller
//   (iOS 13.4): a UIKey's code is the USB HID usage, which is the
//   contract's key code; its meaning the character it types without
//   modifiers, lower-cased, or MWIN_KEY_NAMED with the code for a key
//   that types none. UIKit does not repeat presses. iOS offers no way to
//   ask a layout what a key types, so a printing key's meaning is known
//   once it was pressed under the current layout; the others are named.
// - The view is a text input client (ios_text.m) and the first responder
//   while its window shows, so text is typed into it, as on the other
//   platforms, whether or not the window accepts text: a hardware key's
//   text, or the on-screen keyboard's. A newline, a tab and a deletion
//   from the on-screen keyboard, which comes with no press, become the
//   Enter, Tab and Backspace keys' press and release.
// - The on-screen keyboard shows while the program asks for it: the
//   view's input view is otherwise empty, which keeps it away. The
//   purpose sets the keyboard's type; nothing is corrected, capitalized
//   or completed. The part of the window it covers is posted when its
//   frame changes.

#include "ios.h"
#include "key_prints.h"

#include <string.h>

enum
{
    codeEnter = 40,
    codeBackspace = 42,
    codeTab = 43,
};

static void Post(mwinIOSPlatform* platform, uint32_t slot, mwinEvent* event)
{
    event->timeNs = mwinIOSNow();
    mwinPost(platform->context, slot, event);
}

static bool Held(const mwinIOSPlatform* platform, mwinKeyCode code)
{
    return (platform->held[code / 8] & (1u << (code % 8))) != 0;
}

static void Hold(mwinIOSPlatform* platform, mwinKeyCode code, bool down)
{
    uint8_t bit = (uint8_t)(1u << (code % 8));
    platform->held[code / 8] =
        down ? (platform->held[code / 8] | bit) : (platform->held[code / 8] & (uint8_t)~bit);
}

// The character a key types without modifiers, or 0: one character,
// not a UIKeyInput name such as UIKeyInputLeftArrow, nor one of the
// private use characters of function keys.
static mwinKey CharacterOf(NSString* characters)
{
    NSString* lower = characters.lowercaseString;
    if (lower.length == 0 || lower.length > 2)
    {
        return 0;
    }
    uint32_t unit = [lower characterAtIndex:0];
    if (lower.length == 2)
    {
        uint32_t low = [lower characterAtIndex:1];
        bool pair = unit >= 0xD800 && unit <= 0xDBFF && low >= 0xDC00 && low <= 0xDFFF;
        return pair ? 0x10000u + ((unit - 0xD800u) << 10) + (low - 0xDC00u) : 0;
    }
    bool printing = unit >= 0x20 && unit != 0x7F && (unit < 0xD800 || unit > 0xDFFF) &&
                    (unit < 0xF700 || unit > 0xF8FF);
    return printing ? unit : 0;
}

mwinKey mwinIOSMapKeyCode(const mwinIOSPlatform* platform, mwinKeyCode code)
{
    if (code >= MWIN_IOS_KEY_CODES)
    {
        return 0;
    }
    return mwinKeyPrints(code) ? platform->meanings[code] : MWIN_KEY_NAMED | code;
}

static void PostKey(mwinIOSPlatform* platform, uint32_t slot, mwinEventType type, mwinKeyCode code,
                    mwinKey key, mwinModifiers modifiers)
{
    mwinEvent event = {.type = type};
    event.data.key = (mwinKeyEvent){code, modifiers, key, false};
    Post(platform, slot, &event);
}

void mwinIOSPresses(mwinIOSPlatform* platform, uint32_t slot, NSSet<UIPress*>* presses, bool down)
{
    for (UIPress* press in presses)
    {
        UIKey* key = press.key;
        if (key == nil || (NSInteger)key.keyCode <= 0 || key.keyCode >= MWIN_IOS_KEY_CODES)
        {
            continue;
        }
        mwinKeyCode code = (mwinKeyCode)key.keyCode;
        mwinKey meaning = MWIN_KEY_NAMED | code;
        if (mwinKeyPrints(code))
        {
            mwinKey typed = CharacterOf(key.charactersIgnoringModifiers);
            meaning = typed != 0 ? typed : platform->meanings[code];
            platform->meanings[code] = meaning;
        }
        // A release of a key no press was seen of is left out.
        if (!down && !Held(platform, code))
        {
            continue;
        }
        Hold(platform, code, down);
        PostKey(platform, slot, down ? mwin_eventKeyDown : mwin_eventKeyUp, code, meaning,
                mwinIOSModifiersOf(key.modifierFlags));
    }
}

// A key the on-screen keyboard gave only as text: its press and release,
// unless a hardware key's press already came.
static void Tap(mwinIOSPlatform* platform, uint32_t slot, mwinKeyCode code)
{
    if (Held(platform, code))
    {
        return;
    }
    PostKey(platform, slot, mwin_eventKeyDown, code, MWIN_KEY_NAMED | code, 0);
    PostKey(platform, slot, mwin_eventKeyUp, code, MWIN_KEY_NAMED | code, 0);
}

static void PostText(mwinIOSPlatform* platform, uint32_t slot, NSMutableString* kept)
{
    const char* bytes = kept.UTF8String;
    size_t length = bytes != nullptr ? strlen(bytes) : 0;
    if (length > 0 && length <= UINT32_MAX)
    {
        mwinEvent event = {.type = mwin_eventTextInput};
        event.data.text = (mwinTextEvent){bytes, (uint32_t)length};
        Post(platform, slot, &event);
    }
    [kept setString:@""];
}

void mwinIOSInsertText(mwinIOSPlatform* platform, uint32_t slot, NSString* text)
{
    NSMutableString* kept = [NSMutableString stringWithCapacity:text.length];
    for (NSUInteger i = 0; i < text.length; i++)
    {
        unichar unit = [text characterAtIndex:i];
        if (unit == '\n' || unit == '\r' || unit == '\t')
        {
            PostText(platform, slot, kept);
            Tap(platform, slot, unit == '\t' ? codeTab : codeEnter);
        }
        else if (unit >= 0x20 && unit != 0x7F && (unit < 0xF700 || unit > 0xF8FF))
        {
            CFStringAppendCharacters((CFMutableStringRef)kept, &unit, 1);
        }
    }
    PostText(platform, slot, kept);
}

void mwinIOSDeleteBackward(mwinIOSPlatform* platform, uint32_t slot)
{
    Tap(platform, slot, codeBackspace);
}

// The on-screen keyboard's type for a purpose.
UIKeyboardType mwinIOSKeyboardTypeOf(mwinInputPurpose purpose)
{
    switch (purpose)
    {
    case mwin_purposeNumber:
        return UIKeyboardTypeDecimalPad;
    case mwin_purposeEmail:
        return UIKeyboardTypeEmailAddress;
    case mwin_purposeUrl:
        return UIKeyboardTypeURL;
    default:
        return UIKeyboardTypeDefault;
    }
}

mwinOutcome mwinIOSSetTextInput(mwinIOSPlatform* platform, uint32_t slot, bool enabled,
                                mwinRect caret)
{
    mwinIOSWindow* window = &platform->windows[slot];
    if (!enabled && window->textInput)
    {
        // The composition is dropped, not accepted.
        mwinIOSEndComposition(platform, slot, false);
    }
    window->textInput = enabled;
    window->caret = caret;
    return mwin_outcomeDone;
}

mwinOutcome mwinIOSSetVirtualKeyboard(mwinIOSPlatform* platform, uint32_t slot, bool visible,
                                      mwinInputPurpose purpose)
{
    mwinIOSWindow* window = &platform->windows[slot];
    window->keyboard = visible;
    window->purpose = purpose;
    UIView* view = window->view;
    if (!view.isFirstResponder)
    {
        [view becomeFirstResponder];
    }
    // The input view and the traits are read again.
    [view reloadInputViews];
    return mwin_outcomeDone;
}

// The part of each window the keyboard now covers, where it changed.
static void KeyboardMoved(mwinIOSPlatform* platform, NSNotification* note)
{
    CGRect frame = [note.userInfo[UIKeyboardFrameEndUserInfoKey] CGRectValue];
    for (uint32_t slot = 0; slot < platform->context->limits.windows; slot++)
    {
        mwinIOSWindow* window = &platform->windows[slot];
        UIScreen* screen = window->scene.screen;
        if (window->view == nil || screen == nil)
        {
            continue;
        }
        CGRect covered = CGRectIntersection([window->view convertRect:frame
                                                  fromCoordinateSpace:screen.coordinateSpace],
                                            window->view.bounds);
        mwinRect rect = CGRectIsNull(covered) || CGRectIsEmpty(covered)
                            ? (mwinRect){0}
                            : (mwinRect){(float)covered.origin.x, (float)covered.origin.y,
                                         (float)covered.size.width, (float)covered.size.height};
        if (memcmp(&rect, &window->covered, sizeof(rect)) == 0)
        {
            continue;
        }
        window->covered = rect;
        mwinEvent event = {.type = mwin_eventVirtualKeyboardChanged};
        event.data.rect = rect;
        Post(platform, slot, &event);
    }
}

mwinResult mwinIOSKeyboardLayout(const mwinIOSPlatform* platform, char* buffer, size_t capacity,
                                 size_t* lengthOut)
{
    size_t length = 0;
    @autoreleasepool
    {
        UITextInputMode* mode = nil;
        for (uint32_t i = 0; mode == nil && i < platform->context->limits.windows; i++)
        {
            UIView* view = platform->windows[i].view;
            mode = view.isFirstResponder ? view.textInputMode : nil;
        }
        mode = mode != nil ? mode : UITextInputMode.activeInputModes.firstObject;
        const char* bytes = mode.primaryLanguage.UTF8String;
        length = bytes != nullptr ? strlen(bytes) : 0;
        if (capacity > 0 && length > 0)
        {
            memcpy(buffer, bytes, length < capacity ? length : capacity);
        }
    }
    *lengthOut = length;
    return length > capacity ? mwin_errorCapacity : mwin_success;
}

void mwinIOSWatchKeyboard(mwinIOSPlatform* platform)
{
    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
    platform->keyboardObservers[0] =
        [[center addObserverForName:UIKeyboardDidChangeFrameNotification
                             object:nil
                              queue:nil
                         usingBlock:^(NSNotification* note) {
                           KeyboardMoved(platform, note);
                         }] retain];
    platform->keyboardObservers[1] =
        [[center addObserverForName:UITextInputCurrentInputModeDidChangeNotification
                             object:nil
                              queue:nil
                         usingBlock:^(NSNotification* note) {
                           (void)note;
                           // What the keys type is learned again.
                           memset(platform->meanings, 0, sizeof(platform->meanings));
                           mwinEvent event = {.type = mwin_eventKeyboardLayoutChanged,
                                              .timeNs = mwinIOSNow()};
                           mwinPostGlobal(platform->context, &event);
                         }] retain];
}

void mwinIOSUnwatchKeyboard(mwinIOSPlatform* platform)
{
    for (size_t i = 0; i < sizeof(platform->keyboardObservers) / sizeof(id); i++)
    {
        if (platform->keyboardObservers[i] != nil)
        {
            [[NSNotificationCenter defaultCenter] removeObserver:platform->keyboardObservers[i]];
            [platform->keyboardObservers[i] release];
            platform->keyboardObservers[i] = nil;
        }
    }
}
