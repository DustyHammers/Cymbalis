#include "MacEvents.h"

#import <AppKit/AppKit.h>
#import <objc/runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace cymbalis::uitest
{
namespace
{
// `p` in `target`'s coordinates -> location in its window's base coordinates.
NSPoint windowPoint (juce::Component& target, juce::Point<float> p, NSWindow*& window)
{
    auto* peer = target.getPeer();
    NSView* view = peer != nullptr ? (NSView*) peer->getNativeHandle() : nil;
    window = [view window];
    const auto inPeer = peer->getComponent().getLocalPoint (&target, p);
    // JUCE views are flipped (origin top-left).
    const NSPoint inView = NSMakePoint (inPeer.x, inPeer.y);
    return [view convertPoint: inView toView: nil];
}

// The editor's TUNING page: its window, and the page area (design units 460,100 to 1500,1000)
// on screen. The plugin's editor fills the host's editor component, the design scaled to fit
// and centred (as the test's at()).
struct Page
{
    NSWindow* window = nil;
    NSRect screen = NSZeroRect;
};

Page tuningPage (juce::Component& editor)
{
    Page page;
    const float w = (float) editor.getWidth(), h = (float) editor.getHeight();
    const float s = std::min (w / 1536.0f, h / 1024.0f);
    const float ox = (w - s * 1536.0f) * 0.5f, oy = (h - s * 1024.0f) * 0.5f;
    if (editor.getPeer() == nullptr)
        return page;
    NSWindow* window = nil;
    const NSPoint a = windowPoint (editor, { ox + 460.0f * s, oy + 100.0f * s }, window);   // top left (window y runs up)
    const NSPoint b = windowPoint (editor, { ox + 1500.0f * s, oy + 1000.0f * s }, window); // bottom right
    if (window == nil)
        return page;
    page.window = window;
    page.screen = [window convertRectToScreen: NSMakeRect (a.x, b.y, b.x - a.x, a.y - b.y)];
    return page;
}

// A point on screen -> the window's base coordinates.
NSPoint inWindow (NSWindow* window, NSPoint screen)
{
    return [window convertRectFromScreen: NSMakeRect (screen.x, screen.y, 0.0, 0.0)].origin;
}

// Scroll-wheel events at `location` (window base coordinates) of `window`, sent to the view
// under it, which must be a JUCE view (the plugin's editor). There is no
// NSEvent constructor for scroll events, so they come from CGEvents. Such an event has no
// window and gives its location in screen coordinates (origin at the bottom left of the
// main screen); it is placed so that the view reads it as `location`, as JUCE does
// ([view convertPoint: [event locationInWindow] fromView: nil]).
juce::String wheelAt (NSWindow* window, NSPoint location, std::initializer_list<int> steps = { -1, -1, 1 })
{
    if (window == nil)
        return "no window";
    NSView* content = [window contentView];
    NSView* view = [content hitTest: [[content superview] convertPoint: location fromView: nil]];
    if (view == nil)
        return "no view under the point";
    if (! [NSStringFromClass ([view class]) hasPrefix: @"JUCEView"])
        return juce::String ("not a JUCE view: ") + [NSStringFromClass ([view class]) UTF8String];
    const CGFloat mainHeight = NSMaxY ([[[NSScreen screens] firstObject] frame]);
    for (int lines : steps)
    {
        CGEventRef cg = CGEventCreateScrollWheelEvent (nullptr, kCGScrollEventUnitLine, 1, lines);
        CGEventSetLocation (cg, CGPointMake (location.x, mainHeight - location.y));
        NSEvent* e = [NSEvent eventWithCGEvent: cg];
        CFRelease (cg);
        const NSPoint at = [e locationInWindow];
        if ([e type] != NSEventTypeScrollWheel || [e window] != nil || std::abs (at.x - location.x) > 0.5
            || std::abs (at.y - location.y) > 0.5 || [e deltaY] == 0.0)
            return "unexpected scroll event";
        [view scrollWheel: e];
    }
    return {};
}

// JUCE ignores a click on its window at a point where another window is in front
// (NSViewComponentPeer::contains), as a user's click would land on that window. A window of
// this process in front of `window` at `location` (window base coordinates) is therefore moved
// aside first, as a user would move it: to the right of the point. putBack() returns it to
// where it was. Prints what it did; another application's window is only reported.
NSWindow* movedWindow = nil;
NSPoint movedFrom = NSZeroPoint;

void uncover (NSWindow* window, NSPoint location)
{
    const NSPoint p = [window convertRectToScreen: NSMakeRect (location.x, location.y, 0.0, 0.0)].origin;
    const NSInteger front = [NSWindow windowNumberAtPoint: p belowWindowWithWindowNumber: 0];
    if (front == [window windowNumber])
        return;
    const NSRect screen = [[[NSScreen screens] firstObject] frame];
    NSWindow* other = [NSApp windowWithWindowNumber: front];
    if (other == nil)
    {
        NSString* owner = @"?";
        CFArrayRef info = CGWindowListCopyWindowInfo (kCGWindowListOptionIncludingWindow, (CGWindowID) front);
        if (info != nullptr && CFArrayGetCount (info) > 0)
            if (NSString* name = ((__bridge NSDictionary*) CFArrayGetValueAtIndex (info, 0))[(__bridge NSString*) kCGWindowOwnerName])
                owner = name;
        if (info != nullptr)
            CFRelease (info);
        std::printf ("      the click at (%.0f, %.0f) on the %.0f x %.0f screen is covered by window %ld of %s\n", p.x, p.y,
                     NSWidth (screen), NSHeight (screen), (long) front, [owner UTF8String]);
        std::fflush (stdout);
        return;
    }
    const NSRect f = [other frame];
    NSString* title = [other title] != nil ? [other title] : @"";
    std::printf ("      \"%s\" (%.0f x %.0f at x %.0f, y %.0f) covers the click at (%.0f, %.0f) on the %.0f x %.0f screen: "
                 "moved to x %.0f\n",
                 [title UTF8String], NSWidth (f), NSHeight (f), NSMinX (f), NSMinY (f), p.x, p.y, NSWidth (screen),
                 NSHeight (screen), p.x + 24.0);
    std::fflush (stdout);
    if (movedWindow == nil)
    {
        movedWindow = other;
        movedFrom = f.origin;
    }
    [other setFrameOrigin: NSMakePoint (p.x + 24.0, NSMinY (f))];
}

void post (NSWindow* window, NSEventType type, NSPoint location, int clickCount = 1)
{
    NSEvent* e = [NSEvent mouseEventWithType: type
                                    location: location
                               modifierFlags: 0
                                   timestamp: [[NSProcessInfo processInfo] systemUptime]
                                windowNumber: [window windowNumber]
                                     context: nil
                                 eventNumber: 0
                                  clickCount: clickCount
                                    pressure: type == NSEventTypeLeftMouseUp ? 0.0f : 1.0f];
    [NSApp postEvent: e atStart: NO];
}

// ---- The TUNING page's controls, by their text (accessibility elements).

void collectElements (id element, NSMutableArray* out, int depth)
{
    if (element == nil || depth > 40 || ! [element respondsToSelector: @selector (accessibilityChildren)])
        return;
    for (id child in [element accessibilityChildren])
    {
        [out addObject: child];
        collectElements (child, out, depth + 1);
    }
}

bool showsText (id element, NSString* text, bool prefix)
{
    auto matches = [&] (id s) {
        if (s == nil || ! [s isKindOfClass: [NSString class]])
            return false;
        return prefix ? (bool) [(NSString*) s hasPrefix: text] : (bool) [(NSString*) s isEqualToString: text];
    };
    return ([element respondsToSelector: @selector (accessibilityValue)] && matches ([element accessibilityValue]))
           || ([element respondsToSelector: @selector (accessibilityTitle)] && matches ([element accessibilityTitle]))
           || ([element respondsToSelector: @selector (accessibilityLabel)] && matches ([element accessibilityLabel]));
}

// The accessibility elements of `view` and of every view inside it: a plugin's editor is a
// view of its own (the plugin's JUCE) inside the host's window.
void collectFromViews (NSView* view, NSMutableArray* out)
{
    if (view == nil)
        return;
    collectElements (view, out, 0);
    for (NSView* sub in [view subviews])
        collectFromViews (sub, out);
}

id findShowing (NSWindow* window, NSString* text, bool prefix)
{
    NSMutableArray* all = [NSMutableArray array];
    collectFromViews ([window contentView], all);
    for (id e in all)
        if (showsText (e, text, prefix))
            return e;
    return nil;
}

// The slider whose frame holds the middle of `row` vertically, right of it. JUCE gives each
// frame as the smallest whole-pixel rectangle that holds it; the panel, unscaled under the
// editor's scaled design, has fractional edges, so a frame can reach a pixel past its edge on
// each side and a row's label and its slider overlap by up to 2.
id sliderBeside (NSWindow* window, NSRect row)
{
    NSMutableArray* all = [NSMutableArray array];
    collectFromViews ([window contentView], all);
    for (id e in all)
        if ([e respondsToSelector: @selector (accessibilityRole)] && [[e accessibilityRole] isEqualToString: NSAccessibilitySliderRole])
        {
            const NSRect f = [e accessibilityFrame];
            if (NSMidY (row) >= NSMinY (f) && NSMidY (row) <= NSMaxY (f) && NSMinX (f) >= NSMaxX (row) - 4.0)
                return e;
        }
    return nil;
}

// Scrolls the panel's list by `lines` (negative: down) with the wheel over the page area's
// middle.
juce::String scrollList (const Page& page, int lines)
{
    const NSRect r = page.screen;
    return wheelAt (page.window, inWindow (page.window, NSMakePoint (NSMidX (r), NSMaxY (r) - 0.6 * NSHeight (r))), { lines });
}

// The list scrolled to its top, then down until an element showing `text` is between 25 % and
// 75 % of the page area's height from its top: that element, or nil and what failed.
id scrollToShow (const Page& page, NSString* text, bool prefix, juce::String& error)
{
    for (int i = 0; i < 80; ++i)
        if ((error = scrollList (page, 10)).isNotEmpty())
            return nil;
    const NSRect content = page.screen;
    for (int i = 0; i < 500; ++i)
    {
        if (id e = findShowing (page.window, text, prefix))
        {
            const double fromTop = (NSMaxY (content) - NSMidY ([e accessibilityFrame])) / NSHeight (content);
            if (fromTop >= 0.25 && fromTop <= 0.75)
                return e;
        }
        if ((error = scrollList (page, -2)).isNotEmpty())
            return nil;
    }
    error = juce::String ("nothing shows \"") + [text UTF8String] + "\" in the list";
    return nil;
}

void clickAtScreenPoint (NSWindow* window, NSPoint p)
{
    const NSPoint loc = [window convertRectFromScreen: NSMakeRect (p.x, p.y, 0.0, 0.0)].origin;
    [NSApp activateIgnoringOtherApps: YES];
    [window makeKeyAndOrderFront: nil];
    uncover (window, loc);
    post (window, NSEventTypeMouseMoved, loc);
    post (window, NSEventTypeLeftMouseDown, loc);
    post (window, NSEventTypeLeftMouseUp, loc);
}

// The TUNING page, when it shows (its SAVE... button in the page area); otherwise none and what
// failed.
Page visibleTuningPage (juce::Component& editor, juce::String& error)
{
    const Page page = tuningPage (editor);
    if (page.window == nil)
    {
        error = "no window";
        return {};
    }
    id save = findShowing (page.window, @"SAVE...", false);
    if (save == nil || ! NSPointInRect (NSMakePoint (NSMidX ([save accessibilityFrame]), NSMidY ([save accessibilityFrame])), page.screen))
    {
        error = "no TUNING page";
        return {};
    }
    return page;
}

// ---- File panels that answer at once.

NSURL* panelAnswer = nil;
struct Replaced
{
    Method method;
    IMP original;
};
std::vector<Replaced> replaced;

void answerBegin (id, SEL, void (^handler) (NSModalResponse))
{
    dispatch_async (dispatch_get_main_queue(), ^{ handler (NSModalResponseOK); });
}
NSURL* answerURL (id, SEL) { return panelAnswer; }
NSArray* answerURLs (id, SEL) { return panelAnswer != nil ? [NSArray arrayWithObject: panelAnswer] : [NSArray array]; }

// `sel` of NSSavePanel and of every subclass that has its own, replaced by `imp`.
void replaceInPanels (SEL sel, IMP imp)
{
    const int n = objc_getClassList (nullptr, 0);
    std::vector<Class> classes ((size_t) n);
    const int got = objc_getClassList (classes.data(), n);
    for (int k = 0; k < got && k < n; ++k)
    {
        bool panel = false;
        for (Class c = classes[(size_t) k]; c != nil && ! panel; c = class_getSuperclass (c))
            panel = c == [NSSavePanel class];
        if (! panel)
            continue;
        unsigned count = 0;
        Method* methods = class_copyMethodList (classes[(size_t) k], &count);
        for (unsigned i = 0; i < count; ++i)
            if (method_getName (methods[i]) == sel)
                replaced.push_back ({ methods[i], method_setImplementation (methods[i], imp) });
        std::free (methods);
    }
}
} // namespace

void click (juce::Component& target, juce::Point<float> p)
{
    NSWindow* window = nil;
    const NSPoint loc = windowPoint (target, p, window);
    [NSApp activateIgnoringOtherApps: YES];
    [window makeKeyAndOrderFront: nil];
    uncover (window, loc);
    post (window, NSEventTypeMouseMoved, loc);
    post (window, NSEventTypeLeftMouseDown, loc);
    post (window, NSEventTypeLeftMouseUp, loc);
}

void putBack()
{
    if (movedWindow == nil)
        return;
    [movedWindow setFrameOrigin: movedFrom];
    movedWindow = nil;
}

void doubleClick (juce::Component& target, juce::Point<float> p)
{
    NSWindow* window = nil;
    const NSPoint loc = windowPoint (target, p, window);
    [NSApp activateIgnoringOtherApps: YES];
    [window makeKeyAndOrderFront: nil];
    uncover (window, loc);
    post (window, NSEventTypeMouseMoved, loc);
    post (window, NSEventTypeLeftMouseDown, loc, 1);
    post (window, NSEventTypeLeftMouseUp, loc, 1);
    post (window, NSEventTypeLeftMouseDown, loc, 2);
    post (window, NSEventTypeLeftMouseUp, loc, 2);
}

juce::String sliderText (juce::Component& target, juce::Point<float> p)
{
    NSWindow* window = nil;
    const NSPoint loc = windowPoint (target, p, window);
    if (window == nil)
        return "no window";
    const NSPoint screen = [window convertRectToScreen: NSMakeRect (loc.x, loc.y, 0.0, 0.0)].origin;
    NSMutableArray* all = [NSMutableArray array];
    collectFromViews ([window contentView], all);
    for (id e in all)
    {
        if (! [e respondsToSelector: @selector (accessibilityRole)] || ! [e respondsToSelector: @selector (accessibilityFrame)])
            continue;
        if (! [[e accessibilityRole] isEqualToString: NSAccessibilitySliderRole] || ! NSPointInRect (screen, [e accessibilityFrame]))
            continue;
        id v = [e respondsToSelector: @selector (accessibilityValue)] ? [e accessibilityValue] : nil;
        if ([v isKindOfClass: [NSString class]])
            return juce::String::fromUTF8 ([(NSString*) v UTF8String]);
        return v != nil ? juce::String::fromUTF8 ([[v description] UTF8String]) : juce::String ("(no value)");
    }
    return "no slider at the point";
}

juce::String elementText (juce::Component& target, const juce::String& title)
{
    auto* peer = target.getPeer();
    NSWindow* window = peer != nullptr ? [(NSView*) peer->getNativeHandle() window] : nil;
    if (window == nil)
        return "no window";
    NSString* wanted = [NSString stringWithUTF8String: title.toRawUTF8()];
    NSMutableArray* all = [NSMutableArray array];
    collectFromViews ([window contentView], all);
    for (id e in all)
    {
        if (! [e respondsToSelector: @selector (accessibilityTitle)])
            continue;
        id t = [e accessibilityTitle];
        if (! [t isKindOfClass: [NSString class]] || ! [(NSString*) t isEqualToString: wanted])
            continue;
        id label = [e respondsToSelector: @selector (accessibilityLabel)] ? [e accessibilityLabel] : nil;
        return [label isKindOfClass: [NSString class]] ? juce::String::fromUTF8 ([(NSString*) label UTF8String]) : juce::String ("(no description)");
    }
    return "no element titled " + title;
}

juce::String wheel (juce::Component& target, juce::Point<float> p)
{
    NSWindow* window = nil;
    const NSPoint loc = windowPoint (target, p, window);
    return wheelAt (window, loc);
}

juce::String clickTitled (juce::Component& target, const juce::String& title)
{
    auto* peer = target.getPeer();
    NSWindow* window = peer != nullptr ? [(NSView*) peer->getNativeHandle() window] : nil;
    if (window == nil)
        return "no window";
    NSString* wanted = [NSString stringWithUTF8String: title.toRawUTF8()];
    NSMutableArray* all = [NSMutableArray array];
    collectFromViews ([window contentView], all);
    for (id e in all)
    {
        if (! [e respondsToSelector: @selector (accessibilityTitle)] || ! [e respondsToSelector: @selector (accessibilityFrame)])
            continue;
        id t = [e accessibilityTitle];
        if (! [t isKindOfClass: [NSString class]] || ! [(NSString*) t isEqualToString: wanted])
            continue;
        const NSRect f = [e accessibilityFrame];
        clickAtScreenPoint (window, NSMakePoint (NSMidX (f), NSMidY (f)));
        return {};
    }
    return "no element titled " + title;
}

juce::String wheelTuning (juce::Component& editor, float fx, float fy)
{
    juce::String error;
    const Page page = visibleTuningPage (editor, error);
    if (page.window == nil)
        return error;
    const NSRect r = page.screen;
    return wheelAt (page.window, inWindow (page.window, NSMakePoint (NSMinX (r) + fx * NSWidth (r), NSMaxY (r) - fy * NSHeight (r))));
}

juce::String tuningPageState (juce::Component& editor)
{
    const Page page = tuningPage (editor);
    if (page.window == nil)
        return "no window";
    juce::String error;
    if (visibleTuningPage (editor, error).window != nil)
        return "shown";
    return findShowing (page.window, @"SAVE...", false) == nil ? juce::String ("hidden") : "SAVE... outside the page area";
}

juce::String tuningLayout (juce::Component& editor)
{
    juce::String error;
    const Page page = visibleTuningPage (editor, error);
    if (page.window == nil)
        return error;
    id save = findShowing (page.window, @"SAVE...", false);
    id reset = findShowing (page.window, @"RESET ALL", false);
    if (save == nil || reset == nil)
        return "no SAVE... or RESET ALL button";
    const NSRect sv = [save accessibilityFrame], rs = [reset accessibilityFrame];
    return juce::String::formatted ("page %.0fx%.0f, SAVE... %.0fx%.0f, RESET ALL %.0f from the page's right", NSWidth (page.screen),
                                    NSHeight (page.screen), NSWidth (sv), NSHeight (sv), NSMaxX (page.screen) - NSMaxX (rs));
}

void drag (juce::Component& target, juce::Point<float> from, juce::Point<float> to)
{
    NSWindow* window = nil;
    const NSPoint a = windowPoint (target, from, window);
    const NSPoint b = windowPoint (target, to, window);
    [NSApp activateIgnoringOtherApps: YES];
    [window makeKeyAndOrderFront: nil];
    post (window, NSEventTypeMouseMoved, a);
    post (window, NSEventTypeLeftMouseDown, a);
    for (int i = 1; i <= 8; ++i)
        post (window, NSEventTypeLeftMouseDragged, NSMakePoint (a.x + (b.x - a.x) * i / 8.0, a.y + (b.y - a.y) * i / 8.0));
    post (window, NSEventTypeLeftMouseUp, b);
}

// press, dragTo and release keep the window and the window points of the press: the target
// resizes under the drag, and its points then map to other window points.
static NSWindow* pressWindow = nil;
static NSPoint pressOrigin;
static juce::Point<float> pressAt;

static NSPoint pressPoint (juce::Point<float> p)
{
    return NSMakePoint (pressOrigin.x + (p.x - pressAt.x), pressOrigin.y - (p.y - pressAt.y)); // window y runs up
}

void press (juce::Component& target, juce::Point<float> at)
{
    NSWindow* window = nil;
    pressOrigin = windowPoint (target, at, window);
    pressWindow = window;
    pressAt = at;
    [NSApp activateIgnoringOtherApps: YES];
    [window makeKeyAndOrderFront: nil];
    post (window, NSEventTypeMouseMoved, pressOrigin);
    post (window, NSEventTypeLeftMouseDown, pressOrigin);
}

void dragTo (juce::Component&, juce::Point<float> from, juce::Point<float> to)
{
    if (pressWindow == nil)
        return;
    const NSPoint a = pressPoint (from), b = pressPoint (to);
    for (int i = 1; i <= 8; ++i)
        post (pressWindow, NSEventTypeLeftMouseDragged, NSMakePoint (a.x + (b.x - a.x) * i / 8.0, a.y + (b.y - a.y) * i / 8.0));
}

void release (juce::Component&, juce::Point<float> at)
{
    if (pressWindow == nil)
        return;
    post (pressWindow, NSEventTypeLeftMouseUp, pressPoint (at));
    pressWindow = nil;
}
juce::String clickTuningSlider (juce::Component& editor, const juce::String& rowName, float fraction)
{
    juce::String error;
    const Page page = visibleTuningPage (editor, error);
    if (page.window == nil)
        return error;
    NSWindow* tuning = page.window;
    id row = scrollToShow (page, [NSString stringWithUTF8String: rowName.toRawUTF8()], true, error);
    if (row == nil)
        return error;
    id slider = sliderBeside (tuning, [row accessibilityFrame]);
    if (slider == nil)
        return "no slider beside \"" + rowName + "\"";
    const NSRect f = [slider accessibilityFrame];
    clickAtScreenPoint (tuning, NSMakePoint (NSMinX (f) + fraction * NSWidth (f), NSMidY (f)));
    return {};
}

juce::String clickTuningButtonBeside (juce::Component& editor, const juce::String& rowName, const juce::String& text)
{
    juce::String error;
    const Page page = visibleTuningPage (editor, error);
    if (page.window == nil)
        return error;
    NSWindow* tuning = page.window;
    id row = scrollToShow (page, [NSString stringWithUTF8String: rowName.toRawUTF8()], true, error);
    if (row == nil)
        return error;
    const NSRect r = [row accessibilityFrame];
    NSMutableArray* all = [NSMutableArray array];
    collectFromViews ([tuning contentView], all);
    for (id e in all)
        if (showsText (e, [NSString stringWithUTF8String: text.toRawUTF8()], false))
        {
            const NSRect f = [e accessibilityFrame];
            if (NSMidY (r) >= NSMinY (f) && NSMidY (r) <= NSMaxY (f))
            {
                clickAtScreenPoint (tuning, NSMakePoint (NSMidX (f), NSMidY (f)));
                return {};
            }
        }
    return "no button \"" + text + "\" on the row \"" + rowName + "\"";
}

juce::String clickTuningButton (juce::Component& editor, const juce::String& text, bool inList)
{
    juce::String error;
    const Page page = visibleTuningPage (editor, error);
    if (page.window == nil)
        return error;
    NSWindow* tuning = page.window;
    NSString* t = [NSString stringWithUTF8String: text.toRawUTF8()];
    id button = inList ? scrollToShow (page, t, false, error) : findShowing (tuning, t, false);
    if (button == nil)
        return error.isNotEmpty() ? error : "no button \"" + text + "\"";
    const NSRect f = [button accessibilityFrame];
    clickAtScreenPoint (tuning, NSMakePoint (NSMidX (f), NSMidY (f)));
    return {};
}

juce::String tuningShows (juce::Component& editor, const juce::String& text)
{
    juce::String error;
    const Page page = visibleTuningPage (editor, error);
    if (page.window == nil)
        return error;
    if (scrollToShow (page, [NSString stringWithUTF8String: text.toRawUTF8()], false, error) != nil)
        return "shown";
    return error.startsWith ("nothing shows") ? juce::String ("none") : error;
}

void answerFilePanels (const juce::File& file)
{
    endFilePanels();
    panelAnswer = [[NSURL fileURLWithPath: [NSString stringWithUTF8String: file.getFullPathName().toRawUTF8()]] retain];
    replaceInPanels (@selector (beginWithCompletionHandler:), (IMP) answerBegin);
    replaceInPanels (@selector (URL), (IMP) answerURL);
    replaceInPanels (@selector (URLs), (IMP) answerURLs);
}

void endFilePanels()
{
    for (auto it = replaced.rbegin(); it != replaced.rend(); ++it)
        method_setImplementation (it->method, it->original);
    replaced.clear();
    [panelAnswer release];
    panelAnswer = nil;
}
} // namespace cymbalis::uitest
