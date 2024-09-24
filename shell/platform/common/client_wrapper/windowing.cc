// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <iostream>

#include "include/flutter/windowing.h"

namespace flutter {

namespace internal {

auto PlaceWindow(FlutterWindowPositioner const& positioner,
                 FlutterWindowSize child_size,
                 FlutterWindowRectangle const& anchor_rect,
                 FlutterWindowRectangle const& parent_rect,
                 FlutterWindowRectangle const& output_rect)
    -> FlutterWindowRectangle {
  FlutterWindowRectangle default_result;

  auto const offset_for{
      [](FlutterWindowSize const& size,
         FlutterWindowPositioner::Anchor anchor) -> FlutterWindowPoint {
        switch (anchor) {
          case FlutterWindowPositioner::Anchor::top_left:
            return {0, 0};
          case FlutterWindowPositioner::Anchor::top:
            return {-size.width / 2, 0};
          case FlutterWindowPositioner::Anchor::top_right:
            return {-1 * size.width, 0};
          case FlutterWindowPositioner::Anchor::left:
            return {0, -size.height / 2};
          case FlutterWindowPositioner::Anchor::center:
            return {-size.width / 2, -size.height / 2};
          case FlutterWindowPositioner::Anchor::right:
            return {-1 * size.width, -size.height / 2};
          case FlutterWindowPositioner::Anchor::bottom_left:
            return {0, -1 * size.height};
          case FlutterWindowPositioner::Anchor::bottom:
            return {-size.width / 2, -1 * size.height};
          case FlutterWindowPositioner::Anchor::bottom_right:
            return {-1 * size.width, -1 * size.height};
          default:
            std::cerr << "Unknown anchor value: " << static_cast<int>(anchor)
                      << '\n';
            std::abort();
        }
      }};

  auto const anchor_position_for{
      [](FlutterWindowRectangle const& rect,
         FlutterWindowPositioner::Anchor anchor) -> FlutterWindowPoint {
        switch (anchor) {
          case FlutterWindowPositioner::Anchor::top_left:
            return rect.top_left;
          case FlutterWindowPositioner::Anchor::top:
            return rect.top_left + FlutterWindowPoint{rect.size.width / 2, 0};
          case FlutterWindowPositioner::Anchor::top_right:
            return rect.top_left + FlutterWindowPoint{rect.size.width, 0};
          case FlutterWindowPositioner::Anchor::left:
            return rect.top_left + FlutterWindowPoint{0, rect.size.height / 2};
          case FlutterWindowPositioner::Anchor::center:
            return rect.top_left + FlutterWindowPoint{rect.size.width / 2,
                                                      rect.size.height / 2};
          case FlutterWindowPositioner::Anchor::right:
            return rect.top_left +
                   FlutterWindowPoint{rect.size.width, rect.size.height / 2};
          case FlutterWindowPositioner::Anchor::bottom_left:
            return rect.top_left + FlutterWindowPoint{0, rect.size.height};
          case FlutterWindowPositioner::Anchor::bottom:
            return rect.top_left +
                   FlutterWindowPoint{rect.size.width / 2, rect.size.height};
          case FlutterWindowPositioner::Anchor::bottom_right:
            return rect.top_left +
                   FlutterWindowPoint{rect.size.width, rect.size.height};
          default:
            std::cerr << "Unknown anchor value: " << static_cast<int>(anchor)
                      << '\n';
            std::abort();
        }
      }};

  auto const constrain_to{
      [](FlutterWindowRectangle const& r,
         FlutterWindowPoint const& p) -> FlutterWindowPoint {
        return {std::clamp(p.x, r.top_left.x, r.top_left.x + r.size.width),
                std::clamp(p.y, r.top_left.y, r.top_left.y + r.size.height)};
      }};

  auto const flip_anchor_x{[](FlutterWindowPositioner::Anchor anchor)
                               -> FlutterWindowPositioner::Anchor {
    switch (anchor) {
      case FlutterWindowPositioner::Anchor::top_left:
        return FlutterWindowPositioner::Anchor::top_right;
      case FlutterWindowPositioner::Anchor::top_right:
        return FlutterWindowPositioner::Anchor::top_left;
      case FlutterWindowPositioner::Anchor::left:
        return FlutterWindowPositioner::Anchor::right;
      case FlutterWindowPositioner::Anchor::right:
        return FlutterWindowPositioner::Anchor::left;
      case FlutterWindowPositioner::Anchor::bottom_left:
        return FlutterWindowPositioner::Anchor::bottom_right;
      case FlutterWindowPositioner::Anchor::bottom_right:
        return FlutterWindowPositioner::Anchor::bottom_left;
      default:
        return anchor;
    }
  }};

  auto const flip_anchor_y{[](FlutterWindowPositioner::Anchor anchor)
                               -> FlutterWindowPositioner::Anchor {
    switch (anchor) {
      case FlutterWindowPositioner::Anchor::top_left:
        return FlutterWindowPositioner::Anchor::bottom_left;
      case FlutterWindowPositioner::Anchor::top:
        return FlutterWindowPositioner::Anchor::bottom;
      case FlutterWindowPositioner::Anchor::top_right:
        return FlutterWindowPositioner::Anchor::bottom_right;
      case FlutterWindowPositioner::Anchor::bottom_left:
        return FlutterWindowPositioner::Anchor::top_left;
      case FlutterWindowPositioner::Anchor::bottom:
        return FlutterWindowPositioner::Anchor::top;
      case FlutterWindowPositioner::Anchor::bottom_right:
        return FlutterWindowPositioner::Anchor::top_right;
      default:
        return anchor;
    }
  }};

  auto const flip_offset_x{
      [](FlutterWindowPoint const& p) -> FlutterWindowPoint {
        return {-1 * p.x, p.y};
      }};

  auto const flip_offset_y{
      [](FlutterWindowPoint const& p) -> FlutterWindowPoint {
        return {p.x, -1 * p.y};
      }};

  {
    auto const result{
        constrain_to(parent_rect, anchor_position_for(
                                      anchor_rect, positioner.parent_anchor) +
                                      positioner.offset) +
        offset_for(child_size, positioner.child_anchor)};

    if (output_rect.contains({result, child_size})) {
      return FlutterWindowRectangle{result, child_size};
    }

    default_result = FlutterWindowRectangle{result, child_size};
  }

  if (positioner.constraint_adjustment &
      FlutterWindowPositioner::ConstraintAdjustment::flip_x) {
    auto const result{
        constrain_to(parent_rect,
                     anchor_position_for(
                         anchor_rect, flip_anchor_x(positioner.parent_anchor)) +
                         flip_offset_x(positioner.offset)) +
        offset_for(child_size, flip_anchor_x(positioner.child_anchor))};

    if (output_rect.contains({result, child_size})) {
      return FlutterWindowRectangle{result, child_size};
    }
  }

  if (positioner.constraint_adjustment &
      FlutterWindowPositioner::ConstraintAdjustment::flip_y) {
    auto const result{
        constrain_to(parent_rect,
                     anchor_position_for(
                         anchor_rect, flip_anchor_y(positioner.parent_anchor)) +
                         flip_offset_y(positioner.offset)) +
        offset_for(child_size, flip_anchor_y(positioner.child_anchor))};

    if (output_rect.contains({result, child_size})) {
      return FlutterWindowRectangle{result, child_size};
    }
  }

  if (positioner.constraint_adjustment &
          FlutterWindowPositioner::ConstraintAdjustment::flip_x &&
      positioner.constraint_adjustment &
          FlutterWindowPositioner::ConstraintAdjustment::flip_y) {
    auto const result{
        constrain_to(
            parent_rect,
            anchor_position_for(anchor_rect, flip_anchor_x(flip_anchor_y(
                                                 positioner.parent_anchor))) +
                flip_offset_x(flip_offset_y(positioner.offset))) +
        offset_for(child_size,
                   flip_anchor_x(flip_anchor_y(positioner.child_anchor)))};

    if (output_rect.contains({result, child_size})) {
      return FlutterWindowRectangle{result, child_size};
    }
  }

  {
    auto result{constrain_to(
                    parent_rect,
                    anchor_position_for(anchor_rect, positioner.parent_anchor) +
                        positioner.offset) +
                offset_for(child_size, positioner.child_anchor)};

    if (positioner.constraint_adjustment &
        FlutterWindowPositioner::ConstraintAdjustment::slide_x) {
      auto const left_overhang{result.x - output_rect.top_left.x};
      auto const right_overhang{
          (result.x + child_size.width) -
          (output_rect.top_left.x + output_rect.size.width)};

      if (left_overhang < 0) {
        result.x -= left_overhang;
      } else if (right_overhang > 0) {
        result.x -= right_overhang;
      }
    }

    if (positioner.constraint_adjustment &
        FlutterWindowPositioner::ConstraintAdjustment::slide_y) {
      auto const top_overhang{result.y - output_rect.top_left.y};
      auto const bot_overhang{
          (result.y + child_size.height) -
          (output_rect.top_left.y + output_rect.size.height)};

      if (top_overhang < 0) {
        result.y -= top_overhang;
      } else if (bot_overhang > 0) {
        result.y -= bot_overhang;
      }
    }

    if (output_rect.contains({result, child_size})) {
      return FlutterWindowRectangle{result, child_size};
    }
  }

  {
    auto result{constrain_to(
                    parent_rect,
                    anchor_position_for(anchor_rect, positioner.parent_anchor) +
                        positioner.offset) +
                offset_for(child_size, positioner.child_anchor)};

    if (positioner.constraint_adjustment &
        FlutterWindowPositioner::ConstraintAdjustment::resize_x) {
      auto const left_overhang{result.x - output_rect.top_left.x};
      auto const right_overhang{
          (result.x + child_size.width) -
          (output_rect.top_left.x + output_rect.size.width)};

      if (left_overhang < 0) {
        result.x -= left_overhang;
        child_size.width += left_overhang;
      }

      if (right_overhang > 0) {
        child_size.width -= right_overhang;
      }
    }

    if (positioner.constraint_adjustment &
        FlutterWindowPositioner::ConstraintAdjustment::resize_y) {
      auto const top_overhang{result.y - output_rect.top_left.y};
      auto const bot_overhang{
          (result.y + child_size.height) -
          (output_rect.top_left.y + output_rect.size.height)};

      if (top_overhang < 0) {
        result.y -= top_overhang;
        child_size.height += top_overhang;
      }

      if (bot_overhang > 0) {
        child_size.height -= bot_overhang;
      }
    }

    if (output_rect.contains({result, child_size})) {
      return FlutterWindowRectangle{result, child_size};
    }
  }

  return default_result;
}

}  // namespace internal

}  // namespace flutter
