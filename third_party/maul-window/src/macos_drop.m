// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// macOS drag and drop (macos.h): each window's view is a dragging
// destination for file URLs and text, taking a drag that carries either
// with the copy operation and reporting a drag over that has moved. A
// drop's files are the file URLs' paths, and its text the pasteboard's
// string.

#include "macos.h"

#include <string.h>

static NSDictionary* FilesOnly(void)
{
    return @{NSPasteboardURLReadingFileURLsOnlyKey : @YES};
}

static mwinDragContents ContentsOf(NSPasteboard* pasteboard)
{
    bool files = [pasteboard canReadObjectForClasses:@[ [NSURL class] ] options:FilesOnly()];
    bool text = [pasteboard availableTypeFromArray:@[ NSPasteboardTypeString ]] != nil;
    return (mwinDragContents)((files ? mwin_dragFiles : 0) | (text ? mwin_dragText : 0));
}

static mwinPosition PositionOf(const mwinMacWindow* window, id<NSDraggingInfo> drag)
{
    NSPoint point = [window->view convertPoint:drag.draggingLocation fromView:nil];
    return (mwinPosition){(float)point.x, (float)point.y};
}

static void PostDrag(mwinMacPlatform* platform, uint32_t slot, mwinEventType type)
{
    mwinMacWindow* window = &platform->windows[slot];
    mwinEvent event = {.type = type, .timeNs = mwinMacNow()};
    event.data.drag = (mwinDragEvent){window->dragPosition, window->dragContents};
    mwinPost(platform->context, slot, &event);
}

NSDragOperation mwinMacDragEntered(mwinMacPlatform* platform, uint32_t slot,
                                   id<NSDraggingInfo> drag)
{
    mwinMacWindow* window = &platform->windows[slot];
    window->dragContents = ContentsOf(drag.draggingPasteboard);
    if (window->dragContents == 0)
    {
        return NSDragOperationNone;
    }
    window->dragPosition = PositionOf(window, drag);
    PostDrag(platform, slot, mwin_eventDragEntered);
    return NSDragOperationCopy;
}

// AppKit calls it again and again while the drag rests.
NSDragOperation mwinMacDragUpdated(mwinMacPlatform* platform, uint32_t slot,
                                   id<NSDraggingInfo> drag)
{
    mwinMacWindow* window = &platform->windows[slot];
    if (window->dragContents == 0)
    {
        return NSDragOperationNone;
    }
    mwinPosition position = PositionOf(window, drag);
    if (position.x != window->dragPosition.x || position.y != window->dragPosition.y)
    {
        window->dragPosition = position;
        PostDrag(platform, slot, mwin_eventDragMoved);
    }
    return NSDragOperationCopy;
}

void mwinMacDragExited(mwinMacPlatform* platform, uint32_t slot)
{
    mwinMacWindow* window = &platform->windows[slot];
    if (window->dragContents != 0)
    {
        PostDrag(platform, slot, mwin_eventDragLeft);
    }
    window->dragContents = 0;
}

bool mwinMacDrop(mwinMacPlatform* platform, uint32_t slot, id<NSDraggingInfo> drag)
{
    mwinMacWindow* window = &platform->windows[slot];
    mwinContext* context = platform->context;
    if (window->dragContents == 0)
    {
        return false;
    }
    window->dragContents = 0;
    NSPasteboard* pasteboard = drag.draggingPasteboard;
    mwinBeginDrop(context);
    for (NSURL* file in [pasteboard readObjectsForClasses:@[ [NSURL class] ] options:FilesOnly()])
    {
        const char* path = file.path.UTF8String;
        if (path != nullptr)
        {
            mwinAddDroppedFile(context, path, strlen(path));
        }
    }
    const char* text = [pasteboard stringForType:NSPasteboardTypeString].UTF8String;
    if (text != nullptr)
    {
        mwinSetDroppedText(context, text, strlen(text));
    }
    mwinFinishDrop(context, slot, PositionOf(window, drag), mwinMacNow());
    return true;
}
