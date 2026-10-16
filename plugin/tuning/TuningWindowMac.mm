// Tuning build only (docs/TUNING.md): the Tuning window as a child window of the editor's
// window, so it is always in front of the editor and the host, on the editor's Space, and
// moves and goes with it.
#include "TuningWindow.h"

#import <AppKit/AppKit.h>

#include <algorithm>

namespace cymbalis::tuning
{
namespace
{
NSWindow* nativeWindow (juce::Component& c)
{
    auto* peer = c.getPeer();
    if (peer == nullptr)
        return nil;
    NSView* view = (NSView*) peer->getNativeHandle();
    return [view window];
}
} // namespace

void attachAbove (juce::Component& window, juce::Component& owner)
{
    NSWindow* child = nativeWindow (window);
    NSWindow* parent = nativeWindow (owner);
    if (child == nil || parent == nil || child == parent)
        return;
    if ([child parentWindow] != parent)
    {
        if ([child parentWindow] != nil)
            [[child parentWindow] removeChildWindow: child];
        [parent addChildWindow: child ordered: NSWindowAbove];
    }
    // In front of the host's windows too (hosts often float their plugin windows), but not
    // in front of other applications while the host is in the background.
    [child setLevel: std::max ([parent level], (NSInteger) NSFloatingWindowLevel)];
    [child setHidesOnDeactivate: YES];
    [child setCollectionBehavior: [child collectionBehavior] | NSWindowCollectionBehaviorFullScreenAuxiliary];
    [child orderFront: nil];
}

void detach (juce::Component& window)
{
    NSWindow* child = nativeWindow (window);
    if (child != nil && [child parentWindow] != nil)
        [[child parentWindow] removeChildWindow: child];
}
} // namespace cymbalis::tuning
