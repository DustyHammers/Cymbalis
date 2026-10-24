#include "MacEvents.h"

#import <AppKit/AppKit.h>

#include <cmath>

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

NSWindow* tuningWindow (juce::Component& editor)
{
    auto* peer = editor.getPeer();
    NSWindow* host = peer != nullptr ? [(NSView*) peer->getNativeHandle() window] : nil;
    NSWindow* tuning = nil;
    for (NSWindow* w in [NSApp windows])
        if (w != host && [[w title] rangeOfString: @"Tuning"].location != NSNotFound)
            tuning = w;
    return tuning;
}

// Scroll-wheel events at `location` (window base coordinates) of `window`, sent to the view
// under it, which must be a JUCE view (the plugin's editor or Tuning window). There is no
// NSEvent constructor for scroll events, so they come from CGEvents. Such an event has no
// window and gives its location in screen coordinates (origin at the bottom left of the
// main screen); it is placed so that the view reads it as `location`, as JUCE does
// ([view convertPoint: [event locationInWindow] fromView: nil]).
juce::String wheelAt (NSWindow* window, NSPoint location)
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
    for (int lines : { -1, -1, 1 })
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

void post (NSWindow* window, NSEventType type, NSPoint location)
{
    NSEvent* e = [NSEvent mouseEventWithType: type
                                    location: location
                               modifierFlags: 0
                                   timestamp: [[NSProcessInfo processInfo] systemUptime]
                                windowNumber: [window windowNumber]
                                     context: nil
                                 eventNumber: 0
                                  clickCount: 1
                                    pressure: type == NSEventTypeLeftMouseUp ? 0.0f : 1.0f];
    [NSApp postEvent: e atStart: NO];
}
} // namespace

void click (juce::Component& target, juce::Point<float> p)
{
    NSWindow* window = nil;
    const NSPoint loc = windowPoint (target, p, window);
    [NSApp activateIgnoringOtherApps: YES];
    [window makeKeyAndOrderFront: nil];
    post (window, NSEventTypeMouseMoved, loc);
    post (window, NSEventTypeLeftMouseDown, loc);
    post (window, NSEventTypeLeftMouseUp, loc);
}

juce::String wheel (juce::Component& target, juce::Point<float> p)
{
    NSWindow* window = nil;
    const NSPoint loc = windowPoint (target, p, window);
    return wheelAt (window, loc);
}

juce::String wheelTuning (juce::Component& editor, float fx, float fy)
{
    NSWindow* tuning = tuningWindow (editor);
    if (tuning == nil || ! [tuning isVisible])
        return "no Tuning window";
    const NSRect r = [[tuning contentView] frame];
    return wheelAt (tuning, NSMakePoint (NSMinX (r) + fx * NSWidth (r), NSMaxY (r) - fy * NSHeight (r)));
}

juce::String tuningWindowState (juce::Component& editor)
{
    auto* peer = editor.getPeer();
    NSWindow* host = peer != nullptr ? [(NSView*) peer->getNativeHandle() window] : nil;
    NSWindow* tuning = tuningWindow (editor);
    if (tuning == nil)
        return "none";
    if (! [tuning isVisible])
        return "hidden";
    // Window server order of the windows on screen, front to back.
    long tuningAt = -1, hostAt = -1;
    CFArrayRef list = CGWindowListCopyWindowInfo (kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements,
                                                  kCGNullWindowID);
    if (list != nullptr)
    {
        for (CFIndex i = 0; i < CFArrayGetCount (list); ++i)
        {
            NSDictionary* d = (__bridge NSDictionary*) CFArrayGetValueAtIndex (list, i);
            const long number = [d[(__bridge NSString*) kCGWindowNumber] longValue];
            if (number == (long) [tuning windowNumber]) tuningAt = (long) i;
            if (number == (long) [host windowNumber]) hostAt = (long) i;
        }
        CFRelease (list);
    }
    const bool onScreen = tuningAt >= 0;
    const bool front = onScreen && hostAt >= 0 && tuningAt < hostAt;
    const bool attached = [tuning parentWindow] == host;
    const bool space = [tuning isOnActiveSpace];
    if (onScreen && front && attached && space)
        return "visible-front";
    return juce::String ("on screen ") + (onScreen ? "yes" : "no") + ", in front of the editor " + (front ? "yes" : "no")
           + ", attached " + (attached ? "yes" : "no") + ", active Space " + (space ? "yes" : "no");
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
} // namespace cymbalis::uitest
