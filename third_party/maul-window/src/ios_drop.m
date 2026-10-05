// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Drag and drop on iOS (ios.h): each window's view has a drop
// interaction taking a drag whose items are text or files, with the copy
// operation, and reporting a drag over that has moved. An item with a
// type that is neither text nor an address is a file, loaded as that
// type; another that loads as a string is text (a text file
// dragged gives its text). A drop's
// items load asynchronously, and a file is handed over only while its
// load finishes, so each is copied into the application's temporary
// directory, a folder of its own per drop, under the name the system
// gave it (its source's suggestion, or "Document"), and the drop gives
// those paths;
// the system empties that directory in time. The first text item is the
// drop's text. The drop is posted once every item has loaded, at the
// place it was made; one whose window or program went since is dropped.

#include "ios.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <string.h>

// The type of an item's file: its first registered type that is neither
// text nor an address (a file's own address among them), or nil. A file
// of an extension the system does not know has a dynamic type.
static NSString* FileTypeOf(NSItemProvider* provider)
{
    for (NSString* identifier in provider.registeredTypeIdentifiers)
    {
        UTType* type = [UTType typeWithIdentifier:identifier];
        if (type == nil || (![type conformsToType:UTTypeText] && ![type conformsToType:UTTypeURL]))
        {
            return identifier;
        }
    }
    return nil;
}

static bool IsText(NSItemProvider* provider)
{
    return FileTypeOf(provider) == nil && [provider canLoadObjectOfClass:[NSString class]];
}

static mwinDragContents ContentsOf(id<UIDropSession> session)
{
    mwinDragContents contents = 0;
    for (UIDragItem* item in session.items)
    {
        NSItemProvider* provider = item.itemProvider;
        if (FileTypeOf(provider) != nil)
        {
            contents |= mwin_dragFiles;
        }
        else if (IsText(provider))
        {
            contents |= mwin_dragText;
        }
    }
    return contents;
}

static mwinPosition PositionOf(UIView* view, id<UIDropSession> session)
{
    CGPoint point = [session locationInView:view];
    return (mwinPosition){(float)point.x, (float)point.y};
}

static void PostDrag(mwinIOSPlatform* platform, uint32_t slot, mwinEventType type)
{
    mwinIOSWindow* window = &platform->windows[slot];
    mwinEvent event = {.type = type, .timeNs = mwinIOSNow()};
    event.data.drag = (mwinDragEvent){window->dragPosition, window->dragContents};
    mwinPost(platform->context, slot, &event);
}

// Copies a loaded file into a folder of the drop's own, keeping the name
// the system gave it (the one its source suggests); nil when it cannot.
static NSString* Keep(NSURL* loaded, NSString* folder, NSUInteger index)
{
    NSFileManager* files = [NSFileManager defaultManager];
    NSString* place = [folder stringByAppendingPathComponent:[@(index) stringValue]];
    NSString* name = loaded.lastPathComponent.length > 0 ? loaded.lastPathComponent : @"file";
    NSString* path = [place stringByAppendingPathComponent:name];
    bool made = [files createDirectoryAtPath:place
                 withIntermediateDirectories:YES
                                  attributes:nil
                                       error:nullptr];
    return made && [files copyItemAtURL:loaded toURL:[NSURL fileURLWithPath:path] error:nullptr]
               ? path
               : nil;
}

// A dropper: the drop interaction's delegate, for a window's view.
@interface MwinIOSDropper : NSObject <UIDropInteractionDelegate>
{
  @public
    mwinIOSPlatform* platform;
    uint32_t slot;
    UIView* view;
}
@end

@implementation MwinIOSDropper
- (BOOL)dropInteraction:(UIDropInteraction*)interaction canHandleSession:(id<UIDropSession>)session
{
    (void)interaction;
    return platform != nullptr && ContentsOf(session) != 0;
}

- (void)dropInteraction:(UIDropInteraction*)interaction sessionDidEnter:(id<UIDropSession>)session
{
    (void)interaction;
    if (platform == nullptr)
    {
        return;
    }
    mwinIOSWindow* window = &platform->windows[slot];
    window->dragContents = ContentsOf(session);
    if (window->dragContents != 0)
    {
        window->dragPosition = PositionOf(view, session);
        PostDrag(platform, slot, mwin_eventDragEntered);
    }
}

- (UIDropProposal*)dropInteraction:(UIDropInteraction*)interaction
                  sessionDidUpdate:(id<UIDropSession>)session
{
    (void)interaction;
    mwinIOSWindow* window = platform != nullptr ? &platform->windows[slot] : nullptr;
    if (window == nullptr || window->dragContents == 0)
    {
        return [[[UIDropProposal alloc] initWithDropOperation:UIDropOperationCancel] autorelease];
    }
    mwinPosition position = PositionOf(view, session);
    if (position.x != window->dragPosition.x || position.y != window->dragPosition.y)
    {
        window->dragPosition = position;
        PostDrag(platform, slot, mwin_eventDragMoved);
    }
    return [[[UIDropProposal alloc] initWithDropOperation:UIDropOperationCopy] autorelease];
}

- (void)dropInteraction:(UIDropInteraction*)interaction sessionDidExit:(id<UIDropSession>)session
{
    (void)interaction;
    (void)session;
    if (platform == nullptr)
    {
        return;
    }
    mwinIOSWindow* window = &platform->windows[slot];
    if (window->dragContents != 0)
    {
        PostDrag(platform, slot, mwin_eventDragLeft);
    }
    window->dragContents = 0;
}

- (void)dropInteraction:(UIDropInteraction*)interaction performDrop:(id<UIDropSession>)session
{
    (void)interaction;
    if (platform == nullptr || platform->windows[slot].dragContents == 0)
    {
        return;
    }
    platform->windows[slot].dragContents = 0;
    mwinContext* context = platform->context;
    mwinPosition position = PositionOf(view, session);
    NSString* folder = [NSTemporaryDirectory()
        stringByAppendingPathComponent:[@"maul-window-drops"
                                           stringByAppendingPathComponent:NSUUID.UUID.UUIDString]];
    NSArray<UIDragItem*>* items = session.items;
    // Per item, its path or text once loaded, in the items' order.
    NSMutableArray* loaded = [NSMutableArray arrayWithCapacity:items.count];
    dispatch_group_t group = dispatch_group_create();
    for (NSUInteger i = 0; i < items.count; i++)
    {
        [loaded addObject:[NSNull null]];
        NSItemProvider* provider = items[i].itemProvider;
        void (^settle)(id) = ^(id value) {
          dispatch_async(dispatch_get_main_queue(), ^{
            if (value != nil)
            {
                loaded[i] = value;
            }
            dispatch_group_leave(group);
          });
        };
        NSString* type = FileTypeOf(provider);
        if (type == nil && IsText(provider))
        {
            dispatch_group_enter(group);
            [provider loadObjectOfClass:[NSString class]
                      completionHandler:^(id<NSItemProviderReading> text, NSError* error) {
                        (void)error;
                        settle(@[ text != nil ? (NSString*)text : @"" ]);
                      }];
        }
        else if (type != nil)
        {
            dispatch_group_enter(group);
            [provider loadFileRepresentationForTypeIdentifier:type
                                            completionHandler:^(NSURL* file, NSError* error) {
                                              (void)error;
                                              // The file goes once this returns.
                                              settle(file != nil ? Keep(file, folder, i) : nil);
                                            }];
        }
    }
    mwinIOSPlatform* platformThen = platform;
    uint32_t slotThen = slot;
    UIView* viewThen = view;
    dispatch_group_notify(group, dispatch_get_main_queue(), ^{
      if (mwinIOSCurrentContext() != context || platformThen->windows[slotThen].view != viewThen)
      {
          return;
      }
      mwinBeginDrop(context);
      bool texted = false;
      for (id value in loaded)
      {
          // A file's path, or text in an array of one.
          if ([value isKindOfClass:[NSString class]])
          {
              const char* path = ((NSString*)value).UTF8String;
              mwinAddDroppedFile(context, path, strlen(path));
          }
          else if ([value isKindOfClass:[NSArray class]] && !texted)
          {
              const char* text = ((NSString*)((NSArray*)value).firstObject).UTF8String;
              mwinSetDroppedText(context, text, strlen(text));
              texted = true;
          }
      }
      mwinFinishDrop(context, slotThen, position, mwinIOSNow());
    });
    dispatch_release(group);
}
@end

id mwinIOSWatchDrops(mwinIOSPlatform* platform, uint32_t slot, UIView* view)
{
    MwinIOSDropper* dropper = [[MwinIOSDropper alloc] init];
    dropper->platform = platform;
    dropper->slot = slot;
    dropper->view = view;
    UIDropInteraction* interaction = [[UIDropInteraction alloc] initWithDelegate:dropper];
    [view addInteraction:interaction];
    [interaction release];
    return dropper;
}

void mwinIOSForgetDrops(id dropper)
{
    ((MwinIOSDropper*)dropper)->platform = nullptr;
}
