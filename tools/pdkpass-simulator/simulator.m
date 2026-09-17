#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "lvgl.h"
#include "pdkpass_data.h"
#include "pdkpass_network.h"
#include "pdkpass_results.h"
#include "pdkpass_season.h"
#include "pdkpass_ui.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    SIMULATOR_WIDTH = 240,
    SIMULATOR_HEIGHT = 320,
    SIMULATOR_SCALE = 3,
};

static uint16_t s_draw_buffer[SIMULATOR_WIDTH * SIMULATOR_HEIGHT];
static uint16_t s_framebuffer[SIMULATOR_WIDTH * SIMULATOR_HEIGHT];
static size_t s_home_race_index = 12U;
static NSLock *s_results_lock;
static pdkpass_result_snapshot_t
    s_results[PDKPASS_MAX_RACES][PDKPASS_SESSION_COUNT];
static BOOL s_results_loading[PDKPASS_MAX_RACES];
static BOOL s_simulator_online = YES;
static size_t s_last_requested_race = SIZE_MAX;

static __weak NSView *s_simulator_view;

static void simulator_refresh(void);
static BOOL save_framebuffer_png(NSString *path);

// LVGL renders into the same RGB565 framebuffer layout used by the device.
// AppKit only scales this completed 240 x 320 image for the Mac window.
static void display_flush(lv_display_t *display, const lv_area_t *area,
                          uint8_t *pixels)
{
    const uint16_t *source = (const uint16_t *)pixels;
    int width = area->x2 - area->x1 + 1;
    int height = area->y2 - area->y1 + 1;
    for (int y = 0; y < height; y++) {
        memcpy(&s_framebuffer[(area->y1 + y) * SIMULATOR_WIDTH + area->x1],
               &source[y * width], (size_t)width * sizeof(uint16_t));
    }
    lv_display_flush_ready(display);
    [s_simulator_view setNeedsDisplay:YES];
}

static NSBitmapImageRep *framebuffer_representation(void)
{
    NSBitmapImageRep *representation = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:NULL
                      pixelsWide:SIMULATOR_WIDTH
                      pixelsHigh:SIMULATOR_HEIGHT
                   bitsPerSample:8
                 samplesPerPixel:4
                        hasAlpha:YES
                        isPlanar:NO
                  colorSpaceName:NSCalibratedRGBColorSpace
                    bitmapFormat:NSBitmapFormatAlphaNonpremultiplied
                     bytesPerRow:SIMULATOR_WIDTH * 4
                    bitsPerPixel:32];
    unsigned char *destination = representation.bitmapData;
    for (size_t i = 0; i < SIMULATOR_WIDTH * SIMULATOR_HEIGHT; i++) {
        uint16_t pixel = s_framebuffer[i];
        destination[i * 4U + 0U] =
            (unsigned char)((((pixel >> 11U) & 0x1fU) * 255U) / 31U);
        destination[i * 4U + 1U] =
            (unsigned char)((((pixel >> 5U) & 0x3fU) * 255U) / 63U);
        destination[i * 4U + 2U] =
            (unsigned char)(((pixel & 0x1fU) * 255U) / 31U);
        destination[i * 4U + 3U] = 255U;
    }
    return representation;
}

static BOOL save_framebuffer_png(NSString *path)
{
    NSBitmapImageRep *representation = framebuffer_representation();
    NSData *png = [representation representationUsingType:NSBitmapImageFileTypePNG
                                                properties:@{}];
    return [png writeToFile:path atomically:YES];
}

static int s_simulator_battery = 88;

int bsp_battery_soc(void)
{
    return s_simulator_battery;
}

void bsp_display_backlight(uint8_t percent)
{
    (void)percent;
}

// Use the firmware calendar verbatim. Only completion boundaries are shifted
// for the simulator's explicitly selected preview round.
bool pdkpass_season_snapshot(pdkpass_season_snapshot_t *snapshot)
{
    if (!snapshot) return false;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->year = 2026;
    snapshot->race_count = (uint8_t)pdkpass_race_count;
    snapshot->driver_count = (uint8_t)pdkpass_driver_count;
    snprintf(snapshot->standings_as_of, sizeof(snapshot->standings_as_of),
             "31 AUG");
    memcpy(snapshot->races, pdkpass_races,
           pdkpass_race_count * sizeof(snapshot->races[0]));
    time_t now = time(NULL);
    for (size_t i = 0; i < snapshot->race_count; i++) {
        snapshot->races[i].switch_at_utc = i < s_home_race_index
            ? now - (int64_t)(s_home_race_index - i) * 86400
            : now + (int64_t)(i - s_home_race_index + 1U) * 86400;
    }
    memcpy(snapshot->drivers, pdkpass_drivers,
           pdkpass_driver_count * sizeof(snapshot->drivers[0]));
    return true;
}
static NSString *results_cache_path(void)
{
    NSFileManager *manager = NSFileManager.defaultManager;
    NSString *override = NSProcessInfo.processInfo.environment[
        @"PDKPASS_SIMULATOR_CACHE_DIR"];
    NSURL *base = override.length > 0
                      ? [NSURL fileURLWithPath:override isDirectory:YES]
                      : [manager URLsForDirectory:NSCachesDirectory
                                        inDomains:NSUserDomainMask].firstObject;
    if (!base) base = [NSURL fileURLWithPath:NSTemporaryDirectory()];
    NSURL *directory = [base URLByAppendingPathComponent:@"PDKPASS-Simulator"
                                             isDirectory:YES];
    [manager createDirectoryAtURL:directory
      withIntermediateDirectories:YES
                       attributes:nil
                            error:nil];
    return [[directory URLByAppendingPathComponent:@"results-2026.json"] path];
}

static void copy_json_string(char *destination, size_t capacity, id value)
{
    NSString *text = [value isKindOfClass:NSString.class] ? value : @"";
    snprintf(destination, capacity, "%s", text.UTF8String);
}

static void results_cache_load(void)
{
    NSData *data = [NSData dataWithContentsOfFile:results_cache_path()];
    if (!data) return;
    NSDictionary *root = [NSJSONSerialization JSONObjectWithData:data
                                                         options:0
                                                           error:nil];
    if (![root isKindOfClass:NSDictionary.class] ||
        [root[@"version"] unsignedIntegerValue] != 1U ||
        [root[@"year"] unsignedIntegerValue] != 2026U) return;
    NSDictionary *entries = root[@"results"];
    if (![entries isKindOfClass:NSDictionary.class]) return;

    [entries enumerateKeysAndObjectsUsingBlock:^(NSString *key,
                                                 NSDictionary *entry,
                                                 BOOL *stop) {
        (void)stop;
        size_t race = 0;
        unsigned session = 0;
        if (sscanf(key.UTF8String, "%zu-%u", &race, &session) != 2 ||
            race >= PDKPASS_MAX_RACES || session >= PDKPASS_SESSION_COUNT ||
            ![entry isKindOfClass:NSDictionary.class]) return;
        pdkpass_result_snapshot_t *snapshot = &s_results[race][session];
        snapshot->status = (pdkpass_result_status_t)
            [entry[@"status"] unsignedIntegerValue];
        snapshot->session_end_utc = [entry[@"session_end"] longLongValue];
        NSArray *podium = entry[@"podium"];
        if (snapshot->status != PDKPASS_RESULT_READY ||
            ![podium isKindOfClass:NSArray.class] ||
            podium.count != PDKPASS_PODIUM_SIZE) return;
        for (size_t i = 0; i < PDKPASS_PODIUM_SIZE; i++) {
            NSDictionary *driver = podium[i];
            snapshot->podium[i].position = (unsigned)i + 1U;
            copy_json_string(snapshot->podium[i].code,
                             sizeof(snapshot->podium[i].code),
                             driver[@"code"]);
            copy_json_string(snapshot->podium[i].name,
                             sizeof(snapshot->podium[i].name),
                             driver[@"name"]);
            copy_json_string(snapshot->podium[i].team,
                             sizeof(snapshot->podium[i].team),
                             driver[@"team"]);
        }
    }];
}

// Called while s_results_lock is held. Only terminal classifications are
// persisted; pending sessions are requested again the next time they are open.
static void results_cache_save_locked(void)
{
    NSMutableDictionary *entries = [NSMutableDictionary dictionary];
    for (size_t race = 0; race < PDKPASS_MAX_RACES; race++) {
        for (size_t session = 0; session < PDKPASS_SESSION_COUNT; session++) {
            const pdkpass_result_snapshot_t *snapshot =
                &s_results[race][session];
            BOOL raceComplete =
                s_results[race][PDKPASS_SESSION_RACE].status ==
                PDKPASS_RESULT_READY;
            if (snapshot->status != PDKPASS_RESULT_READY &&
                snapshot->status != PDKPASS_RESULT_CANCELLED &&
                !(snapshot->status == PDKPASS_RESULT_NOT_HELD &&
                  raceComplete)) continue;
            NSMutableDictionary *entry = [@{
                @"status" : @(snapshot->status),
                @"session_end" : @(snapshot->session_end_utc),
            } mutableCopy];
            if (snapshot->status == PDKPASS_RESULT_READY) {
                NSMutableArray *podium = [NSMutableArray array];
                for (size_t i = 0; i < PDKPASS_PODIUM_SIZE; i++) {
                    [podium addObject:@{
                        @"code" : @(snapshot->podium[i].code),
                        @"name" : @(snapshot->podium[i].name),
                        @"team" : @(snapshot->podium[i].team),
                    }];
                }
                entry[@"podium"] = podium;
            }
            entries[[NSString stringWithFormat:@"%zu-%zu", race, session]] =
                entry;
        }
    }
    NSDictionary *root = @{
        @"version" : @1,
        @"year" : @2026,
        @"results" : entries,
    };
    NSData *data = [NSJSONSerialization dataWithJSONObject:root
                                                   options:0
                                                     error:nil];
    [data writeToFile:results_cache_path()
              options:NSDataWritingAtomic
                error:nil];
}

static NSArray *openf1_get(NSString *endpoint,
                           NSDictionary<NSString *, NSString *> *parameters)
{
    NSURLComponents *components = [NSURLComponents
        componentsWithString:[@"https://api.openf1.org/v1/"
                                  stringByAppendingString:endpoint]];
    NSMutableArray<NSURLQueryItem *> *items = [NSMutableArray array];
    [parameters enumerateKeysAndObjectsUsingBlock:^(NSString *key,
                                                     NSString *value,
                                                     BOOL *stop) {
        (void)stop;
        [items addObject:[NSURLQueryItem queryItemWithName:key value:value]];
    }];
    components.queryItems = items;

    NSMutableURLRequest *request = [NSMutableURLRequest
        requestWithURL:components.URL
           cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
       timeoutInterval:15.0];
    [request setValue:@"application/json" forHTTPHeaderField:@"Accept"];
    [request setValue:@"PDKPASS-Simulator/1.0"
        forHTTPHeaderField:@"User-Agent"];

    dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
    __block NSData *responseData = nil;
    __block NSInteger statusCode = 0;
    NSURLSessionConfiguration *configuration =
        NSURLSessionConfiguration.ephemeralSessionConfiguration;
    configuration.URLCache = nil;
    configuration.HTTPCookieStorage = nil;
    NSURLSession *session = [NSURLSession sessionWithConfiguration:configuration];
    NSURLSessionDataTask *task = [session
        dataTaskWithRequest:request
          completionHandler:^(NSData *data, NSURLResponse *response,
                              NSError *error) {
        if (!error) {
            responseData = data;
            statusCode = [(NSHTTPURLResponse *)response statusCode];
        }
        dispatch_semaphore_signal(semaphore);
    }];
    [task resume];
    if (dispatch_semaphore_wait(
            semaphore,
            dispatch_time(DISPATCH_TIME_NOW, 20LL * NSEC_PER_SEC)) != 0) {
        [task cancel];
        [session invalidateAndCancel];
        return nil;
    }
    [session finishTasksAndInvalidate];
    if (statusCode != 200 || !responseData) return nil;
    id json = [NSJSONSerialization JSONObjectWithData:responseData
                                              options:0
                                                error:nil];
    return [json isKindOfClass:NSArray.class] ? json : nil;
}

static BOOL simulator_results_online(void)
{
    [s_results_lock lock];
    BOOL online = s_simulator_online;
    [s_results_lock unlock];
    return online;
}

static void results_notify(size_t race_index)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        pdkpass_ui_results_update(race_index);
        simulator_refresh();
    });
}

static BOOL fill_podium(NSArray *classification, NSArray *drivers,
                        pdkpass_result_snapshot_t *snapshot)
{
    NSDictionary *top[PDKPASS_PODIUM_SIZE] = { nil, nil, nil };
    for (NSDictionary *entry in classification) {
        unsigned position = [entry[@"position"] unsignedIntValue];
        if (position >= 1U && position <= PDKPASS_PODIUM_SIZE) {
            top[position - 1U] = entry;
        }
    }
    for (size_t i = 0; i < PDKPASS_PODIUM_SIZE; i++) {
        if (!top[i]) return NO;
        NSInteger number = [top[i][@"driver_number"] integerValue];
        NSDictionary *match = nil;
        for (NSDictionary *driver in drivers) {
            if ([driver[@"driver_number"] integerValue] == number) {
                match = driver;
                break;
            }
        }
        if (!match) return NO;
        pdkpass_podium_driver_t *podium = &snapshot->podium[i];
        podium->position = (unsigned)i + 1U;
        copy_json_string(podium->code, sizeof(podium->code),
                         match[@"name_acronym"]);
        copy_json_string(podium->name, sizeof(podium->name),
                         [match[@"last_name"] uppercaseString]);
        copy_json_string(podium->team, sizeof(podium->team),
                         [match[@"team_name"] uppercaseString]);
    }
    snapshot->status = PDKPASS_RESULT_READY;
    return YES;
}

static void results_sync_race(size_t race_index)
{
    pdkpass_season_snapshot_t season;
    NSArray *sessions = nil;
    if (!pdkpass_season_snapshot(&season) ||
        race_index >= season.race_count || !simulator_results_online()) {
        goto complete;
    }
    const pdkpass_race_t *race = &season.races[race_index];
    sessions = openf1_get(@"sessions", @{
        @"year" : [NSString stringWithFormat:@"%u", season.year],
        @"country_name" : @(race->api_country),
    });
    if (!sessions) {
        fprintf(stderr, "OpenF1 session sync failed for R%u\n", race->round);
        goto complete;
    }

    BOOL present[PDKPASS_SESSION_COUNT] = { NO };
    for (NSDictionary *sessionInfo in sessions) {
        NSString *name = sessionInfo[@"session_name"];
        pdkpass_session_kind_t kind = pdkpass_session_kind_from_name(
            [name isKindOfClass:NSString.class] ? name.UTF8String : NULL);
        if (kind >= PDKPASS_SESSION_COUNT) continue;
        present[kind] = YES;

        pdkpass_result_snapshot_t snapshot;
        memset(&snapshot, 0, sizeof(snapshot));
        NSString *dateEnd = sessionInfo[@"date_end"];
        if ([dateEnd isKindOfClass:NSString.class]) {
            pdkpass_parse_iso8601_utc(dateEnd.UTF8String,
                                      &snapshot.session_end_utc);
        }
        snapshot.status = [sessionInfo[@"is_cancelled"] boolValue]
                              ? PDKPASS_RESULT_CANCELLED
                              : PDKPASS_RESULT_SCHEDULED;

        [s_results_lock lock];
        BOOL alreadyReady =
            s_results[race_index][kind].status == PDKPASS_RESULT_READY;
        if (!alreadyReady) s_results[race_index][kind] = snapshot;
        [s_results_lock unlock];
        results_notify(race_index);
        if (alreadyReady || snapshot.status == PDKPASS_RESULT_CANCELLED ||
            !pdkpass_session_result_due((int64_t)time(NULL),
                                        snapshot.session_end_utc) ||
            !simulator_results_online()) continue;

        NSString *sessionKey = [sessionInfo[@"session_key"] stringValue];
        NSArray *classification = openf1_get(@"session_result", @{
            @"session_key" : sessionKey,
            @"position<" : @"3",
        });
        NSArray *drivers = openf1_get(@"drivers", @{
            @"session_key" : sessionKey,
        });
        if (classification && drivers &&
            fill_podium(classification, drivers, &snapshot)) {
            [s_results_lock lock];
            s_results[race_index][kind] = snapshot;
            results_cache_save_locked();
            [s_results_lock unlock];
            results_notify(race_index);
        }
    }

    [s_results_lock lock];
    for (size_t kind = 0; kind < PDKPASS_SESSION_COUNT; kind++) {
        if (!present[kind] &&
            s_results[race_index][kind].status != PDKPASS_RESULT_READY) {
            memset(&s_results[race_index][kind], 0,
                   sizeof(s_results[race_index][kind]));
            s_results[race_index][kind].status = PDKPASS_RESULT_NOT_HELD;
        }
    }
    results_cache_save_locked();
    [s_results_lock unlock];
    results_notify(race_index);

complete:
    [s_results_lock lock];
    s_results_loading[race_index] = NO;
    [s_results_lock unlock];
}

bool pdkpass_results_get(size_t race_index, pdkpass_session_kind_t session,
                         pdkpass_result_snapshot_t *snapshot)
{
    if (!snapshot || !s_results_lock || race_index >= PDKPASS_MAX_RACES ||
        session >= PDKPASS_SESSION_COUNT) return false;
    [s_results_lock lock];
    *snapshot = s_results[race_index][session];
    [s_results_lock unlock];
    return true;
}

void pdkpass_results_request_race(size_t race_index)
{
    if (!s_results_lock || race_index >= PDKPASS_MAX_RACES) return;
    [s_results_lock lock];
    s_last_requested_race = race_index;
    if (!s_simulator_online || s_results_loading[race_index]) {
        [s_results_lock unlock];
        return;
    }
    BOOL needsSync = NO;
    for (size_t session = 0; session < PDKPASS_SESSION_COUNT; session++) {
        pdkpass_result_status_t status = s_results[race_index][session].status;
        if (status == PDKPASS_RESULT_UNKNOWN ||
            status == PDKPASS_RESULT_SCHEDULED) {
            needsSync = YES;
            break;
        }
    }
    if (!needsSync) {
        [s_results_lock unlock];
        return;
    }
    s_results_loading[race_index] = YES;
    [s_results_lock unlock];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        results_sync_race(race_index);
    });
}

static void simulator_set_network(pdkpass_network_state_t state)
{
    if (s_results_lock) {
        [s_results_lock lock];
        s_simulator_online = state == PDKPASS_NETWORK_ONLINE;
        size_t requested = s_last_requested_race;
        [s_results_lock unlock];
        if (s_simulator_online && requested < PDKPASS_MAX_RACES) {
            pdkpass_results_request_race(requested);
        }
    }
    pdkpass_network_update_t update = {
        .state = state,
        .time_valid = state == PDKPASS_NETWORK_ONLINE ||
                      state == PDKPASS_NETWORK_OFFLINE,
        .setup_ssid = "PDKPASS-SETUP",
        .setup_password = "K7M9P2X4",
        .hotspot_active = state == PDKPASS_NETWORK_SETUP,
        .setup_seconds_left = 180,
    };
    pdkpass_ui_network_update(&update);
    simulator_refresh();
}

// UI preview only: radio/timers are exercised by firmware service tests.
void pdkpass_network_request(pdkpass_network_command_t command)
{
    simulator_set_network(command == PDKPASS_NETWORK_OPEN_SETUP ? PDKPASS_NETWORK_SETUP :
        command == PDKPASS_NETWORK_RETRY ? PDKPASS_NETWORK_CONNECTING : PDKPASS_NETWORK_OFFLINE);
}

static void simulator_send_button(bsp_btn_t button, bsp_btn_ev_t event)
{
    pdkpass_ui_key(button, event);
    simulator_refresh();
}

static void simulator_refresh(void)
{
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
}

static void simulator_initialize(void)
{
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
    memset(s_results, 0, sizeof(s_results));
    memset(s_results_loading, 0, sizeof(s_results_loading));
    s_results_lock = [[NSLock alloc] init];
    results_cache_load();
    lv_init();
    lv_display_t *display = lv_display_create(SIMULATOR_WIDTH, SIMULATOR_HEIGHT);
    lv_display_set_buffers(display, s_draw_buffer, NULL, sizeof(s_draw_buffer),
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, display_flush);
    pdkpass_ui_enter(true);
    pdkpass_ui_battery_update(bsp_battery_soc());
    simulator_set_network(PDKPASS_NETWORK_ONLINE);
}

@interface PDKPASSSimulatorView : NSView
@property(nonatomic) NSTimeInterval okPressedAt;
@property(nonatomic) BOOL okIsDown;
@end

@implementation PDKPASSSimulatorView

- (BOOL)isFlipped
{
    return YES;
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (void)drawRect:(NSRect)dirtyRect
{
    (void)dirtyRect;
    [[NSColor blackColor] setFill];
    NSRectFill(self.bounds);

    NSBitmapImageRep *representation = framebuffer_representation();
    NSImage *image = [[NSImage alloc]
        initWithSize:NSMakeSize(SIMULATOR_WIDTH, SIMULATOR_HEIGHT)];
    [image addRepresentation:representation];
    NSGraphicsContext.currentContext.imageInterpolation = NSImageInterpolationNone;
    [image drawInRect:self.bounds
             fromRect:NSMakeRect(0, 0, SIMULATOR_WIDTH, SIMULATOR_HEIGHT)
            operation:NSCompositingOperationCopy
             fraction:1.0
       respectFlipped:YES
                hints:@{NSImageHintInterpolation : @(NSImageInterpolationNone)}];
}

- (void)keyDown:(NSEvent *)event
{
    if (event.isARepeat) return;
    switch (event.keyCode) {
    case 126:
        simulator_send_button(BSP_BTN_UP, BSP_BTN_CLICK);
        return;
    case 125:
        simulator_send_button(BSP_BTN_DOWN, BSP_BTN_CLICK);
        return;
    case 36:
    case 49:
    case 76:
        self.okIsDown = YES;
        self.okPressedAt = event.timestamp;
        return;
    case 53:
        simulator_send_button(BSP_BTN_OK, BSP_BTN_LONG);
        return;
    default:
        break;
    }

    NSString *key = event.charactersIgnoringModifiers.lowercaseString;
    if ([key isEqualToString:@"1"]) {
        simulator_set_network(PDKPASS_NETWORK_SETUP);
    } else if ([key isEqualToString:@"2"]) {
        simulator_set_network(PDKPASS_NETWORK_CONNECTING);
    } else if ([key isEqualToString:@"3"]) {
        simulator_set_network(PDKPASS_NETWORK_SYNCING);
    } else if ([key isEqualToString:@"4"]) {
        simulator_set_network(PDKPASS_NETWORK_ONLINE);
    } else if ([key isEqualToString:@"5"]) {
        simulator_set_network(PDKPASS_NETWORK_OFFLINE);
    } else if ([key isEqualToString:@"s"] &&
               !(event.modifierFlags & NSEventModifierFlagCommand)) {
        [NSApp sendAction:@selector(saveScreenshot:)
                       to:NSApp.delegate
                     from:self];
    }
}

- (void)keyUp:(NSEvent *)event
{
    if ((event.keyCode == 36 || event.keyCode == 49 || event.keyCode == 76) &&
        self.okIsDown) {
        self.okIsDown = NO;
        NSTimeInterval duration = event.timestamp - self.okPressedAt;
        simulator_send_button(BSP_BTN_OK,
                              duration >= 0.65 ? BSP_BTN_LONG : BSP_BTN_CLICK);
        return;
    }
    [super keyUp:event];
}

@end

@interface PDKPASSSimulatorDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, strong) NSWindow *window;
@property(nonatomic, strong) NSTimer *timer;
@end

@implementation PDKPASSSimulatorDelegate

- (void)installMenu
{
    NSMenu *menuBar = [[NSMenu alloc] init];
    NSMenuItem *appItem = [[NSMenuItem alloc] init];
    [menuBar addItem:appItem];
    NSMenu *appMenu = [[NSMenu alloc] init];
    [appMenu addItemWithTitle:@"Quit PDKPASS Simulator"
                       action:@selector(terminate:)
                keyEquivalent:@"q"];
    appItem.submenu = appMenu;

    NSMenuItem *fileItem = [[NSMenuItem alloc] init];
    [menuBar addItem:fileItem];
    NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
    NSMenuItem *saveItem = [fileMenu addItemWithTitle:@"Save Screenshot…"
                                               action:@selector(saveScreenshot:)
                                        keyEquivalent:@"s"];
    saveItem.target = self;
    fileItem.submenu = fileMenu;
    NSApp.mainMenu = menuBar;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification
{
    (void)notification;
    [self installMenu];

    NSRect frame = NSMakeRect(0, 0, SIMULATOR_WIDTH * SIMULATOR_SCALE,
                             SIMULATOR_HEIGHT * SIMULATOR_SCALE);
    self.window = [[NSWindow alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskTitled |
                            NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable |
                            NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    self.window.title = @"PDKPASS Simulator — ↑↓ Browse · Return OK · Hold/Esc Back";
    self.window.contentAspectRatio = NSMakeSize(SIMULATOR_WIDTH, SIMULATOR_HEIGHT);
    self.window.minSize = NSMakeSize(SIMULATOR_WIDTH + 16, SIMULATOR_HEIGHT + 39);

    PDKPASSSimulatorView *view = [[PDKPASSSimulatorView alloc] initWithFrame:frame];
    s_simulator_view = view;
    self.window.contentView = view;
    simulator_initialize();

    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
    [self.window makeFirstResponder:view];
    [NSApp activateIgnoringOtherApps:YES];

    self.timer = [NSTimer scheduledTimerWithTimeInterval:0.016
                                                  target:self
                                                selector:@selector(tick:)
                                                userInfo:nil
                                                 repeats:YES];
}

- (void)tick:(NSTimer *)timer
{
    (void)timer;
    lv_tick_inc(16);
    lv_timer_handler();
}

- (void)saveScreenshot:(id)sender
{
    (void)sender;
    NSDateFormatter *formatter = [[NSDateFormatter alloc] init];
    formatter.dateFormat = @"yyyyMMdd-HHmmss";
    NSString *name = [NSString stringWithFormat:@"PDKPASS-%@.png",
                      [formatter stringFromDate:[NSDate date]]];
    NSSavePanel *panel = [NSSavePanel savePanel];
    panel.nameFieldStringValue = name;
    panel.allowedContentTypes = @[UTTypePNG];
    [panel beginSheetModalForWindow:self.window
                  completionHandler:^(NSModalResponse response) {
        if (response != NSModalResponseOK) return;
        if (!save_framebuffer_png(panel.URL.path)) {
            NSAlert *alert = [[NSAlert alloc] init];
            alert.messageText = @"Could not save the screenshot.";
            [alert beginSheetModalForWindow:self.window completionHandler:nil];
        }
    }];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
    (void)sender;
    return YES;
}

@end


static void print_usage(const char *program)
{
    printf("Usage: %s [--race 1-23] [--page home|calendar|standings|track|results] [--sync-results] [--network-view menu|retry|setup|confirm] "
           "[--battery -1..100] [--screenshot FILE.png]\n", program);
    printf("\nKeyboard: Up/Down browse, Return/Space select, hold Return or Esc back,\n");
    printf("          1-5 network states, S or Command-S screenshot.\n");
}

int main(int argc, const char *argv[])
{
    @autoreleasepool {
        NSString *screenshotPath = nil;
        const char *networkView = NULL;
        const char *pageView = "home";
        BOOL syncResults = NO;
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--page") == 0 && i + 1 < argc) {
                pageView = argv[++i];
                if (strcmp(pageView, "home") && strcmp(pageView, "calendar") &&
                    strcmp(pageView, "standings") && strcmp(pageView, "track") &&
                    strcmp(pageView, "results")) return 2;
                continue;
            }
            if (strcmp(argv[i], "--network-view") == 0 && i + 1 < argc) {
                networkView = argv[++i];
                if (strcmp(networkView, "menu") && strcmp(networkView, "retry") &&
                    strcmp(networkView, "setup") && strcmp(networkView, "confirm")) return 2;
                continue;
            }
            if (strcmp(argv[i], "--battery") == 0 && i + 1 < argc) {
                char *end;
                long value = strtol(argv[++i], &end, 10);
                if (!argv[i][0] || *end || value < -1 || value > 100) {
                    fprintf(stderr, "--battery must be -1 (unknown) or 0..100\n");
                    return 2;
                }
                s_simulator_battery = (int)value;
                continue;
            }
            if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
                print_usage(argv[0]);
                return 0;
            }
            if (strcmp(argv[i], "--race") == 0 && i + 1 < argc) {
                unsigned long race = strtoul(argv[++i], NULL, 10);
                if (race < 1U || race > 23U) {
                    fprintf(stderr, "--race must be between 1 and 23\n");
                    return 2;
                }
                s_home_race_index = (size_t)race - 1U;
                continue;
            }
            if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
                screenshotPath = [NSString stringWithUTF8String:argv[++i]];
                continue;
            }
            if (strcmp(argv[i], "--sync-results") == 0) {
                syncResults = YES;
                continue;
            }
            fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 2;
        }

        if (screenshotPath || syncResults) {
            simulator_initialize();
            if (!networkView && !syncResults) {
                if (strcmp(pageView, "calendar") == 0)
                    simulator_send_button(BSP_BTN_DOWN, BSP_BTN_CLICK);
                else if (strcmp(pageView, "standings") == 0)
                    simulator_send_button(BSP_BTN_UP, BSP_BTN_CLICK);
                else if (strcmp(pageView, "track") == 0 || strcmp(pageView, "results") == 0) {
                    simulator_send_button(BSP_BTN_OK, BSP_BTN_CLICK);
                    if (strcmp(pageView, "results") == 0)
                        simulator_send_button(BSP_BTN_OK, BSP_BTN_CLICK);
                }
            }
            if (networkView) {
                simulator_set_network(strcmp(networkView, "confirm") == 0
                    ? PDKPASS_NETWORK_ONLINE : PDKPASS_NETWORK_OFFLINE);
                simulator_send_button(BSP_BTN_OK, BSP_BTN_LONG);
                if (strcmp(networkView, "menu") != 0) {
                    if (strcmp(networkView, "retry") != 0)
                        simulator_send_button(BSP_BTN_DOWN, BSP_BTN_CLICK);
                    simulator_send_button(BSP_BTN_OK, BSP_BTN_CLICK);
                }
            }
            if (syncResults) {
                pdkpass_results_request_race(s_home_race_index);
                NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:120.0];
                for (;;) {
                    [s_results_lock lock];
                    BOOL loading = s_results_loading[s_home_race_index];
                    [s_results_lock unlock];
                    if (!loading || deadline.timeIntervalSinceNow <= 0.0) break;
                    [NSThread sleepForTimeInterval:0.05];
                }
                for (size_t session = 0; session < PDKPASS_SESSION_COUNT;
                     session++) {
                    pdkpass_result_snapshot_t result;
                    pdkpass_results_get(s_home_race_index,
                                        (pdkpass_session_kind_t)session,
                                        &result);
                    printf("R%02zu %-15s status=%u", s_home_race_index + 1U,
                           pdkpass_session_label(
                               (pdkpass_session_kind_t)session),
                           result.status);
                    if (result.status == PDKPASS_RESULT_READY) {
                        printf(" %s/%s/%s", result.podium[0].code,
                               result.podium[1].code,
                               result.podium[2].code);
                    }
                    printf("\n");
                }
                if (screenshotPath) {
                    simulator_send_button(BSP_BTN_OK, BSP_BTN_CLICK);
                    simulator_send_button(BSP_BTN_OK, BSP_BTN_CLICK);
                }
            }
            simulator_refresh();
            if (!screenshotPath) return 0;
            if (!save_framebuffer_png(screenshotPath)) {
                fprintf(stderr, "Could not write %s\n", screenshotPath.UTF8String);
                return 3;
            }
            printf("Saved %s\n", screenshotPath.UTF8String);
            return 0;
        }

        NSApplication *application = [NSApplication sharedApplication];
        application.activationPolicy = NSApplicationActivationPolicyRegular;
        PDKPASSSimulatorDelegate *delegate = [[PDKPASSSimulatorDelegate alloc] init];
        application.delegate = delegate;
        [application run];
    }
    return 0;
}
