#include "include/flutter/flutter_window_controller.h"

#include "include/flutter/encodable_value.h"
#include "include/flutter/standard_method_codec.h"

#include <dwmapi.h>

namespace {
auto const* const kChannel{"flutter/windowing"};

// Helper function to build a flutter::FlutterWindowPositioner
// from a flutter::EncodableMap containing positioner settings.
std::optional<flutter::FlutterWindowPositioner> buildPositioner(
    flutter::EncodableMap const* map,
    std::unique_ptr<flutter::MethodResult<>>& result) {
  auto const anchor_rect_it{map->find(flutter::EncodableValue("anchorRect"))};
  auto const positioner_parent_anchor_it{
      map->find(flutter::EncodableValue("positionerParentAnchor"))};
  auto const positioner_child_anchor_it{
      map->find(flutter::EncodableValue("positionerChildAnchor"))};
  auto const positioner_offset_it{
      map->find(flutter::EncodableValue("positionerOffset"))};
  auto const positioner_constraint_adjustment_it{
      map->find(flutter::EncodableValue("positionerConstraintAdjustment"))};

  if (anchor_rect_it == map->end() ||
      positioner_parent_anchor_it == map->end() ||
      positioner_child_anchor_it == map->end() ||
      positioner_offset_it == map->end() ||
      positioner_constraint_adjustment_it == map->end()) {
    result->Error("INVALID_VALUE",
                  "Map does not contain required keys: {'anchorRect', "
                  "'positionerParentAnchor', 'positionerChildAnchor', "
                  "'positionerOffset', 'positionerConstraintAdjustment'}.");
    return std::nullopt;
  }

  std::optional<flutter::FlutterWindowPositioner::Rect> anchor_rect;
  if (auto const* const anchor_rect_list{
          std::get_if<std::vector<flutter::EncodableValue>>(
              &anchor_rect_it->second)}) {
    if (anchor_rect_list->size() != 4) {
      result->Error("INVALID_VALUE",
                    "Values for 'anchorRect' must be an array of 4 integers.");
      return std::nullopt;
    } else if (!std::holds_alternative<int>(anchor_rect_list->at(0)) ||
               !std::holds_alternative<int>(anchor_rect_list->at(1)) ||
               !std::holds_alternative<int>(anchor_rect_list->at(2)) ||
               !std::holds_alternative<int>(anchor_rect_list->at(3))) {
      result->Error("INVALID_VALUE",
                    "Values for 'anchorRect' must be of type int.");
      return std::nullopt;
    }
    anchor_rect = flutter::FlutterWindowPositioner::Rect{
        .x = std::get<int>(anchor_rect_list->at(0)),
        .y = std::get<int>(anchor_rect_list->at(1)),
        .width = std::get<int>(anchor_rect_list->at(2)),
        .height = std::get<int>(anchor_rect_list->at(3))};
  }

  auto const* const positioner_parent_anchor{
      std::get_if<int>(&positioner_parent_anchor_it->second)};
  if (!positioner_parent_anchor) {
    result->Error("INVALID_VALUE",
                  "Value for 'positionerParentAnchor' must be of type int.");
    return std::nullopt;
  }

  auto const* const positioner_child_anchor{
      std::get_if<int>(&positioner_child_anchor_it->second)};
  if (!positioner_child_anchor) {
    result->Error("INVALID_VALUE",
                  "Value for 'positionerChildAnchor' must be of type int.");
    return std::nullopt;
  }

  // Convert from anchor (originally a WindowPositionerAnchor) to
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

  auto const* const positioner_offset_list{
      std::get_if<std::vector<flutter::EncodableValue>>(
          &positioner_offset_it->second)};
  if (positioner_offset_list->size() != 2 ||
      !std::holds_alternative<int>(positioner_offset_list->at(0)) ||
      !std::holds_alternative<int>(positioner_offset_list->at(1))) {
    result->Error("INVALID_VALUE",
                  "Values for 'positionerOffset' must be of type int.");
    return std::nullopt;
  }
  auto const dx{std::get<int>(positioner_offset_list->at(0))};
  auto const dy{std::get<int>(positioner_offset_list->at(1))};

  auto const* const positioner_constraint_adjustment{
      std::get_if<int>(&positioner_constraint_adjustment_it->second)};
  if (!positioner_constraint_adjustment) {
    result->Error(
        "INVALID_VALUE",
        "Value for 'positionerConstraintAdjustment' must be of type int.");
    return std::nullopt;
  }

  return flutter::FlutterWindowPositioner{
      .anchor_rect = anchor_rect,
      .anchor = static_cast<flutter::FlutterWindowPositioner::Anchor>(
          *positioner_parent_anchor),
      .gravity = gravity,
      .offset = {.dx = dx, .dy = dy},
      .constraint_adjustment =
          static_cast<uint32_t>(*positioner_constraint_adjustment)};
}

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
      result->Error("INVALID_VALUE",
                    "Map does not contain required keys: {'width', 'height'}.");
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

      if (auto const data{
              flutter::FlutterWindowController::instance().createDialogWindow(
                  L"dialog", size,
                  *parent >= 0 ? std::optional<flutter::FlutterViewId>{*parent}
                               : std::nullopt)}) {
        result->Success(encodeWindowCreationResult(data.value()));
      } else {
        result->Error("UNAVAILABLE", "Can't create window.");
      }
    } else {
      result->Error("INVALID_VALUE",
                    "Map does not contain required keys: {'parent', 'size'}.");
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

      if (auto const positioner{buildPositioner(map, result)}) {
        if (auto const data{flutter::FlutterWindowController::instance()
                                .createSatelliteWindow(L"satellite", size,
                                                       positioner.value(),
                                                       *parent)}) {
          result->Success(encodeWindowCreationResult(data.value()));
        } else {
          result->Error("UNAVAILABLE", "Can't create window.");
        }
      }
    } else {
      result->Error("INVALID_VALUE",
                    "Map does not contain required keys: {'parent', 'size'}.");
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

      if (auto const positioner{buildPositioner(map, result)}) {
        if (auto const data{
                flutter::FlutterWindowController::instance().createPopupWindow(
                    L"popup", size, positioner.value(), *parent)}) {
          result->Success(encodeWindowCreationResult(data.value()));
        } else {
          result->Error("UNAVAILABLE", "Can't create window.");
        }
      }
    } else {
      result->Error("INVALID_VALUE",
                    "Map does not contain required keys: {'parent', 'size'}.");
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
  if (!window->Create(title, size, FlutterWindowArchetype::dialog, parent_hwnd,
                      std::nullopt)) {
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
    Win32Window::Size const& size,
    FlutterWindowPositioner const& positioner,
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
  if (!window->Create(title, size, FlutterWindowArchetype::satellite,
                      parent_hwnd, positioner)) {
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
    Win32Window::Size const& size,
    FlutterWindowPositioner const& positioner,
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
  if (!window->Create(title, size, FlutterWindowArchetype::popup, parent_hwnd,
                      positioner)) {
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
  auto const dpr{FlutterDesktopGetDpiForHWND(hwnd) /
                 static_cast<double>(USER_DEFAULT_SCREEN_DPI)};
  frame.left = static_cast<LONG>(frame.left / dpr);
  frame.top = static_cast<LONG>(frame.top / dpr);
  frame.right = static_cast<LONG>(frame.right / dpr);
  frame.bottom = static_cast<LONG>(frame.bottom / dpr);

  auto const width{frame.right - frame.left};
  auto const height{frame.bottom - frame.top};
  return {static_cast<int>(width), static_cast<int>(height)};
}

}  // namespace flutter
