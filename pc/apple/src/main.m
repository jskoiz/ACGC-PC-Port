#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "acgc/macos_host.h"

#include <math.h>
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

typedef void (^ACGCMetalStatusHandler)(NSString* message);

@interface ACGCMetalClearView : NSView
@property(nonatomic, strong) id<MTLDevice> device;
@property(nonatomic, strong) id<MTLCommandQueue> commandQueue;
@property(nonatomic, strong) CAMetalLayer* metalLayer;
@property(nonatomic, strong) NSTimer* frameTimer;
@property(nonatomic, strong) NSTimer* deadlineTimer;
@property(nonatomic, copy) ACGCMetalStatusHandler statusHandler;
@property(nonatomic, copy) NSString* statusMessage;
@property(nonatomic, assign) uint32_t requestedFrames;
@property(nonatomic, assign) double verifySeconds;
@property(nonatomic, assign) NSUInteger submittedFrames;
@property(nonatomic, assign) NSUInteger completedFrames;
@property(nonatomic, assign) BOOL drawableUnavailableNoticeSent;
@property(nonatomic, assign) BOOL failed;
@property(nonatomic, assign) BOOL verificationSucceeded;

- (instancetype)initWithFrame:(NSRect)frame
                requestedFrames:(uint32_t)requestedFrames
                   verifySeconds:(double)verifySeconds
                    statusHandler:(ACGCMetalStatusHandler)statusHandler;
- (void)startRendering;
- (void)stopRendering;
@end

static const MTLClearColor ACGCMetalClearColor = {
    0.125,
    0.250,
    0.500,
    1.000
};

@implementation ACGCMetalClearView

- (instancetype)initWithFrame:(NSRect)frame
                requestedFrames:(uint32_t)requestedFrames
                   verifySeconds:(double)verifySeconds
                    statusHandler:(ACGCMetalStatusHandler)statusHandler {
    self = [super initWithFrame:frame];
    if (self != nil) {
        self.requestedFrames = requestedFrames;
        self.verifySeconds = verifySeconds;
        self.statusHandler = statusHandler;
        self.wantsLayer = YES;
        self.metalLayer = [CAMetalLayer layer];
        self.layer = self.metalLayer;
        [self prepareMetal];
    }
    return self;
}

- (void)prepareMetal {
    self.device = MTLCreateSystemDefaultDevice();
    if (self.device == nil) {
        [self setFailure:@"Metal setup failed: no system Metal device was available"];
        return;
    }
    self.commandQueue = [self.device newCommandQueue];
    if (self.commandQueue == nil) {
        [self setFailure:@"Metal setup failed: could not create a command queue"];
        return;
    }
    if (self.metalLayer == nil) {
        [self setFailure:@"Metal setup failed: could not create a CAMetalLayer"];
        return;
    }
    self.metalLayer.device = self.device;
    self.metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    self.metalLayer.framebufferOnly = YES;
    self.metalLayer.presentsWithTransaction = NO;
    self.metalLayer.allowsNextDrawableTimeout = YES;
    [self updateMetalLayerGeometry];
    self.statusMessage = [NSString stringWithFormat:
        @"Metal clear/present: ready (%@, BGRA8Unorm, clear %.3f/%.3f/%.3f/%.3f)",
        self.device.name,
        ACGCMetalClearColor.red,
        ACGCMetalClearColor.green,
        ACGCMetalClearColor.blue,
        ACGCMetalClearColor.alpha];
    [self notifyStatus];
}

- (void)updateMetalLayerGeometry {
    CGFloat scale = self.window.screen.backingScaleFactor;
    CGSize size;

    if (scale <= 0.0) {
        scale = 1.0;
    }
    self.metalLayer.frame = self.bounds;
    self.metalLayer.contentsScale = scale;
    size = CGSizeMake(
        MAX((CGFloat)1.0, floor(self.bounds.size.width * scale)),
        MAX((CGFloat)1.0, floor(self.bounds.size.height * scale))
    );
    self.metalLayer.drawableSize = size;
}

- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    [self updateMetalLayerGeometry];
}

- (void)layout {
    [super layout];
    [self updateMetalLayerGeometry];
}

- (void)notifyStatus {
    if (self.statusHandler != nil && self.statusMessage != nil) {
        self.statusHandler(self.statusMessage);
    }
}

- (void)setFailure:(NSString*)message {
    if (self.failed || self.verificationSucceeded) {
        return;
    }
    self.failed = YES;
    self.statusMessage = [NSString stringWithFormat:@"Metal clear/present FAILED: %@", message];
    fprintf(stderr, "%s\n", self.statusMessage.UTF8String);
    fflush(stderr);
    [self.frameTimer invalidate];
    [self.deadlineTimer invalidate];
    self.frameTimer = nil;
    self.deadlineTimer = nil;
    [self notifyStatus];
}

- (void)stopAfterFailure {
    dispatch_async(dispatch_get_main_queue(), ^{
        [NSApp stop:nil];
    });
}

- (void)startRendering {
    if (self.failed) {
        [self stopAfterFailure];
        return;
    }
    [self updateMetalLayerGeometry];
    self.frameTimer = [NSTimer scheduledTimerWithTimeInterval:(1.0 / 60.0)
        target:self
        selector:@selector(frameTimerFired:)
        userInfo:nil
        repeats:YES];
    if (self.verifySeconds > 0.0) {
        self.deadlineTimer = [NSTimer scheduledTimerWithTimeInterval:self.verifySeconds
            target:self
            selector:@selector(deadlineTimerFired:)
            userInfo:nil
            repeats:NO];
    }
    [self renderFrame];
}

- (void)frameTimerFired:(NSTimer*)timer {
    (void)timer;
    [self renderFrame];
}

- (void)deadlineTimerFired:(NSTimer*)timer {
    (void)timer;
    self.deadlineTimer = nil;
    if (self.requestedFrames > 0 && self.completedFrames < self.requestedFrames) {
        [self setFailure:[NSString stringWithFormat:
            @"verification deadline expired after %.3f seconds (%lu/%u completed)",
            self.verifySeconds,
            (unsigned long)self.completedFrames,
            self.requestedFrames]];
        [self stopAfterFailure];
        return;
    }
    [NSApp terminate:nil];
}

- (void)renderFrame {
    id<CAMetalDrawable> drawable;
    MTLRenderPassDescriptor* renderPass;
    MTLRenderPassColorAttachmentDescriptor* colorAttachment;
    id<MTLCommandBuffer> commandBuffer;
    id<MTLRenderCommandEncoder> encoder;
    __weak ACGCMetalClearView* weakSelf = self;

    if (self.failed || self.verificationSucceeded || self.commandQueue == nil ||
        self.submittedFrames > self.completedFrames ||
        (self.requestedFrames > 0 && self.submittedFrames >= self.requestedFrames)) {
        return;
    }
    drawable = [self.metalLayer nextDrawable];
    if (drawable == nil) {
        if (!self.drawableUnavailableNoticeSent) {
            self.drawableUnavailableNoticeSent = YES;
            self.statusMessage = @"Metal clear/present: drawable unavailable; retrying";
            [self notifyStatus];
        }
        return;
    }
    self.drawableUnavailableNoticeSent = NO;
    renderPass = [MTLRenderPassDescriptor renderPassDescriptor];
    colorAttachment = renderPass.colorAttachments[0];
    colorAttachment.texture = drawable.texture;
    colorAttachment.loadAction = MTLLoadActionClear;
    colorAttachment.storeAction = MTLStoreActionStore;
    colorAttachment.clearColor = ACGCMetalClearColor;

    commandBuffer = [self.commandQueue commandBuffer];
    if (commandBuffer == nil) {
        [self setFailure:@"present failed: command queue returned no command buffer"];
        [self stopAfterFailure];
        return;
    }
    commandBuffer.label = @"ACGC deterministic Metal clear/present";
    encoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPass];
    if (encoder == nil) {
        [self setFailure:@"present failed: could not create the clear render pass encoder"];
        [self stopAfterFailure];
        return;
    }
    [encoder endEncoding];
    [commandBuffer presentDrawable:drawable];
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> completedCommandBuffer) {
        MTLCommandBufferStatus status = completedCommandBuffer.status;
        NSString* errorDescription = completedCommandBuffer.error.localizedDescription;
        dispatch_async(dispatch_get_main_queue(), ^{
            ACGCMetalClearView* strongSelf = weakSelf;
            if (strongSelf != nil) {
                [strongSelf commandBufferCompletedWithStatus:status
                                                       error:errorDescription];
            }
        });
    }];
    self.submittedFrames += 1;
    [commandBuffer commit];
    if (self.requestedFrames == 0) {
        [self.frameTimer invalidate];
        self.frameTimer = nil;
    }
}

- (void)commandBufferCompletedWithStatus:(MTLCommandBufferStatus)status
                                   error:(NSString*)errorDescription {
    if (self.failed || self.verificationSucceeded) {
        return;
    }
    if (status != MTLCommandBufferStatusCompleted) {
        NSString* detail = errorDescription.length > 0 ? errorDescription : @"unknown command-buffer error";
        [self setFailure:[NSString stringWithFormat:
            @"present completion failed: command buffer status %ld (%@)",
            (long)status,
            detail]];
        [self stopAfterFailure];
        return;
    }
    self.completedFrames += 1;
    self.statusMessage = [NSString stringWithFormat:
        @"Metal clear/present: submitted %lu, completed %lu%@",
        (unsigned long)self.submittedFrames,
        (unsigned long)self.completedFrames,
        self.requestedFrames > 0
            ? [NSString stringWithFormat:@" / requested %u", self.requestedFrames]
            : @""];
    [self notifyStatus];
    if (self.requestedFrames > 0 && self.completedFrames >= self.requestedFrames) {
        self.verificationSucceeded = YES;
        [self.frameTimer invalidate];
        [self.deadlineTimer invalidate];
        self.frameTimer = nil;
        self.deadlineTimer = nil;
        self.statusMessage = [NSString stringWithFormat:
            @"Metal clear/present verification PASSED: %u completed frame%@ (submitted %lu)",
            self.requestedFrames,
            self.requestedFrames == 1 ? @"" : @"s",
            (unsigned long)self.submittedFrames];
        fprintf(stdout, "%s\n", self.statusMessage.UTF8String);
        fflush(stdout);
        [self notifyStatus];
        [NSApp stop:nil];
    } else if (self.requestedFrames > 0) {
        [self renderFrame];
    }
}

- (void)stopRendering {
    [self.frameTimer invalidate];
    [self.deadlineTimer invalidate];
    self.frameTimer = nil;
    self.deadlineTimer = nil;
    self.layer = nil;
    self.metalLayer = nil;
    self.commandQueue = nil;
    self.device = nil;
}

- (void)dealloc {
    [self stopRendering];
}

@end

@interface ACGCNativeHostAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@property(nonatomic, strong) NSWindow* window;
@property(nonatomic, copy) NSString* statusText;
@property(nonatomic, strong) NSTextView* statusView;
@property(nonatomic, strong) ACGCMetalClearView* metalView;
@property(nonatomic, assign) uint32_t verifyFrames;
@property(nonatomic, assign) double verifySeconds;
@end

@implementation ACGCNativeHostAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    NSRect frame = NSMakeRect(0, 0, 820, 560);
    NSWindowStyleMask style = NSWindowStyleMaskTitled |
        NSWindowStyleMaskClosable |
        NSWindowStyleMaskMiniaturizable |
        NSWindowStyleMaskResizable;
    NSSplitView* split_view;
    NSScrollView* scroll_view;
    NSTextView* text_view;
    __weak ACGCNativeHostAppDelegate* weakSelf = self;

    (void)notification;
    self.window = [[NSWindow alloc]
        initWithContentRect:frame
        styleMask:style
        backing:NSBackingStoreBuffered
        defer:NO];
    self.window.title = @"ACGC Modern macOS Native Host";
    self.window.delegate = self;

    split_view = [[NSSplitView alloc] initWithFrame:self.window.contentView.bounds];
    split_view.vertical = NO;
    split_view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    scroll_view = [[NSScrollView alloc] initWithFrame:self.window.contentView.bounds];
    scroll_view.hasVerticalScroller = YES;
    scroll_view.borderType = NSBezelBorder;
    text_view = [[NSTextView alloc] initWithFrame:scroll_view.bounds];
    text_view.editable = NO;
    text_view.selectable = YES;
    text_view.font = [NSFont monospacedSystemFontOfSize:13.0 weight:NSFontWeightRegular];
    text_view.string = self.statusText;
    text_view.textContainer.widthTracksTextView = YES;
    scroll_view.documentView = text_view;
    self.statusView = text_view;

    self.metalView = [[ACGCMetalClearView alloc]
        initWithFrame:NSMakeRect(0, 0, frame.size.width, 360)
        requestedFrames:self.verifyFrames
        verifySeconds:self.verifySeconds
        statusHandler:^(NSString* message) {
            ACGCNativeHostAppDelegate* strongSelf = weakSelf;
            if (strongSelf != nil) {
                [strongSelf updateMetalStatus:message];
            }
        }];
    [split_view addSubview:self.metalView];
    [split_view addSubview:scroll_view];
    [split_view setPosition:360.0 ofDividerAtIndex:0];
    [self.window.contentView addSubview:split_view];
    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    [self.metalView startRendering];
}

- (void)updateMetalStatus:(NSString*)message {
    if (self.statusView == nil || message == nil) {
        return;
    }
    self.statusView.string = [NSString stringWithFormat:@"%@\n\n%@", self.statusText, message];
    [self.statusView scrollRangeToVisible:NSMakeRange(self.statusView.string.length, 0)];
}

- (void)applicationWillTerminate:(NSNotification*)notification {
    (void)notification;
    [self.metalView stopRendering];
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
        delegate.verifyFrames = options.verify_frames;
        delegate.verifySeconds = options.verify_seconds;
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];
        application.delegate = delegate;
        [application run];
        {
            int exit_code = delegate.metalView.failed ? 1 : 0;
            [delegate.metalView stopRendering];
            return exit_code;
        }
    }
    return 0;
}
