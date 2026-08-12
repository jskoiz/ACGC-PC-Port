#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include "acgc/macos_host.h"

#include <stdio.h>
#include <string.h>

static int copy_foundation_path(
    NSString* path,
    char* destination,
    size_t capacity,
    char* error,
    size_t error_capacity
) {
    const char* file_system_path;

    if (path == nil || destination == NULL || capacity == 0) {
        if (error != NULL && error_capacity > 0) {
            snprintf(error, error_capacity, "Foundation returned an empty path");
        }
        return 0;
    }
    file_system_path = [path fileSystemRepresentation];
    if (file_system_path == NULL || strlen(file_system_path) >= capacity) {
        if (error != NULL && error_capacity > 0) {
            snprintf(error, error_capacity, "scoped macOS path exceeds %u bytes",
                     (unsigned)(capacity - 1));
        }
        return 0;
    }
    strcpy(destination, file_system_path);
    return 1;
}

static int prepare_one_path(
    NSSearchPathDirectory directory,
    char* destination,
    size_t capacity,
    char* error,
    size_t error_capacity
) {
    NSArray<NSString*>* roots = NSSearchPathForDirectoriesInDomains(
        directory,
        NSUserDomainMask,
        YES
    );
    NSString* root = roots.firstObject;
    NSString* scoped_path;
    NSError* foundation_error = nil;

    if (root == nil) {
        if (error != NULL && error_capacity > 0) {
            snprintf(error, error_capacity, "macOS user directory is unavailable");
        }
        return 0;
    }
    scoped_path = [root stringByAppendingPathComponent:@ACGC_MACOS_HOST_BUNDLE_IDENTIFIER];
    if (![[NSFileManager defaultManager]
            createDirectoryAtPath:scoped_path
            withIntermediateDirectories:YES
            attributes:nil
            error:&foundation_error]) {
        if (error != NULL && error_capacity > 0) {
            const char* message = foundation_error.localizedDescription.UTF8String;
            snprintf(error, error_capacity, "could not create '%s': %s",
                     scoped_path.fileSystemRepresentation,
                     message != NULL ? message : "Foundation error");
        }
        return 0;
    }
    return copy_foundation_path(
        scoped_path,
        destination,
        capacity,
        error,
        error_capacity
    );
}

int acgc_macos_host_prepare_paths(
    AcgcMacosHostPaths* paths,
    char* error,
    size_t error_capacity
) {
    if (paths == NULL) {
        if (error != NULL && error_capacity > 0) {
            snprintf(error, error_capacity, "path output is required");
        }
        return 0;
    }
    memset(paths, 0, sizeof(*paths));
    if (!prepare_one_path(
            NSApplicationSupportDirectory,
            paths->application_support,
            sizeof(paths->application_support),
            error,
            error_capacity)) {
        return 0;
    }
    if (!prepare_one_path(
            NSCachesDirectory,
            paths->caches,
            sizeof(paths->caches),
            error,
            error_capacity)) {
        memset(paths, 0, sizeof(*paths));
        return 0;
    }
    return 1;
}

@interface ACGCNativeHostAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, strong) NSTimer* verifyTimer;
@property(nonatomic, copy) NSString* statusText;
@property(nonatomic, assign) double verifySeconds;
@end

@implementation ACGCNativeHostAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    NSRect frame = NSMakeRect(0, 0, 820, 560);
    NSWindowStyleMask style = NSWindowStyleMaskTitled |
        NSWindowStyleMaskClosable |
        NSWindowStyleMaskMiniaturizable |
        NSWindowStyleMaskResizable;
    NSScrollView* scroll_view;
    NSTextView* text_view;

    (void)notification;
    self.window = [[NSWindow alloc]
        initWithContentRect:frame
        styleMask:style
        backing:NSBackingStoreBuffered
        defer:NO];
    self.window.title = @"ACGC Modern macOS Native Host";
    self.window.delegate = self;

    scroll_view = [[NSScrollView alloc] initWithFrame:self.window.contentView.bounds];
    scroll_view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    scroll_view.hasVerticalScroller = YES;
    scroll_view.borderType = NSBezelBorder;
    text_view = [[NSTextView alloc] initWithFrame:scroll_view.bounds];
    text_view.editable = NO;
    text_view.selectable = YES;
    text_view.font = [NSFont monospacedSystemFontOfSize:13.0 weight:NSFontWeightRegular];
    text_view.string = self.statusText;
    text_view.textContainer.widthTracksTextView = YES;
    scroll_view.documentView = text_view;
    [self.window.contentView addSubview:scroll_view];
    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    if (self.verifySeconds > 0.0) {
        fprintf(stdout, "macOS host verify mode: foreground window will exit after %.3f seconds\n",
                self.verifySeconds);
        fflush(stdout);
        self.verifyTimer = [NSTimer scheduledTimerWithTimeInterval:self.verifySeconds
            target:self
            selector:@selector(verifyTimerFired:)
            userInfo:nil
            repeats:NO];
    }
}

- (void)verifyTimerFired:(NSTimer*)timer {
    (void)timer;
    [NSApp terminate:nil];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}

@end

static int run_headless(
    const AcgcMacosHostOptions* options,
    const AcgcMacosHostPaths* paths,
    const AcgcMacosDiscReport* report,
    AcgcMacosHostStatus status
) {
    char status_text[8192];

    if (options->disc_path == NULL) {
        fprintf(stderr, "--headless requires --disc PATH; no image search is performed\n");
        return 2;
    }
    acgc_macos_host_format_status(paths, report, status_text, sizeof(status_text));
    fputs(status_text, stdout);
    return status == ACGC_MACOS_HOST_OK ? 0 : 1;
}

int main(int argc, const char* argv[]) {
    AcgcMacosHostOptions options;
    AcgcMacosHostPaths paths;
    AcgcMacosDiscReport report;
    AcgcMacosHostStatus disc_status = ACGC_MACOS_HOST_OK;
    char error[ACGC_MACOS_HOST_ERROR_CAPACITY];
    char status_text[8192];

    @autoreleasepool {
        if (!acgc_macos_host_parse_options(
                argc,
                argv,
                &options,
                error,
                sizeof(error))) {
            fprintf(stderr, "%s\n%s", error, acgc_macos_host_usage());
            return 2;
        }
        if (options.show_help) {
            fputs(acgc_macos_host_usage(), stdout);
            return 0;
        }
        if (options.self_test) {
            return acgc_macos_host_run_self_test();
        }
        if (!acgc_macos_host_prepare_paths(&paths, error, sizeof(error))) {
            fprintf(stderr, "could not prepare scoped macOS paths: %s\n", error);
            return 1;
        }
        memset(&report, 0, sizeof(report));
        if (options.disc_path != NULL) {
            disc_status = acgc_macos_host_validate_disc(options.disc_path, &report);
        }
        if (options.headless) {
            return run_headless(&options, &paths, &report, disc_status);
        }
        acgc_macos_host_format_status(&paths, options.disc_path != NULL ? &report : NULL,
                                      status_text, sizeof(status_text));
        fputs(status_text, stdout);
        fflush(stdout);

        NSApplication* application = [NSApplication sharedApplication];
        ACGCNativeHostAppDelegate* delegate = [[ACGCNativeHostAppDelegate alloc] init];
        delegate.statusText = [NSString stringWithUTF8String:status_text];
        delegate.verifySeconds = options.verify_seconds;
        (void)disc_status;
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];
        application.delegate = delegate;
        [application run];
    }
    return 0;
}
