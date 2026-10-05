// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// iOS file dialogs (ios.h): the document picker, presented over the top
// of its window's view controllers. It never blocks: frames go on while
// it shows, and it answers its request when the user is done.
//
// - Opening files, the picker copies them into the application, whose
//   paths it answers with, files the program may read as they are.
// - A folder is the folder itself, reached through a security scope the
//   backend opens and keeps open while the application runs.
// - iOS has no save panel: the picker exports a file to where the user
//   chooses. The dialog exports an empty file of the offered name (or
//   "Untitled"), so the place chosen then holds it, and answers with
//   that place, whose security scope is kept open too, for the program
//   to write.
//
// Filters allow the content types of every filter's extensions at once,
// the picker having no menu of them; none allows every item. The folder
// is where the picker starts. The picker shows no title. A picker whose
// request goes (its window destroyed, the program stopping) is taken
// away at the next pump, and answers nothing.

#include "answer.h"
#include "dialog.h"
#include "ios.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <string.h>

// A picker and the request it answers; its platform is null once the
// backend stopped.
@interface MwinIOSDialog : NSObject <UIDocumentPickerDelegate>
{
  @public
    mwinIOSPlatform* platform;
    mwinServiceAnswer to;
    UIDocumentPickerViewController* picker;
    // Whether the answer's places are reached through a security scope,
    // whether the picker's presentation completed, and whether the
    // dialog finished.
    bool scoped;
    bool shown;
    bool finished;
}
@end

// Answers a picker's request with how it ended and the places chosen;
// one whose request went, or whose backend stopped, answers nothing.
static void Finish(MwinIOSDialog* dialog, mwinOutcome outcome, NSArray<NSURL*>* chosen)
{
    if (dialog->finished)
    {
        return;
    }
    dialog->finished = true;
    [[dialog retain] autorelease];
    // UIKit ignores a dismissal while the presentation runs: one
    // finished before it completes is taken away as it does.
    if (dialog->shown && dialog->picker.presentingViewController != nil)
    {
        [dialog->picker dismissViewControllerAnimated:NO completion:nil];
    }
    mwinIOSPlatform* platform = dialog->platform;
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
    for (NSURL* place in outcome == mwin_outcomeDone ? chosen : @[])
    {
        // Kept open while the application runs.
        if (dialog->scoped)
        {
            (void)[place startAccessingSecurityScopedResource];
        }
        const char* path = place.path.UTF8String;
        if (path != nullptr)
        {
            mwinAddDialogFile(context, path, strlen(path));
        }
    }
    outcome = mwinSettleDialog(context, dialog->to.slot, dialog->to.request, outcome);
    mwinAnswer(context, &dialog->to, outcome);
}

@implementation MwinIOSDialog
- (void)documentPicker:(UIDocumentPickerViewController*)controller
    didPickDocumentsAtURLs:(NSArray<NSURL*>*)urls
{
    (void)controller;
    Finish(self, mwin_outcomeDone, urls);
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController*)controller
{
    (void)controller;
    Finish(self, mwin_outcomeCancelled, @[]);
}

- (void)dealloc
{
    picker.delegate = nil;
    [picker release];
    [super dealloc];
}
@end

static NSString* StringOf(const char* text, uint32_t length)
{
    return [[[NSString alloc] initWithBytes:text length:length
                                   encoding:NSUTF8StringEncoding] autorelease];
}

// The content types of every filter's extensions ("png;jpg"), or every
// item for none.
static NSArray<UTType*>* TypesOf(const mwinDialogCopy* copy)
{
    NSMutableArray<UTType*>* types = [NSMutableArray array];
    for (uint32_t i = 0; i < copy->filterCount; i++)
    {
        for (NSString* extension in
             [@(copy->filters[i].extensions) componentsSeparatedByString:@";"])
        {
            UTType* type = [UTType typeWithFilenameExtension:extension];
            if (type != nil && ![types containsObject:type])
            {
                [types addObject:type];
            }
        }
    }
    return types.count > 0 ? types : @[ UTTypeItem ];
}

// The empty file a save dialog exports, or nil.
static NSURL* ExportedOf(const mwinDialogCopy* copy)
{
    NSString* name = copy->nameLength > 0 ? StringOf(copy->name, copy->nameLength) : @"Untitled";
    NSString* folder = [NSTemporaryDirectory()
        stringByAppendingPathComponent:[@"maul-window-saves"
                                           stringByAppendingPathComponent:NSUUID.UUID.UUIDString]];
    NSString* path = [folder stringByAppendingPathComponent:name.lastPathComponent];
    NSFileManager* files = [NSFileManager defaultManager];
    bool made = [files createDirectoryAtPath:folder
                    withIntermediateDirectories:YES
                                     attributes:nil
                                          error:nullptr] &&
                [files createFileAtPath:path contents:[NSData data] attributes:nil];
    return made ? [NSURL fileURLWithPath:path] : nil;
}

static UIDocumentPickerViewController* PickerOf(const mwinDialogCopy* copy)
{
    switch (copy->kind)
    {
    case mwin_dialogSave:
    {
        NSURL* exported = ExportedOf(copy);
        return exported != nil
                   ? [[UIDocumentPickerViewController alloc] initForExportingURLs:@[ exported ]
                                                                           asCopy:NO]
                   : nil;
    }
    case mwin_dialogFolder:
        return
            [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[ UTTypeFolder ]];
    default:
    {
        UIDocumentPickerViewController* picker =
            [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:TypesOf(copy)
                                                                        asCopy:YES];
        picker.allowsMultipleSelection = copy->kind == mwin_dialogOpenMany;
        return picker;
    }
    }
}

// The view controller everything else of a window shows under.
static UIViewController* TopOf(UIViewController* controller)
{
    while (controller.presentedViewController != nil)
    {
        controller = controller.presentedViewController;
    }
    return controller;
}

int mwinIOSAskDialog(mwinIOSPlatform* platform, uint32_t slot, uint32_t request)
{
    mwinContext* context = platform->context;
    const mwinDialogCopy* copy = context->windows[slot].requests[request].value.dialog;
    UIDocumentPickerViewController* picker = PickerOf(copy);
    if (picker == nil)
    {
        return mwin_outcomeFailed;
    }
    MwinIOSDialog* dialog = [[[MwinIOSDialog alloc] init] autorelease];
    dialog->platform = platform;
    dialog->to = mwinAnswerTo(context, slot, request);
    dialog->picker = picker;
    dialog->scoped = copy->kind == mwin_dialogSave || copy->kind == mwin_dialogFolder;
    picker.delegate = dialog;
    if (copy->folderLength > 0)
    {
        picker.directoryURL = [NSURL fileURLWithPath:StringOf(copy->folder, copy->folderLength)
                                         isDirectory:YES];
    }
    if (platform->dialogs == nil)
    {
        platform->dialogs = [[NSMutableArray alloc] init];
    }
    [platform->dialogs addObject:dialog];
    [TopOf(platform->windows[slot].controller)
        presentViewController:picker
                     animated:YES
                   completion:^{
                     dialog->shown = true;
                     if (dialog->finished)
                     {
                         [dialog->picker dismissViewControllerAnimated:NO completion:nil];
                     }
                   }];
    return -1;
}

void mwinIOSPumpDialogs(mwinIOSPlatform* platform)
{
    for (MwinIOSDialog* dialog in [[platform->dialogs copy] autorelease])
    {
        if (mwinAnswerRequest(platform->context, &dialog->to) == nullptr)
        {
            Finish(dialog, mwin_outcomeCancelled, @[]);
        }
    }
}

void mwinIOSCloseDialogs(mwinIOSPlatform* platform, int64_t slot)
{
    for (MwinIOSDialog* dialog in [[platform->dialogs copy] autorelease])
    {
        if (slot < 0 || dialog->to.slot == (uint32_t)slot)
        {
            dialog->to.waiting = false;
            if (slot < 0)
            {
                dialog->platform = nullptr;
            }
            Finish(dialog, mwin_outcomeCancelled, @[]);
        }
    }
    if (slot < 0)
    {
        [platform->dialogs release];
        platform->dialogs = nil;
    }
}
