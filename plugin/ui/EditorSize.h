#pragma once

// The editor's size limits and its opening size (docs/UI_SPEC.md section 2). No JUCE, so the
// mouse test (tools/cymbalis_uitest) checks the same rule.

#include <algorithm>
#include <cmath>

namespace cymbalis::ui
{

constexpr int kEditorMinWidth = 960, kEditorMinHeight = 640;   // the design is 1536 x 1024, the maximum
constexpr int kEditorOpenWidth = 1104;                         // 1104 x 736: 0.71875 of the design

// The width a new editor with no saved size opens at, for a primary display whose user area
// is areaWidth x areaHeight: 1104 (Kris, 2026-11-23), but at most the user area's width - 40
// and, at 3:2, its height - 200, so a host's title and preset bars still fit around it; never
// below the 960 minimum nor above the 1536 maximum.
inline int editorOpeningWidth (int areaWidth, int areaHeight, int maxWidth) noexcept
{
    const int byHeight = static_cast<int> (std::lround ((areaHeight - 200) * 1.5));
    return std::clamp (std::min ({ kEditorOpenWidth, areaWidth - 40, byHeight }), kEditorMinWidth, maxWidth);
}

// The 3:2 height of a width.
inline int editorHeightFor (int width) noexcept
{
    return static_cast<int> (std::lround (width / 1.5));
}

} // namespace cymbalis::ui
