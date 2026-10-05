// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// macOS file dialogs (macos.h): an open or save panel, as a sheet on its
// window, or a panel of its own while the window is hidden. A panel
// never blocks: frames go on while it shows, and it answers its request
// when the user is done. Filters are a menu under the panel, the first
// chosen at first, each allowing the content types of its extensions.
// A panel whose request goes (its window destroyed, the program
// stopping) is closed at the next pump, and answers nothing.

#include "answer.h"
#include "dialog.h"
#include "macos.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <string.h>

// A panel and the request it answers; its platform is nullptr once the
// backend stopped.
@interface MwinMacDialog : NSObject
{
  @public
    mwinMacPlatform* platform;
    mwinServiceAnswer to;
    NSSavePanel* panel;
    // Per filter, the content types it allows.
    NSArray* filterTypes;
    bool closing;
}
- (void)chooseFilter:(NSPopUpButton*)menu;
@end

@implementation MwinMacDialog
- (void)chooseFilter:(NSPopUpButton*)menu
{
    if (@available(macOS 11.0, *))
    {
        panel.allowedContentTypes = filterTypes[(NSUInteger)menu.indexOfSelectedItem];
    }
}

- (void)dealloc
{
    [panel release];
    [filterTypes release];
    [super dealloc];
}
@end

static NSString* StringOf(const char* text, uint32_t length)
{
    return [[[NSString alloc] initWithBytes:text length:length
                                   encoding:NSUTF8StringEncoding] autorelease];
}

// The content types of a filter's extensions, "png;jpg".
static NSArray* TypesOf(const mwinDialogFilter* filter) API_AVAILABLE(macos(11.0))
{
    NSMutableArray* types = [NSMutableArray array];
    for (NSString* extension in [@(filter->extensions) componentsSeparatedByString:@";"])
    {
        UTType* type = [UTType typeWithFilenameExtension:extension];
        if (type != nil)
        {
            [types addObject:type];
        }
    }
    return types;
}

// The filters' menu under the panel, and the first filter's types.
static void Filter(MwinMacDialog* dialog, const mwinDialogCopy* copy)
{
    if (@available(macOS 11.0, *))
    {
        NSMutableArray* types = [NSMutableArray array];
        NSPopUpButton* menu = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
        for (uint32_t i = 0; i < copy->filterCount; i++)
        {
            [types addObject:TypesOf(&copy->filters[i])];
            [menu addItemWithTitle:@(copy->filters[i].name)];
        }
        [menu sizeToFit];
        menu.target = dialog;
        menu.action = @selector(chooseFilter:);
        dialog->filterTypes = [types copy];
        dialog->panel.accessoryView = menu;
        dialog->panel.allowedContentTypes = types[0];
        if ([dialog->panel isKindOfClass:[NSOpenPanel class]])
        {
            ((NSOpenPanel*)dialog->panel).accessoryViewDisclosed = YES;
        }
        [menu release];
    }
}

static NSSavePanel* PanelOf(mwinDialogKind kind)
{
    if (kind == mwin_dialogSave)
    {
        NSSavePanel* panel = [NSSavePanel savePanel];
        panel.canCreateDirectories = YES;
        return panel;
    }
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = kind != mwin_dialogFolder;
    panel.canChooseDirectories = kind == mwin_dialogFolder;
    panel.canCreateDirectories = kind == mwin_dialogFolder;
    panel.allowsMultipleSelection = kind == mwin_dialogOpenMany;
    return panel;
}

// Answers a panel's request with how it ended, the paths it chose when
// done; one whose request went, or whose backend stopped, answers
// nothing.
static void Finish(MwinMacDialog* dialog, mwinOutcome outcome)
{
    mwinMacPlatform* platform = dialog->platform;
    if (platform == nullptr)
    {
        return;
    }
    [platform->dialogs removeObject:dialog];
    mwinContext* context = platform->context;
    if (mwinAnswerRequest(context, &dialog->to) == nullptr)
    {
        return;
    }
    mwinBeginDialog(context);
    // A save panel has its URL only once confirmed.
    NSArray<NSURL*>* chosen = @[];
    if (outcome == mwin_outcomeDone)
    {
        NSURL* saved = dialog->panel.URL;
        chosen = [dialog->panel isKindOfClass:[NSOpenPanel class]]
                     ? ((NSOpenPanel*)dialog->panel).URLs
                 : saved != nil ? @[ saved ]
                                : @[];
    }
    for (NSURL* file in chosen)
    {
        const char* path = file.path.UTF8String;
        if (path != nullptr)
        {
            mwinAddDialogFile(context, path, strlen(path));
        }
    }
    outcome = mwinSettleDialog(context, dialog->to.slot, dialog->to.request, outcome);
    mwinAnswer(context, &dialog->to, outcome);
}

int mwinMacAskDialog(mwinMacPlatform* platform, uint32_t slot, uint32_t request)
{
    mwinContext* context = platform->context;
    const mwinDialogCopy* copy = context->windows[slot].requests[request].value.dialog;
    MwinMacDialog* dialog = [[[MwinMacDialog alloc] init] autorelease];
    dialog->platform = platform;
    dialog->to = mwinAnswerTo(context, slot, request);
    dialog->panel = [PanelOf(copy->kind) retain];
    NSSavePanel* panel = dialog->panel;
    if (copy->titleLength > 0)
    {
        panel.title = StringOf(copy->title, copy->titleLength);
        panel.message = panel.title;
    }
    if (copy->folderLength > 0)
    {
        panel.directoryURL = [NSURL fileURLWithPath:StringOf(copy->folder, copy->folderLength)
                                        isDirectory:YES];
    }
    if (copy->kind == mwin_dialogSave && copy->nameLength > 0)
    {
        panel.nameFieldStringValue = StringOf(copy->name, copy->nameLength);
    }
    if (copy->kind != mwin_dialogFolder && copy->filterCount > 0)
    {
        Filter(dialog, copy);
    }
    if (platform->dialogs == nil)
    {
        platform->dialogs = [[NSMutableArray alloc] init];
    }
    [platform->dialogs addObject:dialog];
    void (^done)(NSModalResponse) = ^(NSModalResponse response) {
      Finish(dialog, response == NSModalResponseOK ? mwin_outcomeDone : mwin_outcomeCancelled);
    };
    NSWindow* window = platform->windows[slot].window;
    if (window.visible)
    {
        [panel beginSheetModalForWindow:window completionHandler:done];
    }
    else
    {
        [panel beginWithCompletionHandler:done];
    }
    return -1;
}

// Cancels a panel once. Panels are another process's now, whose own
// cancel raises: a sheet is ended by its window, which calls its
// handler; a panel of its own is closed and finished here. Its handler
// answers nothing if its request went, and finishing twice does nothing.
static void Close(MwinMacDialog* dialog)
{
    if (dialog->closing)
    {
        return;
    }
    dialog->closing = true;
    NSWindow* parent = dialog->panel.sheetParent;
    if (parent != nil)
    {
        [parent endSheet:dialog->panel returnCode:NSModalResponseCancel];
        return;
    }
    [dialog->panel close];
    Finish(dialog, mwin_outcomeCancelled);
}

void mwinMacPumpDialogs(mwinMacPlatform* platform)
{
    for (MwinMacDialog* dialog in [[platform->dialogs copy] autorelease])
    {
        if (mwinAnswerRequest(platform->context, &dialog->to) == nullptr)
        {
            Close(dialog);
        }
    }
}

void mwinMacCloseDialogs(mwinMacPlatform* platform, int64_t slot)
{
    for (MwinMacDialog* dialog in [[platform->dialogs copy] autorelease])
    {
        if (slot < 0 || dialog->to.slot == (uint32_t)slot)
        {
            dialog->to.waiting = false;
            if (slot < 0)
            {
                dialog->platform = nullptr;
            }
            Close(dialog);
        }
    }
    if (slot < 0)
    {
        [platform->dialogs release];
        platform->dialogs = nil;
    }
}
