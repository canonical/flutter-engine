#ifndef FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_FLUTTER_WINDOW_TYPES_H_
#define FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_FLUTTER_WINDOW_TYPES_H_

#include "flutter_view.h"

#include <optional>

namespace flutter {

struct FlutterWindowSize {
  int width{0};
  int height{0};
};

struct FlutterWindowPoint {
  int x{0};
  int y{0};

  auto operator+(FlutterWindowSize const& size) const -> FlutterWindowPoint {
    return {x + size.width, y + size.height};
  }

  friend auto operator+(FlutterWindowPoint const& lhs,
                        FlutterWindowPoint const& rhs) -> FlutterWindowPoint {
    return {lhs.x + rhs.x, lhs.y + rhs.y};
  }
};

struct FlutterWindowRectangle {
  FlutterWindowPoint top_left;
  FlutterWindowSize size;

  // Checks if this rectangle fully contains |rect|.
  // Note: An empty rectangle can still contain other empty rectangles,
  // which are treated as points or lines of thickness zero
  auto contains(FlutterWindowRectangle const& rect) const -> bool {
    return rect.top_left.x >= top_left.x &&
           rect.top_left.x + rect.size.width <= top_left.x + size.width &&
           rect.top_left.y >= top_left.y &&
           rect.top_left.y + rect.size.height <= top_left.y + size.height;
  }
};

struct FlutterWindowPositioner {
  enum class Anchor {
    none,
    top,
    bottom,
    left,
    right,
    top_left,
    bottom_left,
    top_right,
    bottom_right,
  };

  enum ConstraintAdjustment {
    none = 0,
    slide_x = 1 << 0,
    slide_y = 1 << 1,
    flip_x = 1 << 2,
    flip_y = 1 << 3,
    resize_x = 1 << 4,
    resize_y = 1 << 5,
    antipodes = 1 << 6,
    flip_any = flip_x | flip_y,
    slide_any = slide_x | slide_y,
    resize_any = resize_x | resize_y,
  };

  std::optional<FlutterWindowRectangle> anchor_rect;
  Anchor parent_anchor{Anchor::none};
  Anchor child_anchor{Anchor::none};
  FlutterWindowPoint offset;
  ConstraintAdjustment constraint_adjustment{ConstraintAdjustment::none};
};

enum class FlutterWindowArchetype {
  regular,
  floating_regular,
  dialog,
  satellite,
  popup,
  tip,
};

struct FlutterWindowCreationResult {
  FlutterViewId view_id{0};
  std::optional<FlutterViewId> parent_id;
  FlutterWindowArchetype archetype{FlutterWindowArchetype::regular};
  FlutterWindowSize size;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_FLUTTER_WINDOW_TYPES_H_
