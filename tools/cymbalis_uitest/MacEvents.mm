#include "MacEvents.h"

#import <AppKit/AppKit.h>

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
