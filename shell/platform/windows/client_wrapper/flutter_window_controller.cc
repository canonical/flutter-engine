#include "include/flutter/flutter_window_controller.h"

#include "include/flutter/encodable_value.h"
#include "include/flutter/standard_method_codec.h"

#include <algorithm>

#include <dwmapi.h>

namespace {
auto const* const kChannel{"flutter/windowing"};
auto const kBaseDpi{static_cast<double>(USER_DEFAULT_SCREEN_DPI)};

// Encodes the attributes of a FlutterWindowCreationResult into an EncodableMap
// wrapped in an EncodableValue.
flutter::EncodableValue encodeWindowCreationResult(
    flutter::FlutterWindowCreationResult const& result) {
  return flutter::EncodableValue(flutter::EncodableMap{
      {flutter::EncodableValue("viewId"),
       flutter::EncodableValue(result.view_id)},
      {flutter::EncodableValue("parentViewId"),
       result.parent_id ? flutter::EncodableValue(*result.parent_id)
                        : flutter::EncodableValue()},
      {flutter::EncodableValue("archetype"),
       flutter::EncodableValue(static_cast<int>(result.archetype))},
      {flutter::EncodableValue("width"),
       flutter::EncodableValue(result.size.width)},
      {flutter::EncodableValue("height"),
       flutter::EncodableValue((result.size.height))}});
}

// Returns the origin point that will center a window of size 'size' within the
// client area of the window identified by 'handle'.
auto calculateCenteredOrigin(flutter::Win32Window::Size size,
                             HWND handle) -> flutter::Win32Window::Point {
  if (RECT frame; handle && GetWindowRect(handle, &frame)) {
    POINT const target_point{frame.left, frame.top};
    auto* const monitor{
        MonitorFromPoint(target_point, MONITOR_DEFAULTTONEAREST)};
    auto const dpr{FlutterDesktopGetDpiForMonitor(monitor) / kBaseDpi};
    auto const centered_x{(frame.left + frame.right - size.width * dpr) / 2.0};
    auto const centered_y{(frame.top + frame.bottom - size.height * dpr) / 2.0};
    return {static_cast<unsigned int>(centered_x / dpr),
            static_cast<unsigned int>(centered_y / dpr)};
  }
  return {0, 0};
}

std::tuple<flutter::Win32Window::Point, flutter::Win32Window::Size>
applyPositioner(flutter::FlutterWindowPositioner const& positioner,
                flutter::Win32Window::Size const& size,
                flutter::FlutterViewId parent_view_id) {
  auto const& windows{flutter::FlutterWindowController::instance().windows()};
  auto const& parent_window{windows.at(parent_view_id)};
  auto const& parent_hwnd{parent_window->GetHandle()};
  auto const dpr{FlutterDesktopGetDpiForHWND(parent_hwnd) / kBaseDpi};
  auto const monitor_rect{[](HWND hwnd) -> RECT {
    auto* monitor{MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)};
    MONITORINFO mi;
    mi.cbSize = sizeof(MONITORINFO);
    return GetMonitorInfo(monitor, &mi) ? mi.rcMonitor : RECT{0, 0, 0, 0};
  }(parent_hwnd)};

  struct RectF {
    double left;
    double top;
    double right;
    double bottom;
  };
  struct PointF {
    double x;
    double y;
  };

  auto const anchor_rect{[&]() -> RectF {
    if (positioner.anchor_rect) {
      // If the positioner's anchor rect is not std::nullopt, use it to anchor
      // relative to the client area
      RECT rect;
      GetClientRect(parent_hwnd, &rect);
      POINT top_left{rect.left, rect.top};
      ClientToScreen(parent_hwnd, &top_left);
      POINT bottom_right{rect.right, rect.bottom};
      ClientToScreen(parent_hwnd, &bottom_right);

      RectF anchor_rect_screen_space{
          .left = top_left.x + positioner.anchor_rect->x * dpr,
          .top = top_left.y + positioner.anchor_rect->y * dpr,
          .right =
              top_left.x +
              (positioner.anchor_rect->x + positioner.anchor_rect->width) * dpr,
          .bottom = top_left.y + (positioner.anchor_rect->y +
                                  positioner.anchor_rect->height) *
                                     dpr};
      // Ensure the anchor rect stays within the bounds of the client rect
      anchor_rect_screen_space.left = std::clamp(
          anchor_rect_screen_space.left, static_cast<double>(top_left.x),
          static_cast<double>(bottom_right.x));
      anchor_rect_screen_space.top = std::clamp(
          anchor_rect_screen_space.top, static_cast<double>(top_left.y),
          static_cast<double>(bottom_right.y));
      anchor_rect_screen_space.right = std::clamp(
          anchor_rect_screen_space.right, static_cast<double>(top_left.x),
          static_cast<double>(bottom_right.x));
      anchor_rect_screen_space.bottom = std::clamp(
          anchor_rect_screen_space.bottom, static_cast<double>(top_left.y),
          static_cast<double>(bottom_right.y));
      return anchor_rect_screen_space;
    } else {
      // If the positioner's anchor rect is std::nullopt, create an anchor rect
      // that is equal to the window frame area
      RECT frame_rect;
      DwmGetWindowAttribute(parent_hwnd, DWMWA_EXTENDED_FRAME_BOUNDS,
                            &frame_rect, sizeof(frame_rect));
      return {static_cast<double>(frame_rect.left),
              static_cast<double>(frame_rect.top),
              static_cast<double>(frame_rect.right),
              static_cast<double>(frame_rect.bottom)};
    }
  }()};

  // TODO: check if centering is being done correctly

  PointF const center{.x = (anchor_rect.left + anchor_rect.right) / 2.0,
                      .y = (anchor_rect.top + anchor_rect.bottom) / 2.0};
  PointF child_size{size.width * dpr, size.height * dpr};
  PointF const child_center{child_size.x / 2.0, child_size.y / 2.0};

  auto const get_parent_anchor_point{
      [&](flutter::FlutterWindowPositioner::Anchor anchor) -> PointF {
        switch (anchor) {
          case flutter::FlutterWindowPositioner::Anchor::top:
            return {center.x, anchor_rect.top};
          case flutter::FlutterWindowPositioner::Anchor::bottom:
            return {center.x, anchor_rect.bottom};
          case flutter::FlutterWindowPositioner::Anchor::left:
            return {anchor_rect.left, center.y};
          case flutter::FlutterWindowPositioner::Anchor::right:
            return {anchor_rect.right, center.y};
          case flutter::FlutterWindowPositioner::Anchor::top_left:
            return {anchor_rect.left, anchor_rect.top};
          case flutter::FlutterWindowPositioner::Anchor::bottom_left:
            return {anchor_rect.left, anchor_rect.bottom};
          case flutter::FlutterWindowPositioner::Anchor::top_right:
            return {anchor_rect.right, anchor_rect.top};
          case flutter::FlutterWindowPositioner::Anchor::bottom_right:
            return {anchor_rect.right, anchor_rect.bottom};
          default:
            return center;
        }
      }};

  auto const get_child_anchor_point{
      [&](flutter::FlutterWindowPositioner::Gravity gravity) -> PointF {
        switch (gravity) {
          case flutter::FlutterWindowPositioner::Gravity::top:
            return {-child_center.x, -child_size.x};
          case flutter::FlutterWindowPositioner::Gravity::bottom:
            return {-child_center.x, 0};
          case flutter::FlutterWindowPositioner::Gravity::left:
            return {-child_size.x, -child_center.y};
          case flutter::FlutterWindowPositioner::Gravity::right:
            return {0, -child_center.y};
          case flutter::FlutterWindowPositioner::Gravity::top_left:
            return {-child_size.x, -child_size.y};
          case flutter::FlutterWindowPositioner::Gravity::bottom_left:
            return {-child_size.x, 0};
          case flutter::FlutterWindowPositioner::Gravity::top_right:
            return {0, -child_size.y};
          case flutter::FlutterWindowPositioner::Gravity::bottom_right:
            return {0, 0};
          default:
            return {-child_center.x, -child_center.y};
        }
      }};

  auto const calculate_origin{[](PointF const& parent_anchor,
                                 PointF const& child_anchor,
                                 PointF const& offset) -> PointF {
    return {.x = parent_anchor.x + child_anchor.x + offset.x,
            .y = parent_anchor.y + child_anchor.y + offset.y};
  }};

  auto anchor{positioner.anchor};
  auto gravity{positioner.gravity};
  PointF offset{static_cast<double>(positioner.offset.dx),
                static_cast<double>(positioner.offset.dy)};

  auto parent_anchor_point{get_parent_anchor_point(anchor)};
  auto child_anchor_point{get_child_anchor_point(gravity)};
  PointF origin_dc{
      calculate_origin(parent_anchor_point, child_anchor_point, offset)};

  // Constraint adjustments

  auto const is_constrained_along_x{[&]() {
    return origin_dc.x < 0 || origin_dc.x + child_size.x > monitor_rect.right;
  }};
  auto const is_constrained_along_y{[&]() {
    return origin_dc.y < 0 || origin_dc.y + child_size.y > monitor_rect.bottom;
  }};

  // X axis
  if (is_constrained_along_x()) {
    auto const reverse_anchor_along_x{
        [](flutter::FlutterWindowPositioner::Anchor anchor) {
          switch (anchor) {
            case flutter::FlutterWindowPositioner::Anchor::left:
              return flutter::FlutterWindowPositioner::Anchor::right;
            case flutter::FlutterWindowPositioner::Anchor::right:
              return flutter::FlutterWindowPositioner::Anchor::left;
            case flutter::FlutterWindowPositioner::Anchor::top_left:
              return flutter::FlutterWindowPositioner::Anchor::top_right;
            case flutter::FlutterWindowPositioner::Anchor::bottom_left:
              return flutter::FlutterWindowPositioner::Anchor::bottom_right;
            case flutter::FlutterWindowPositioner::Anchor::top_right:
              return flutter::FlutterWindowPositioner::Anchor::top_left;
            case flutter::FlutterWindowPositioner::Anchor::bottom_right:
              return flutter::FlutterWindowPositioner::Anchor::bottom_left;
            default:
              return anchor;
          }
        }};

    auto const reverse_gravity_along_x{
        [](flutter::FlutterWindowPositioner::Gravity gravity) {
          switch (gravity) {
            case flutter::FlutterWindowPositioner::Gravity::left:
              return flutter::FlutterWindowPositioner::Gravity::right;
            case flutter::FlutterWindowPositioner::Gravity::right:
              return flutter::FlutterWindowPositioner::Gravity::left;
            case flutter::FlutterWindowPositioner::Gravity::top_left:
              return flutter::FlutterWindowPositioner::Gravity::top_right;
            case flutter::FlutterWindowPositioner::Gravity::bottom_left:
              return flutter::FlutterWindowPositioner::Gravity::bottom_right;
            case flutter::FlutterWindowPositioner::Gravity::top_right:
              return flutter::FlutterWindowPositioner::Gravity::top_left;
            case flutter::FlutterWindowPositioner::Gravity::bottom_right:
              return flutter::FlutterWindowPositioner::Gravity::bottom_left;
            default:
              return gravity;
          }
        }};

    if (positioner.constraint_adjustment &
        static_cast<uint32_t>(
            flutter::FlutterWindowPositioner::ConstraintAdjustment::flip_x)) {
      anchor = reverse_anchor_along_x(anchor);
      gravity = reverse_gravity_along_x(gravity);
      parent_anchor_point = get_parent_anchor_point(anchor);
      child_anchor_point = get_child_anchor_point(gravity);
      auto const saved_origin_dc{std::exchange(
          origin_dc,
          calculate_origin(parent_anchor_point, child_anchor_point, offset))};
      if (is_constrained_along_x()) {
        origin_dc = saved_origin_dc;
      }
    } else if (positioner.constraint_adjustment &
               static_cast<uint32_t>(flutter::FlutterWindowPositioner::
                                         ConstraintAdjustment::slide_x)) {
      // TODO: Slide towards the direction of the gravity first
      if (origin_dc.x < 0) {
        auto const diff{abs(origin_dc.x)};
        offset = {offset.x + diff, offset.y};
        origin_dc =
            calculate_origin(parent_anchor_point, child_anchor_point, offset);
      }
      if (origin_dc.x + child_size.x > monitor_rect.right) {
        auto const diff{(origin_dc.x + child_size.x) - monitor_rect.right};
        offset = {offset.x - diff, offset.y};
        origin_dc =
            calculate_origin(parent_anchor_point, child_anchor_point, offset);
      }
    } else if (positioner.constraint_adjustment &
               static_cast<uint32_t>(flutter::FlutterWindowPositioner::
                                         ConstraintAdjustment::resize_x)) {
      if (origin_dc.x < 0) {
        auto const diff{std::clamp(abs(origin_dc.x), 1.0, child_size.x - 1)};
        origin_dc.x += diff;
        child_size.x -= diff;
      }
      if (origin_dc.x + child_size.x > monitor_rect.right) {
        auto const diff{
            std::clamp((origin_dc.x + child_size.x) - monitor_rect.right, 1.0,
                       child_size.x - 1)};
        child_size.x -= diff;
      }
    }
  }

  // Y axis
  if (is_constrained_along_y()) {
    auto const reverse_anchor_along_y{
        [](flutter::FlutterWindowPositioner::Anchor anchor) {
          switch (anchor) {
            case flutter::FlutterWindowPositioner::Anchor::top:
              return flutter::FlutterWindowPositioner::Anchor::bottom;
            case flutter::FlutterWindowPositioner::Anchor::bottom:
              return flutter::FlutterWindowPositioner::Anchor::top;
            case flutter::FlutterWindowPositioner::Anchor::top_left:
              return flutter::FlutterWindowPositioner::Anchor::bottom_left;
            case flutter::FlutterWindowPositioner::Anchor::bottom_left:
              return flutter::FlutterWindowPositioner::Anchor::top_left;
            case flutter::FlutterWindowPositioner::Anchor::top_right:
              return flutter::FlutterWindowPositioner::Anchor::bottom_right;
            case flutter::FlutterWindowPositioner::Anchor::bottom_right:
              return flutter::FlutterWindowPositioner::Anchor::top_right;
            default:
              return anchor;
          }
        }};

    auto const reverse_gravity_along_y{
        [](flutter::FlutterWindowPositioner::Gravity gravity) {
          switch (gravity) {
            case flutter::FlutterWindowPositioner::Gravity::top:
              return flutter::FlutterWindowPositioner::Gravity::bottom;
            case flutter::FlutterWindowPositioner::Gravity::bottom:
              return flutter::FlutterWindowPositioner::Gravity::top;
            case flutter::FlutterWindowPositioner::Gravity::top_left:
              return flutter::FlutterWindowPositioner::Gravity::bottom_left;
            case flutter::FlutterWindowPositioner::Gravity::bottom_left:
              return flutter::FlutterWindowPositioner::Gravity::top_left;
            case flutter::FlutterWindowPositioner::Gravity::top_right:
              return flutter::FlutterWindowPositioner::Gravity::bottom_right;
            case flutter::FlutterWindowPositioner::Gravity::bottom_right:
              return flutter::FlutterWindowPositioner::Gravity::top_right;
            default:
              return gravity;
          }
        }};

    if (positioner.constraint_adjustment &
        static_cast<uint32_t>(
            flutter::FlutterWindowPositioner::ConstraintAdjustment::flip_y)) {
      anchor = reverse_anchor_along_y(anchor);
      gravity = reverse_gravity_along_y(gravity);
      parent_anchor_point = get_parent_anchor_point(anchor);
      child_anchor_point = get_child_anchor_point(gravity);
      auto const saved_origin_dc{std::exchange(
          origin_dc,
          calculate_origin(parent_anchor_point, child_anchor_point, offset))};
      if (is_constrained_along_y()) {
        origin_dc = saved_origin_dc;
      }
    } else if (positioner.constraint_adjustment &
               static_cast<uint32_t>(flutter::FlutterWindowPositioner::
                                         ConstraintAdjustment::slide_y)) {
      // TODO: Slide towards the direction of the gravity first
      if (origin_dc.y < 0) {
        auto const diff{abs(origin_dc.y)};
        offset = {offset.x, offset.y + diff};
        origin_dc =
            calculate_origin(parent_anchor_point, child_anchor_point, offset);
      }
      if (origin_dc.y + child_size.y > monitor_rect.bottom) {
        auto const diff{(origin_dc.y + child_size.y) - monitor_rect.bottom};
        offset = {offset.x, offset.y - diff};
        origin_dc =
            calculate_origin(parent_anchor_point, child_anchor_point, offset);
      }
    } else if (positioner.constraint_adjustment &
               static_cast<uint32_t>(flutter::FlutterWindowPositioner::
                                         ConstraintAdjustment::resize_y)) {
      if (origin_dc.y < 0) {
        auto const diff{std::clamp(abs(origin_dc.y), 1.0, child_size.y - 1)};
        origin_dc.y += diff;
        child_size.y -= diff;
      }
      if (origin_dc.y + child_size.y > monitor_rect.bottom) {
        auto const diff{
            std::clamp((origin_dc.y + child_size.y) - monitor_rect.bottom, 1.0,
                       child_size.y - 1)};
        child_size.y -= diff;
      }
    }
  }

  flutter::Win32Window::Point const origin_lc{
      static_cast<unsigned int>(origin_dc.x / dpr),
      static_cast<unsigned int>(origin_dc.y / dpr)};
  flutter::Win32Window::Size const new_size{
      static_cast<unsigned int>(child_size.x / dpr),
      static_cast<unsigned int>(child_size.y / dpr)};
  return {origin_lc, new_size};
}

void handleCreateRegularWindow(
    flutter::MethodCall<> const& call,
    std::unique_ptr<flutter::MethodResult<>>& result) {
  auto const* const arguments{call.arguments()};
  if (auto const* const map{std::get_if<flutter::EncodableMap>(arguments)}) {
    auto const width_it{map->find(flutter::EncodableValue("width"))};
    auto const height_it{map->find(flutter::EncodableValue("height"))};
    if (width_it != map->end() && height_it != map->end()) {
      auto const* const width{std::get_if<int>(&width_it->second)};
      auto const* const height{std::get_if<int>(&height_it->second)};
      if (width && height) {
        flutter::Win32Window::Size const size{
            static_cast<unsigned int>(*width),
            static_cast<unsigned int>(*height)};

        if (auto const data{flutter::FlutterWindowController::instance()
                                .createRegularWindow(L"regular", size)}) {
          result->Success(encodeWindowCreationResult(data.value()));
        } else {
          result->Error("UNAVAILABLE", "Can't create window.");
        }
      } else {
        result->Error("INVALID_VALUE",
                      "Values for {'width', 'height'} must be of type int.");
      }
    } else {
      result->Error(
          "INVALID_VALUE",
          "Map does not contain all required keys: {'width', 'height'}.");
    }
  } else {
    result->Error("INVALID_VALUE", "Value argument is not a map.");
  }
}

void handleCreateDialogWindow(
    flutter::MethodCall<> const& call,
    std::unique_ptr<flutter::MethodResult<>>& result) {
  auto const* const arguments{call.arguments()};
  if (auto const* const map{std::get_if<flutter::EncodableMap>(arguments)}) {
    auto const parent_it{map->find(flutter::EncodableValue("parent"))};
    auto const size_it{map->find(flutter::EncodableValue("size"))};

    if (parent_it != map->end() && size_it != map->end()) {
      auto const* const parent{std::get_if<int>(&parent_it->second)};
      if (!parent) {
        result->Error("INVALID_VALUE",
                      "Value for 'parent' must be of type int.");
        return;
      }

      auto const* const size_list{
          std::get_if<std::vector<flutter::EncodableValue>>(&size_it->second)};
      if (size_list->size() != 2 ||
          !std::holds_alternative<int>(size_list->at(0)) ||
          !std::holds_alternative<int>(size_list->at(1))) {
        result->Error("INVALID_VALUE",
                      "Values for 'size' must be of type int.");
        return;
      }
      auto const width{std::get<int>(size_list->at(0))};
      auto const height{std::get<int>(size_list->at(1))};
      flutter::Win32Window::Size const size{static_cast<unsigned int>(width),
                                            static_cast<unsigned int>(height)};

      auto const origin{
          [&size, &parent]() -> std::optional<flutter::Win32Window::Point> {
            if (parent) {
              auto const& windows{
                  flutter::FlutterWindowController::instance().windows()};
              if (windows.find(*parent) != windows.end()) {
                return calculateCenteredOrigin(
                    size, windows.at(*parent)->GetHandle());
              }
            }
            return std::nullopt;
          }()};

      if (auto const data{
              flutter::FlutterWindowController::instance().createDialogWindow(
                  L"dialog", size, origin,
                  *parent >= 0 ? std::optional<flutter::FlutterViewId>{*parent}
                               : std::nullopt)}) {
        result->Success(encodeWindowCreationResult(data.value()));
      } else {
        result->Error("UNAVAILABLE", "Can't create window.");
      }
    } else {
      result->Error(
          "INVALID_VALUE",
          "Map does not contain all required keys: {'parent', 'size'}.");
    }
  } else {
    result->Error("INVALID_VALUE", "Value argument is not a map.");
  }
}

void handleCreateSatelliteWindow(
    flutter::MethodCall<> const& call,
    std::unique_ptr<flutter::MethodResult<>>& result) {
  auto const* const arguments{call.arguments()};
  if (auto const* const map{std::get_if<flutter::EncodableMap>(arguments)}) {
    auto const parent_it{map->find(flutter::EncodableValue("parent"))};
    auto const size_it{map->find(flutter::EncodableValue("size"))};
    auto const anchor_rect_it{map->find(flutter::EncodableValue("anchorRect"))};
    auto const positioner_parent_anchor_it{
        map->find(flutter::EncodableValue("positionerParentAnchor"))};
    auto const positioner_child_anchor_it{
        map->find(flutter::EncodableValue("positionerChildAnchor"))};
    auto const positioner_offset_it{
        map->find(flutter::EncodableValue("positionerOffset"))};
    auto const positioner_constraint_adjustment_it{
        map->find(flutter::EncodableValue("positionerConstraintAdjustment"))};

    if (parent_it != map->end() && size_it != map->end() &&
        anchor_rect_it != map->end() &&
        positioner_parent_anchor_it != map->end() &&
        positioner_child_anchor_it != map->end() &&
        positioner_offset_it != map->end() &&
        positioner_constraint_adjustment_it != map->end()) {
      // parent
      auto const* const parent{std::get_if<int>(&parent_it->second)};
      if (!parent) {
        result->Error("INVALID_VALUE",
                      "Value for 'parent' must be of type int.");
        return;
      }

      // size
      auto const* const size_list{
          std::get_if<std::vector<flutter::EncodableValue>>(&size_it->second)};
      if (size_list->size() != 2 ||
          !std::holds_alternative<int>(size_list->at(0)) ||
          !std::holds_alternative<int>(size_list->at(1))) {
        result->Error("INVALID_VALUE",
                      "Values for 'size' must be of type int.");
        return;
      }
      auto const width{std::get<int>(size_list->at(0))};
      auto const height{std::get<int>(size_list->at(1))};
      flutter::Win32Window::Size const size{static_cast<unsigned int>(width),
                                            static_cast<unsigned int>(height)};

      // anchorRect
      std::optional<flutter::FlutterWindowPositioner::Rect> anchor_rect;
      if (auto const* const anchor_rect_list{
              std::get_if<std::vector<flutter::EncodableValue>>(
                  &anchor_rect_it->second)}) {
        if (anchor_rect_list->size() != 4) {
          result->Error(
              "INVALID_VALUE",
              "Values for 'anchorRect' must be an array of 4 integers.");
          return;
        } else if (!std::holds_alternative<int>(anchor_rect_list->at(0)) ||
                   !std::holds_alternative<int>(anchor_rect_list->at(1)) ||
                   !std::holds_alternative<int>(anchor_rect_list->at(2)) ||
                   !std::holds_alternative<int>(anchor_rect_list->at(3))) {
          result->Error("INVALID_VALUE",
                        "Values for 'anchorRect' must be of type int.");
          return;
        }
        anchor_rect = flutter::FlutterWindowPositioner::Rect{
            .x = std::get<int>(anchor_rect_list->at(0)),
            .y = std::get<int>(anchor_rect_list->at(1)),
            .width = std::get<int>(anchor_rect_list->at(2)),
            .height = std::get<int>(anchor_rect_list->at(3))};
      }

      // positionerParentAnchor
      auto const* const positioner_parent_anchor{
          std::get_if<int>(&positioner_parent_anchor_it->second)};
      if (!positioner_parent_anchor) {
        result->Error(
            "INVALID_VALUE",
            "Value for 'positionerParentAnchor' must be of type int.");
        return;
      }

      // positionerChildAnchor
      auto const* const positioner_child_anchor{
          std::get_if<int>(&positioner_child_anchor_it->second)};
      if (!positioner_child_anchor) {
        result->Error("INVALID_VALUE",
                      "Value for 'positionerChildAnchor' must be of type int.");
        return;
      }
      // Convert from anchor (originally a FlutterWindowPositionerAnchor) to
      // flutter::FlutterWindowPositioner::Gravity
      auto const gravity{[](flutter::FlutterWindowPositioner::Anchor anchor)
                             -> flutter::FlutterWindowPositioner::Gravity {
        switch (anchor) {
          case flutter::FlutterWindowPositioner::Anchor::none:
            return flutter::FlutterWindowPositioner::Gravity::none;
          case flutter::FlutterWindowPositioner::Anchor::top:
            return flutter::FlutterWindowPositioner::Gravity::bottom;
          case flutter::FlutterWindowPositioner::Anchor::bottom:
            return flutter::FlutterWindowPositioner::Gravity::top;
          case flutter::FlutterWindowPositioner::Anchor::left:
            return flutter::FlutterWindowPositioner::Gravity::right;
          case flutter::FlutterWindowPositioner::Anchor::right:
            return flutter::FlutterWindowPositioner::Gravity::left;
          case flutter::FlutterWindowPositioner::Anchor::top_left:
            return flutter::FlutterWindowPositioner::Gravity::bottom_right;
          case flutter::FlutterWindowPositioner::Anchor::bottom_left:
            return flutter::FlutterWindowPositioner::Gravity::top_right;
          case flutter::FlutterWindowPositioner::Anchor::top_right:
            return flutter::FlutterWindowPositioner::Gravity::bottom_left;
          case flutter::FlutterWindowPositioner::Anchor::bottom_right:
            return flutter::FlutterWindowPositioner::Gravity::top_left;
          default:
            return flutter::FlutterWindowPositioner::Gravity::none;
        }
      }(static_cast<flutter::FlutterWindowPositioner::Anchor>(
                                 *positioner_child_anchor))};

      // positionerOffset
      auto const* const positioner_offset_list{
          std::get_if<std::vector<flutter::EncodableValue>>(
              &positioner_offset_it->second)};
      if (positioner_offset_list->size() != 2 ||
          !std::holds_alternative<int>(size_list->at(0)) ||
          !std::holds_alternative<int>(size_list->at(1))) {
        result->Error("INVALID_VALUE",
                      "Values for 'positionerOffset' must be of type int.");
        return;
      }
      auto const dx{std::get<int>(positioner_offset_list->at(0))};
      auto const dy{std::get<int>(positioner_offset_list->at(1))};

      // positionerConstraintAdjustment
      auto const* const positioner_constraint_adjustment{
          std::get_if<int>(&positioner_constraint_adjustment_it->second)};
      if (!positioner_constraint_adjustment) {
        result->Error(
            "INVALID_VALUE",
            "Value for 'positionerConstraintAdjustment' must be of type int.");
        return;
      }

      flutter::FlutterWindowPositioner const positioner{
          .anchor_rect = anchor_rect,
          .anchor = static_cast<flutter::FlutterWindowPositioner::Anchor>(
              *positioner_parent_anchor),
          .gravity = gravity,
          .offset = {.dx = dx, .dy = dy},
          .constraint_adjustment =
              static_cast<uint32_t>(*positioner_constraint_adjustment)};

      auto const& [origin,
                   new_size]{applyPositioner(positioner, size, *parent)};

      if (auto const data{flutter::FlutterWindowController::instance()
                              .createSatelliteWindow(L"satellite", origin,
                                                     new_size, *parent)}) {
        result->Success(encodeWindowCreationResult(data.value()));
      } else {
        result->Error("UNAVAILABLE", "Can't create window.");
      }
    } else {
      result->Error("INVALID_VALUE",
                    "Map does not contain all required keys: "
                    "{'parent', 'size', 'anchorRect', "
                    "'positionerParentAnchor', 'positionerChildAnchor', "
                    "'positionerOffset', 'positionerConstraintAdjustment'}.");
    }
  } else {
    result->Error("INVALID_VALUE", "Value argument is not a map.");
  }
}

void handleCreatePopupWindow(flutter::MethodCall<> const& call,
                             std::unique_ptr<flutter::MethodResult<>>& result) {
  auto const* const arguments{call.arguments()};
  if (auto const* const map{std::get_if<flutter::EncodableMap>(arguments)}) {
    auto const parent_it{map->find(flutter::EncodableValue("parent"))};
    auto const size_it{map->find(flutter::EncodableValue("size"))};
    auto const anchor_rect_it{map->find(flutter::EncodableValue("anchorRect"))};
    auto const positioner_parent_anchor_it{
        map->find(flutter::EncodableValue("positionerParentAnchor"))};
    auto const positioner_child_anchor_it{
        map->find(flutter::EncodableValue("positionerChildAnchor"))};
    auto const positioner_offset_it{
        map->find(flutter::EncodableValue("positionerOffset"))};
    auto const positioner_constraint_adjustment_it{
        map->find(flutter::EncodableValue("positionerConstraintAdjustment"))};

    if (parent_it != map->end() && size_it != map->end() &&
        anchor_rect_it != map->end() &&
        positioner_parent_anchor_it != map->end() &&
        positioner_child_anchor_it != map->end() &&
        positioner_offset_it != map->end() &&
        positioner_constraint_adjustment_it != map->end()) {
      // parent
      auto const* const parent{std::get_if<int>(&parent_it->second)};
      if (!parent) {
        result->Error("INVALID_VALUE",
                      "Value for 'parent' must be of type int.");
        return;
      }

      // size
      auto const* const size_list{
          std::get_if<std::vector<flutter::EncodableValue>>(&size_it->second)};
      if (size_list->size() != 2 ||
          !std::holds_alternative<int>(size_list->at(0)) ||
          !std::holds_alternative<int>(size_list->at(1))) {
        result->Error("INVALID_VALUE",
                      "Values for 'size' must be of type int.");
        return;
      }
      auto const width{std::get<int>(size_list->at(0))};
      auto const height{std::get<int>(size_list->at(1))};
      flutter::Win32Window::Size const size{static_cast<unsigned int>(width),
                                            static_cast<unsigned int>(height)};

      // anchorRect
      std::optional<flutter::FlutterWindowPositioner::Rect> anchor_rect;
      if (auto const* const anchor_rect_list{
              std::get_if<std::vector<flutter::EncodableValue>>(
                  &anchor_rect_it->second)}) {
        if (anchor_rect_list->size() != 4) {
          result->Error(
              "INVALID_VALUE",
              "Values for 'anchorRect' must be an array of 4 integers.");
          return;
        } else if (!std::holds_alternative<int>(anchor_rect_list->at(0)) ||
                   !std::holds_alternative<int>(anchor_rect_list->at(1)) ||
                   !std::holds_alternative<int>(anchor_rect_list->at(2)) ||
                   !std::holds_alternative<int>(anchor_rect_list->at(3))) {
          result->Error("INVALID_VALUE",
                        "Values for 'anchorRect' must be of type int.");
          return;
        }
        anchor_rect = flutter::FlutterWindowPositioner::Rect{
            .x = std::get<int>(anchor_rect_list->at(0)),
            .y = std::get<int>(anchor_rect_list->at(1)),
            .width = std::get<int>(anchor_rect_list->at(2)),
            .height = std::get<int>(anchor_rect_list->at(3))};
      }

      // positionerParentAnchor
      auto const* const positioner_parent_anchor{
          std::get_if<int>(&positioner_parent_anchor_it->second)};
      if (!positioner_parent_anchor) {
        result->Error(
            "INVALID_VALUE",
            "Value for 'positionerParentAnchor' must be of type int.");
        return;
      }

      // positionerChildAnchor
      auto const* const positioner_child_anchor{
          std::get_if<int>(&positioner_child_anchor_it->second)};
      if (!positioner_child_anchor) {
        result->Error("INVALID_VALUE",
                      "Value for 'positionerChildAnchor' must be of type int.");
        return;
      }
      // Convert from anchor (originally a FlutterWindowPositionerAnchor) to
      // flutter::FlutterWindowPositioner::Gravity
      auto const gravity{[](flutter::FlutterWindowPositioner::Anchor anchor)
                             -> flutter::FlutterWindowPositioner::Gravity {
        switch (anchor) {
          case flutter::FlutterWindowPositioner::Anchor::none:
            return flutter::FlutterWindowPositioner::Gravity::none;
          case flutter::FlutterWindowPositioner::Anchor::top:
            return flutter::FlutterWindowPositioner::Gravity::bottom;
          case flutter::FlutterWindowPositioner::Anchor::bottom:
            return flutter::FlutterWindowPositioner::Gravity::top;
          case flutter::FlutterWindowPositioner::Anchor::left:
            return flutter::FlutterWindowPositioner::Gravity::right;
          case flutter::FlutterWindowPositioner::Anchor::right:
            return flutter::FlutterWindowPositioner::Gravity::left;
          case flutter::FlutterWindowPositioner::Anchor::top_left:
            return flutter::FlutterWindowPositioner::Gravity::bottom_right;
          case flutter::FlutterWindowPositioner::Anchor::bottom_left:
            return flutter::FlutterWindowPositioner::Gravity::top_right;
          case flutter::FlutterWindowPositioner::Anchor::top_right:
            return flutter::FlutterWindowPositioner::Gravity::bottom_left;
          case flutter::FlutterWindowPositioner::Anchor::bottom_right:
            return flutter::FlutterWindowPositioner::Gravity::top_left;
          default:
            return flutter::FlutterWindowPositioner::Gravity::none;
        }
      }(static_cast<flutter::FlutterWindowPositioner::Anchor>(
                                 *positioner_child_anchor))};

      // positionerOffset
      auto const* const positioner_offset_list{
          std::get_if<std::vector<flutter::EncodableValue>>(
              &positioner_offset_it->second)};
      if (positioner_offset_list->size() != 2 ||
          !std::holds_alternative<int>(size_list->at(0)) ||
          !std::holds_alternative<int>(size_list->at(1))) {
        result->Error("INVALID_VALUE",
                      "Values for 'positionerOffset' must be of type int.");
        return;
      }
      auto const dx{std::get<int>(positioner_offset_list->at(0))};
      auto const dy{std::get<int>(positioner_offset_list->at(1))};

      // positionerConstraintAdjustment
      auto const* const positioner_constraint_adjustment{
          std::get_if<int>(&positioner_constraint_adjustment_it->second)};
      if (!positioner_constraint_adjustment) {
        result->Error(
            "INVALID_VALUE",
            "Value for 'positionerConstraintAdjustment' must be of type int.");
        return;
      }

      flutter::FlutterWindowPositioner const positioner{
          .anchor_rect = anchor_rect,
          .anchor = static_cast<flutter::FlutterWindowPositioner::Anchor>(
              *positioner_parent_anchor),
          .gravity = gravity,
          .offset = {.dx = dx, .dy = dy},
          .constraint_adjustment =
              static_cast<uint32_t>(*positioner_constraint_adjustment)};

      auto const& [origin,
                   new_size]{applyPositioner(positioner, size, *parent)};

      if (auto const data{
              flutter::FlutterWindowController::instance().createPopupWindow(
                  L"popup", origin, new_size, *parent)}) {
        result->Success(encodeWindowCreationResult(data.value()));
      } else {
        result->Error("UNAVAILABLE", "Can't create window.");
      }
    } else {
      result->Error("INVALID_VALUE",
                    "Map does not contain all required keys: "
                    "{'parent', 'size', 'anchorRect', "
                    "'positionerParentAnchor', 'positionerChildAnchor', "
                    "'positionerOffset', 'positionerConstraintAdjustment'}.");
    }
  } else {
    result->Error("INVALID_VALUE", "Value argument is not a map.");
  }
}

void handleDestroyWindow(flutter::MethodCall<> const& call,
                         std::unique_ptr<flutter::MethodResult<>>& result) {
  auto const arguments{
      std::get<std::vector<flutter::EncodableValue>>(*call.arguments())};
  if (arguments.size() != 1 || !std::holds_alternative<int>(arguments[0])) {
    result->Error("INVALID_VALUE", "Value argument is not valid.");
  } else {
    auto const view_id{std::get<int>(arguments[0])};
    if (flutter::FlutterWindowController::instance().destroyWindow(view_id,
                                                                   true)) {
      result->Success();
    } else {
      result->Error("UNAVAILABLE", "Can't destroy window.");
    }
  }
}

}  // namespace

namespace flutter {

void FlutterWindowController::initializeChannel() {
  if (!channel_) {
    channel_ = std::make_unique<MethodChannel<>>(
        engine_->messenger(), kChannel, &StandardMethodCodec::GetInstance());
    channel_->SetMethodCallHandler(
        [this](MethodCall<> const& call,
               std::unique_ptr<MethodResult<>> result) {
          if (call.method_name() == "createRegularWindow") {
            handleCreateRegularWindow(call, result);
          } else if (call.method_name() == "createDialogWindow") {
            handleCreateDialogWindow(call, result);
          } else if (call.method_name() == "createSatelliteWindow") {
            handleCreateSatelliteWindow(call, result);
          } else if (call.method_name() == "createPopupWindow") {
            handleCreatePopupWindow(call, result);
          } else if (call.method_name() == "destroyWindow") {
            handleDestroyWindow(call, result);
          } else {
            result->NotImplemented();
          }
        });
  }
}

void FlutterWindowController::setEngine(std::shared_ptr<FlutterEngine> engine) {
  std::lock_guard<std::mutex> const lock(mutex_);
  engine_ = std::move(engine);
  initializeChannel();
}

auto FlutterWindowController::createRegularWindow(std::wstring const& title,
                                                  Win32Window::Size const& size)
    -> std::optional<FlutterWindowCreationResult> {
  std::unique_lock lock(mutex_);
  if (!engine_) {
    std::cerr << "Cannot create regular window without an engine.\n";
    return std::nullopt;
  }
  auto window{std::make_unique<FlutterWin32Window>(engine_)};

  lock.unlock();
  if (!window->Create(title, size, FlutterWindowArchetype::regular,
                      std::nullopt, std::nullopt)) {
    return std::nullopt;
  }
  lock.lock();

  // Assume first window is the main window
  if (windows_.empty()) {
    window->SetQuitOnClose(true);
  }

  auto const view_id{window->flutter_controller()->view_id()};
  windows_[view_id] = std::move(window);

  cleanupClosedWindows();
  sendOnWindowCreated(FlutterWindowArchetype::regular, view_id, std::nullopt);

  FlutterWindowCreationResult result{
      .view_id = view_id,
      .archetype = FlutterWindowArchetype::regular,
      .size = getWindowSize(view_id)};

  lock.unlock();

  sendOnWindowResized(view_id);

  return result;
}

auto FlutterWindowController::createDialogWindow(
    std::wstring const& title,
    Win32Window::Size const& size,
    std::optional<Win32Window::Point> origin,
    std::optional<FlutterViewId> parent_view_id)
    -> std::optional<FlutterWindowCreationResult> {
  std::unique_lock lock(mutex_);
  if (!engine_) {
    std::cerr << "Cannot create dialog without an engine.\n";
    return std::nullopt;
  }

  std::optional<HWND> const parent_hwnd{
      parent_view_id && windows_.find(*parent_view_id) != windows_.end()
          ? std::optional<HWND>{windows_[*parent_view_id].get()->GetHandle()}
          : std::nullopt};
  auto window{std::make_unique<FlutterWin32Window>(engine_)};

  lock.unlock();
  if (!window->Create(title, size, FlutterWindowArchetype::dialog, origin,
                      parent_hwnd)) {
    return std::nullopt;
  }
  lock.lock();

  auto const view_id{window->flutter_controller()->view_id()};
  windows_[view_id] = std::move(window);

  cleanupClosedWindows();
  sendOnWindowCreated(FlutterWindowArchetype::dialog, view_id, parent_view_id);

  FlutterWindowCreationResult result{
      .view_id = view_id,
      .parent_id = parent_view_id,
      .archetype = FlutterWindowArchetype::dialog,
      .size = getWindowSize(view_id)};

  lock.unlock();

  sendOnWindowResized(view_id);

  return result;
}

auto FlutterWindowController::createSatelliteWindow(
    std::wstring const& title,
    Win32Window::Point const& origin,
    Win32Window::Size const& size,
    FlutterViewId parent_view_id)
    -> std::optional<FlutterWindowCreationResult> {
  std::unique_lock lock(mutex_);
  if (!engine_) {
    std::cerr << "Cannot create satellite without an engine.\n";
    return std::nullopt;
  }

  auto* const parent_hwnd{windows_.find(parent_view_id) != windows_.end()
                              ? windows_[parent_view_id].get()->GetHandle()
                              : nullptr};
  if (!parent_hwnd) {
    std::cerr << "Invalid parent window (view ID " << parent_view_id << ").\n";
    return std::nullopt;
  }
  auto window{std::make_unique<FlutterWin32Window>(engine_)};

  lock.unlock();
  if (!window->Create(title, size, FlutterWindowArchetype::satellite, origin,
                      parent_hwnd)) {
    return std::nullopt;
  }
  lock.lock();

  auto const view_id{window->flutter_controller()->view_id()};
  windows_[view_id] = std::move(window);

  cleanupClosedWindows();
  sendOnWindowCreated(FlutterWindowArchetype::satellite, view_id,
                      parent_view_id);

  FlutterWindowCreationResult result{
      .view_id = view_id,
      .parent_id = parent_view_id,
      .archetype = FlutterWindowArchetype::satellite,
      .size = getWindowSize(view_id)};

  lock.unlock();

  sendOnWindowResized(view_id);

  return result;
}

auto FlutterWindowController::createPopupWindow(
    std::wstring const& title,
    Win32Window::Point const& origin,
    Win32Window::Size const& size,
    FlutterViewId parent_view_id)
    -> std::optional<FlutterWindowCreationResult> {
  std::unique_lock lock(mutex_);
  if (!engine_) {
    std::cerr << "Cannot create popup without an engine.\n";
    return std::nullopt;
  }

  auto* const parent_hwnd{windows_.find(parent_view_id) != windows_.end()
                              ? windows_[parent_view_id].get()->GetHandle()
                              : nullptr};
  if (!parent_hwnd) {
    std::cerr << "Invalid parent window (view ID " << parent_view_id << ").\n";
    return std::nullopt;
  }
  auto window{std::make_unique<FlutterWin32Window>(engine_)};

  lock.unlock();
  if (!window->Create(title, size, FlutterWindowArchetype::popup, origin,
                      parent_hwnd)) {
    return std::nullopt;
  }
  lock.lock();

  auto const view_id{window->flutter_controller()->view_id()};
  windows_[view_id] = std::move(window);

  cleanupClosedWindows();
  sendOnWindowCreated(FlutterWindowArchetype::popup, view_id, parent_view_id);

  FlutterWindowCreationResult result{.view_id = view_id,
                                     .parent_id = parent_view_id,
                                     .archetype = FlutterWindowArchetype::popup,
                                     .size = getWindowSize(view_id)};

  lock.unlock();

  sendOnWindowResized(view_id);

  return result;
}

auto FlutterWindowController::destroyWindow(FlutterViewId view_id,
                                            bool destroy_native_window)
    -> bool {
  std::unique_lock lock(mutex_);
  if (windows_.find(view_id) != windows_.end()) {
    if (windows_[view_id]->GetQuitOnClose()) {
      for (auto& [id, window] : windows_) {
        if (id != view_id && window->flutter_controller()) {
          lock.unlock();
          window->Destroy();
          lock.lock();
        }
      }
    }
    if (destroy_native_window) {
      auto const& window{windows_[view_id]};
      lock.unlock();
      window->Destroy();
      lock.lock();
    }
    sendOnWindowDestroyed(view_id);
    return true;
  }
  return false;
}

void FlutterWindowController::cleanupClosedWindows() {
  auto const first_window_with_null_controller{[&] {
    return std::find_if(windows_.begin(), windows_.end(),
                        [](auto const& window) {
                          return !window.second->flutter_controller();
                        });
  }};

  auto it{first_window_with_null_controller()};
  while (it != windows_.end()) {
    windows_.erase(it);
    it = first_window_with_null_controller();
  }
}

auto FlutterWindowController::windows() const -> ViewWindowMap const& {
  std::lock_guard const lock(mutex_);
  return windows_;
}

auto FlutterWindowController::channel() const
    -> std::unique_ptr<MethodChannel<>> const& {
  std::lock_guard const lock(mutex_);
  return channel_;
};

void FlutterWindowController::sendOnWindowCreated(
    FlutterWindowArchetype archetype,
    FlutterViewId view_id,
    std::optional<FlutterViewId> parent_view_id) const {
  if (channel_) {
    channel_->InvokeMethod(
        "onWindowCreated",
        std::make_unique<EncodableValue>(
            EncodableMap{{EncodableValue("viewId"), EncodableValue(view_id)},
                         {EncodableValue("parentViewId"),
                          parent_view_id ? EncodableValue(*parent_view_id)
                                         : EncodableValue()},
                         {EncodableValue("archetype"),
                          EncodableValue(static_cast<int>(archetype))}}));
  }
}

void FlutterWindowController::sendOnWindowDestroyed(
    FlutterViewId view_id) const {
  if (channel_) {
    channel_->InvokeMethod(
        "onWindowDestroyed",
        std::make_unique<EncodableValue>(EncodableMap{
            {EncodableValue("viewId"), EncodableValue(view_id)},
        }));
  }
}

void FlutterWindowController::sendOnWindowResized(FlutterViewId view_id) const {
  std::lock_guard const lock(mutex_);
  if (channel_) {
    auto size = getWindowSize(view_id);
    channel_->InvokeMethod(
        "onWindowResized",
        std::make_unique<EncodableValue>(EncodableMap{
            {EncodableValue("viewId"), EncodableValue(view_id)},
            {EncodableValue("width"), EncodableValue(size.width)},
            {EncodableValue("height"), EncodableValue(size.height)}}));
  }
}

FlutterWindowSize FlutterWindowController::getWindowSize(
    flutter::FlutterViewId view_id) const {
  auto* const hwnd{windows_.at(view_id)->GetHandle()};
  RECT frame;
  if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frame,
                                   sizeof(frame)))) {
    GetWindowRect(hwnd, &frame);
  }

  // Convert to logical coordinates
  auto const dpr{FlutterDesktopGetDpiForHWND(hwnd) / kBaseDpi};
  frame.left = static_cast<LONG>(frame.left / dpr);
  frame.top = static_cast<LONG>(frame.top / dpr);
  frame.right = static_cast<LONG>(frame.right / dpr);
  frame.bottom = static_cast<LONG>(frame.bottom / dpr);

  auto const width{frame.right - frame.left};
  auto const height{frame.bottom - frame.top};
  return {static_cast<int>(width), static_cast<int>(height)};
}

}  // namespace flutter
