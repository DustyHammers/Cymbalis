#include "MacEvents.h"

#import <AppKit/AppKit.h>
#import <objc/runtime.h>

#include <cmath>
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

// ---- The Tuning window's controls, by their text (accessibility elements).

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

id findShowing (NSWindow* window, NSString* text, bool prefix)
{
    NSMutableArray* all = [NSMutableArray array];
    collectElements ([window contentView], all, 0);
    for (id e in all)
        if (showsText (e, text, prefix))
            return e;
    return nil;
}

// The slider whose frame holds the middle of `row` vertically, right of it.
id sliderBeside (NSWindow* window, NSRect row)
{
    NSMutableArray* all = [NSMutableArray array];
    collectElements ([window contentView], all, 0);
    for (id e in all)
        if ([e respondsToSelector: @selector (accessibilityRole)] && [[e accessibilityRole] isEqualToString: NSAccessibilitySliderRole])
        {
            const NSRect f = [e accessibilityFrame];
            if (NSMidY (row) >= NSMinY (f) && NSMidY (row) <= NSMaxY (f) && NSMinX (f) >= NSMaxX (row) - 1.0)
                return e;
        }
    return nil;
}

// The window's content in screen coordinates.
NSRect contentOnScreen (NSWindow* window)
{
    return [window convertRectToScreen: [[window contentView] frame]];
}

// Scrolls the window's list by `lines` (negative: down) with the wheel over its middle.
juce::String scrollList (NSWindow* window, int lines)
{
    const NSRect r = [[window contentView] frame];
    return wheelAt (window, NSMakePoint (NSMinX (r) + 0.5 * NSWidth (r), NSMaxY (r) - 0.6 * NSHeight (r)), { lines });
}

// The list scrolled to its top, then down until an element showing `text` is between 25 % and
// 75 % of the window's height from the top: that element, or nil and what failed.
id scrollToShow (NSWindow* window, NSString* text, bool prefix, juce::String& error)
{
    for (int i = 0; i < 80; ++i)
        if ((error = scrollList (window, 10)).isNotEmpty())
            return nil;
    const NSRect content = contentOnScreen (window);
    for (int i = 0; i < 500; ++i)
    {
        if (id e = findShowing (window, text, prefix))
        {
            const double fromTop = (NSMaxY (content) - NSMidY ([e accessibilityFrame])) / NSHeight (content);
            if (fromTop >= 0.25 && fromTop <= 0.75)
                return e;
        }
        if ((error = scrollList (window, -2)).isNotEmpty())
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
    post (window, NSEventTypeMouseMoved, loc);
    post (window, NSEventTypeLeftMouseDown, loc);
    post (window, NSEventTypeLeftMouseUp, loc);
}

NSWindow* visibleTuningWindow (juce::Component& editor, juce::String& error)
{
    NSWindow* tuning = tuningWindow (editor);
    if (tuning == nil || ! [tuning isVisible])
        error = "no Tuning window";
    return error.isEmpty() ? tuning : nil;
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
juce::String clickTuningSlider (juce::Component& editor, const juce::String& rowName, float fraction)
{
    juce::String error;
    NSWindow* tuning = visibleTuningWindow (editor, error);
    if (tuning == nil)
        return error;
    id row = scrollToShow (tuning, [NSString stringWithUTF8String: rowName.toRawUTF8()], true, error);
    if (row == nil)
        return error;
    id slider = sliderBeside (tuning, [row accessibilityFrame]);
    if (slider == nil)
        return "no slider beside \"" + rowName + "\"";
    const NSRect f = [slider accessibilityFrame];
    clickAtScreenPoint (tuning, NSMakePoint (NSMinX (f) + fraction * NSWidth (f), NSMidY (f)));
    return {};
}

juce::String clickTuningButton (juce::Component& editor, const juce::String& text, bool inList)
{
    juce::String error;
    NSWindow* tuning = visibleTuningWindow (editor, error);
    if (tuning == nil)
        return error;
    NSString* t = [NSString stringWithUTF8String: text.toRawUTF8()];
    id button = inList ? scrollToShow (tuning, t, false, error) : findShowing (tuning, t, false);
    if (button == nil)
        return error.isNotEmpty() ? error : "no button \"" + text + "\"";
    const NSRect f = [button accessibilityFrame];
    clickAtScreenPoint (tuning, NSMakePoint (NSMidX (f), NSMidY (f)));
    return {};
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
