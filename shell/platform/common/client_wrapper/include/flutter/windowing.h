// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_COMMON_CLIENT_WRAPPER_INCLUDE_FLUTTER_WINDOWING_H_
#define FLUTTER_SHELL_PLATFORM_COMMON_CLIENT_WRAPPER_INCLUDE_FLUTTER_WINDOWING_H_

#include <optional>

namespace flutter {

// The unique identifier for a view.
using FlutterViewId = int64_t;

// A point (x, y) in 2D space for window positioning.
struct WindowPoint {
  int x{0};
  int y{0};

  friend auto operator+(WindowPoint const& lhs,
                        WindowPoint const& rhs) -> WindowPoint {
    return {lhs.x + rhs.x, lhs.y + rhs.y};
  }

  friend auto operator-(WindowPoint const& lhs,
                        WindowPoint const& rhs) -> WindowPoint {
    return {lhs.x - rhs.x, lhs.y - rhs.y};
  }

  friend bool operator==(WindowPoint const& lhs, WindowPoint const& rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y;
  }
};

// A size (width, height) in 2D space.
struct WindowSize {
  int width{0};
  int height{0};

  explicit operator WindowPoint() const { return {width, height}; }

  friend bool operator==(WindowSize const& lhs, WindowSize const& rhs) {
    return lhs.width == rhs.width && lhs.height == rhs.height;
  }
};

// A rectangular area defined by a top-left point and size.
struct WindowRectangle {
  WindowPoint top_left;
  WindowSize size;

  // Checks if this rectangle fully contains |rect|.
  // Note: An empty rectangle can still contain other empty rectangles,
  // which are treated as points or lines of thickness zero
  auto contains(WindowRectangle const& rect) const -> bool {
    return rect.top_left.x >= top_left.x &&
           rect.top_left.x + rect.size.width <= top_left.x + size.width &&
           rect.top_left.y >= top_left.y &&
           rect.top_left.y + rect.size.height <= top_left.y + size.height;
  }

  friend bool operator==(WindowRectangle const& lhs,
                         WindowRectangle const& rhs) {
    return lhs.top_left == rhs.top_left && lhs.size == rhs.size;
  }
};

// Defines how a child window should be positioned relative to its parent.
struct WindowPositioner {
  // Allowed anchor positions.
  enum class Anchor {
    center,        // Center.
    top,           // Top, centered horizontally.
    bottom,        // Bottom, centered horizontally.
    left,          // Left, centered vertically.
    right,         // Right, centered vertically.
    top_left,      // Top-left corner.
    bottom_left,   // Bottom-left corner.
    top_right,     // Top-right corner.
    bottom_right,  // Bottom-right corner.
  };

  // Specifies how a window should be adjusted if it doesn't fit the placement
  // bounds. In order of precedence:
  // 1. 'flip_{x|y|any}': reverse the anchor points and offset along an axis.
  // 2. 'slide_{x|y|any}': adjust the offset along an axis.
  // 3. 'resize_{x|y|any}': adjust the window size along an axis.
  enum class ConstraintAdjustment {
    none = 0,                          // No adjustment.
    slide_x = 1 << 0,                  // Slide horizontally to fit.
    slide_y = 1 << 1,                  // Slide vertically to fit.
    flip_x = 1 << 2,                   // Flip horizontally to fit.
    flip_y = 1 << 3,                   // Flip vertically to fit.
    resize_x = 1 << 4,                 // Resize horizontally to fit.
    resize_y = 1 << 5,                 // Resize vertically to fit.
    flip_any = flip_x | flip_y,        // Flip in any direction to fit.
    slide_any = slide_x | slide_y,     // Slide in any direction to fit.
    resize_any = resize_x | resize_y,  // Resize in any direction to fit.
  };

  // The reference anchor rectangle relative to the client rectangle of the
  // parent window. If nullopt, the anchor rectangle is assumed to be the window
  // rectangle.
  std::optional<WindowRectangle> anchor_rect;
  // Specifies which anchor of the parent window to align to.
  Anchor parent_anchor{Anchor::center};
  // Specifies which anchor of the child window to align with the parent.
  Anchor child_anchor{Anchor::center};
  // Offset relative to the position of the anchor on the anchor rectangle and
  // the anchor on the child.
  WindowPoint offset;
  // The adjustments to apply if the window doesn't fit the available space.
  // The order of precedence is: 1) Flip, 2) Slide, 3) Resize.
  ConstraintAdjustment constraint_adjustment{ConstraintAdjustment::none};
};

// Types of windows.
enum class WindowArchetype {
  // Regular top-level window.
  regular,
  // A window that is on a layer above regular windows and is not dockable.
  floating_regular,
  // Dialog window.
  dialog,
  // Satellite window attached to a regular, floating_regular or dialog window.
  satellite,
  // Popup.
  popup,
  // Tooltip.
  tip,
};

// Window metadata returned as the result of creating a Flutter window.
struct WindowMetadata {
  // The ID of the view used for this window, which is unique to each window.
  FlutterViewId view_id{0};
  // The type of the window (e.g., regular, dialog, popup, etc).
  WindowArchetype archetype{WindowArchetype::regular};
  // Size of the created window, in logical coordinates.
  WindowSize size;
  // The ID of the view used by the parent window. If not set, the window is
  // assumed a top-level window.
  std::optional<FlutterViewId> parent_id;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_COMMON_CLIENT_WRAPPER_INCLUDE_FLUTTER_WINDOWING_H_
