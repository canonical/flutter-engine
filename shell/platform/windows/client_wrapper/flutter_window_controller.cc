#include "include/flutter/flutter_window_controller.h"

#include "include/flutter/encodable_value.h"
#include "include/flutter/standard_method_codec.h"

#include <dwmapi.h>

namespace {

auto const* const kChannel{"flutter/windowing"};
auto const* const kErrorCodeInvalidValue{"INVALID_VALUE"};
auto const* const kErrorCodeUnavailable{"UNAVAILABLE"};

// Controls whether satellites are hidden when their top-level window
// and all its children become inactive.
bool g_enable_satellite_hiding{true};

// Retrieves the value associated with |key| from |map|, ensuring it matches
// the expected type |T|. Returns the value if found and correctly typed,
// otherwise logs an error in |result| and returns std::nullopt.
template <typename T>
auto getSingleValueForKeyOrSendError(
    std::string const& key,
    flutter::EncodableMap const* map,
    std::unique_ptr<flutter::MethodResult<>>& result) -> std::optional<T> {
  if (auto const it{map->find(flutter::EncodableValue(key))};
      it != map->end()) {
    if (auto const* const value{std::get_if<T>(&it->second)}) {
      return *value;
    } else {
      result->Error(kErrorCodeInvalidValue, "Value for '" + key +
                                                "' key must be of type '" +
                                                typeid(T).name() + "'.");
    }
  } else {
    result->Error(kErrorCodeInvalidValue,
                  "Map does not contain required '" + key + "' key.");
  }
  return std::nullopt;
}

// Retrieves a list of values associated with |key| from |map|, ensuring the
// list has |Size| elements, all of type |T|. Returns the list if found and
// valid, otherwise logs an error in |result| and returns std::nullopt.
template <typename T, size_t Size>
auto getListValuesForKeyOrSendError(
    std::string const& key,
    flutter::EncodableMap const* map,
    std::unique_ptr<flutter::MethodResult<>>& result)
    -> std::optional<std::vector<T>> {
  if (auto const it{map->find(flutter::EncodableValue(key))};
      it != map->end()) {
    if (auto const* const array{
            std::get_if<std::vector<flutter::EncodableValue>>(&it->second)}) {
      if (array->size() != Size) {
        result->Error(kErrorCodeInvalidValue,
                      "Array for '" + key + "' key must have " +
                          std::to_string(Size) + " values.");
        return std::nullopt;
      }
      std::vector<T> decoded_values;
      for (auto const& value : *array) {
        if (std::holds_alternative<T>(value)) {
          decoded_values.push_back(std::get<T>(value));
        } else {
          result->Error(kErrorCodeInvalidValue,
                        "Array for '" + key +
                            "' key must only have values of type '" +
                            typeid(T).name() + "'.");
          return std::nullopt;
        }
      }
      return decoded_values;
    } else {
      result->Error(kErrorCodeInvalidValue,
                    "Value for '" + key + "' key must be an array.");
    }
  } else {
    result->Error(kErrorCodeInvalidValue,
                  "Map does not contain required '" + key + "' key.");
  }
  return std::nullopt;
}

// Converts a |flutter::FlutterWindowArchetype| to its corresponding wide
// string representation.
auto archetypeToWideString(flutter::FlutterWindowArchetype archetype)
    -> std::wstring {
  switch (archetype) {
    case flutter::FlutterWindowArchetype::regular:
      return L"regular";
    case flutter::FlutterWindowArchetype::floating_regular:
      return L"floating_regular";
    case flutter::FlutterWindowArchetype::dialog:
      return L"dialog";
    case flutter::FlutterWindowArchetype::satellite:
      return L"satellite";
    case flutter::FlutterWindowArchetype::popup:
      return L"popup";
    case flutter::FlutterWindowArchetype::tip:
      return L"tip";
  }
  std::cerr
      << "Unhandled window archetype encountered in archetypeToWideString: "
      << static_cast<int>(archetype) << "\n";
  std::abort();
}

void handleCreateWindow(flutter::FlutterWindowArchetype archetype,
                        flutter::MethodCall<> const& call,
                        std::unique_ptr<flutter::MethodResult<>>& result) {
  auto const* const arguments{call.arguments()};
  auto const* const map{std::get_if<flutter::EncodableMap>(arguments)};
  if (!map) {
    result->Error(kErrorCodeInvalidValue, "Method call argument is not a map.");
    return;
  }

  std::wstring const title{archetypeToWideString(archetype)};

  auto const size_list{
      getListValuesForKeyOrSendError<int, 2>("size", map, result)};
  if (!size_list) {
    return;
  }
  if (size_list->at(0) < 0 || size_list->at(1) < 0) {
    result->Error(kErrorCodeInvalidValue,
                  "Values for 'size' key (" + std::to_string(size_list->at(0)) +
                      ", " + std::to_string(size_list->at(1)) +
                      ") must be nonnegative.");
    return;
  }

  std::optional<flutter::FlutterWindowPositioner> positioner;
  std::optional<flutter::FlutterWindowRectangle> anchor_rect;

  if (archetype == flutter::FlutterWindowArchetype::satellite ||
      archetype == flutter::FlutterWindowArchetype::popup) {
    if (auto const anchor_rect_it{
            map->find(flutter::EncodableValue("anchorRect"))};
        anchor_rect_it != map->end()) {
      if (!anchor_rect_it->second.IsNull()) {
        auto const anchor_rect_list{
            getListValuesForKeyOrSendError<int, 4>("anchorRect", map, result)};
        if (!anchor_rect_list) {
          return;
        }
        anchor_rect = flutter::FlutterWindowRectangle{
            {.x = anchor_rect_list->at(0), .y = anchor_rect_list->at(1)},
            {.width = anchor_rect_list->at(2),
             .height = anchor_rect_list->at(3)}};
      }
    } else {
      result->Error(kErrorCodeInvalidValue,
                    "Map does not contain required 'anchorRect' key.");
      return;
    }

    auto const positioner_parent_anchor{getSingleValueForKeyOrSendError<int>(
        "positionerParentAnchor", map, result)};
    if (!positioner_parent_anchor) {
      return;
    }
    auto const positioner_child_anchor{getSingleValueForKeyOrSendError<int>(
        "positionerChildAnchor", map, result)};
    if (!positioner_child_anchor) {
      return;
    }
    auto const child_anchor{
        static_cast<flutter::FlutterWindowPositioner::Anchor>(
            positioner_child_anchor.value())};

    auto const positioner_offset_list{getListValuesForKeyOrSendError<int, 2>(
        "positionerOffset", map, result)};
    if (!positioner_offset_list) {
      return;
    }
    auto const positioner_constraint_adjustment{
        getSingleValueForKeyOrSendError<int>("positionerConstraintAdjustment",
                                             map, result)};
    if (!positioner_constraint_adjustment) {
      return;
    }
    positioner = flutter::FlutterWindowPositioner{
        .anchor_rect = anchor_rect,
        .parent_anchor = static_cast<flutter::FlutterWindowPositioner::Anchor>(
            positioner_parent_anchor.value()),
        .child_anchor = child_anchor,
        .offset = {.x = positioner_offset_list->at(0),
                   .y = positioner_offset_list->at(1)},
        .constraint_adjustment =
            static_cast<flutter::FlutterWindowPositioner::ConstraintAdjustment>(
                positioner_constraint_adjustment.value())};
  }

  std::optional<flutter::FlutterViewId> parent_view_id;
  if (archetype == flutter::FlutterWindowArchetype::dialog ||
      archetype == flutter::FlutterWindowArchetype::satellite ||
      archetype == flutter::FlutterWindowArchetype::popup) {
    if (auto const parent_it{map->find(flutter::EncodableValue("parent"))};
        parent_it != map->end()) {
      if (parent_it->second.IsNull()) {
        if (archetype != flutter::FlutterWindowArchetype::dialog) {
          result->Error(kErrorCodeInvalidValue,
                        "Value for 'parent' key must not be null.");
          return;
        }
      } else {
        if (auto const* const parent{std::get_if<int>(&parent_it->second)}) {
          parent_view_id = *parent >= 0
                               ? std::optional<flutter::FlutterViewId>(*parent)
                               : std::nullopt;
          if (!parent_view_id.has_value() &&
              (archetype == flutter::FlutterWindowArchetype::satellite ||
               archetype == flutter::FlutterWindowArchetype::popup)) {
            result->Error(kErrorCodeInvalidValue,
                          "Value for 'parent' key (" +
                              std::to_string(parent_view_id.value()) +
                              ") must be nonnegative.");
            return;
          }
        } else {
          result->Error(kErrorCodeInvalidValue,
                        "Value for 'parent' key must be of type int.");
          return;
        }
      }
    } else {
      result->Error(kErrorCodeInvalidValue,
                    "Map does not contain required 'parent' key.");
      return;
    }
  }

  if (auto const data_opt{
          flutter::FlutterWindowController::instance().createWindow(
              title, {.width = size_list->at(0), .height = size_list->at(1)},
              archetype, positioner, parent_view_id)}) {
    auto const& data{data_opt.value()};
    result->Success(flutter::EncodableValue(flutter::EncodableMap{
        {flutter::EncodableValue("viewId"),
         flutter::EncodableValue(data.view_id)},
        {flutter::EncodableValue("parentViewId"),
         data.parent_id ? flutter::EncodableValue(data.parent_id.value())
                        : flutter::EncodableValue()},
        {flutter::EncodableValue("archetype"),
         flutter::EncodableValue(static_cast<int>(data.archetype))},
        {flutter::EncodableValue("width"),
         flutter::EncodableValue(data.size.width)},
        {flutter::EncodableValue("height"),
         flutter::EncodableValue((data.size.height))}}));
  } else {
    result->Error(kErrorCodeUnavailable, "Can't create window.");
  }
}

void handleDestroyWindow(flutter::MethodCall<> const& call,
                         std::unique_ptr<flutter::MethodResult<>>& result) {
  auto const* const arguments{call.arguments()};
  auto const* const map{std::get_if<flutter::EncodableMap>(arguments)};
  if (!map) {
    result->Error(kErrorCodeInvalidValue, "Method call argument is not a map.");
    return;
  }

  auto const view_id{
      getSingleValueForKeyOrSendError<int>("viewId", map, result)};
  if (!view_id) {
    return;
  }
  if (view_id.value() < 0) {
    result->Error(kErrorCodeInvalidValue, "Value for 'viewId' (" +
                                              std::to_string(view_id.value()) +
                                              ") must be nonnegative.");
    return;
  }

  if (flutter::FlutterWindowController::instance().destroyWindow(
          view_id.value(), true)) {
    result->Success();
  } else {
    result->Error(kErrorCodeUnavailable, "Can't destroy window.");
  }
}

auto getParentOrOwner(HWND window) -> HWND {
  auto const parent{GetParent(window)};
  return parent ? parent : GetWindow(window, GW_OWNER);
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
            handleCreateWindow(FlutterWindowArchetype::regular, call, result);
          } else if (call.method_name() == "createDialogWindow") {
            handleCreateWindow(FlutterWindowArchetype::dialog, call, result);
          } else if (call.method_name() == "createSatelliteWindow") {
            handleCreateWindow(FlutterWindowArchetype::satellite, call, result);
          } else if (call.method_name() == "createPopupWindow") {
            handleCreateWindow(FlutterWindowArchetype::popup, call, result);
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

auto FlutterWindowController::createWindow(
    std::wstring const& title,
    FlutterWindowSize const& size,
    FlutterWindowArchetype archetype,
    std::optional<FlutterWindowPositioner> positioner,
    std::optional<FlutterViewId> parent_view_id)
    -> std::optional<FlutterWindowCreationResult> {
  std::unique_lock lock(mutex_);
  if (!engine_) {
    std::cerr << "Cannot create window without an engine.\n";
    return std::nullopt;
  }

  auto window{std::make_unique<FlutterWin32Window>(engine_)};

  std::optional<HWND> const parent_hwnd{
      parent_view_id.has_value() &&
              windows_.find(parent_view_id.value()) != windows_.end()
          ? std::optional<HWND>{windows_[parent_view_id.value()]->GetHandle()}
          : std::nullopt};

  lock.unlock();
  if (!window->Create(title, size, archetype, parent_hwnd, positioner)) {
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
  sendOnWindowCreated(archetype, view_id, parent_view_id);

  FlutterWindowCreationResult result{.view_id = view_id,
                                     .parent_id = parent_view_id,
                                     .archetype = archetype,
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
          DestroyWindow(window->GetHandle());
          lock.lock();
        }
      }
    }
    if (destroy_native_window) {
      auto const& window{windows_[view_id]};
      lock.unlock();
      if (window->archetype_ == FlutterWindowArchetype::dialog &&
          GetWindow(window->GetHandle(), GW_OWNER)) {
        // Temporarily disable satellite hiding. This prevents satellites from
        // flickering because of briefly hiding and showing between the
        // destruction of a modal dialog and the transfer of focus to the owner
        // window.
        g_enable_satellite_hiding = false;
      }
      DestroyWindow(window->GetHandle());
      g_enable_satellite_hiding = true;
      lock.lock();
    } else {
      sendOnWindowDestroyed(view_id);
    }
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
        std::make_unique<EncodableValue>(EncodableMap{
            {EncodableValue("viewId"), EncodableValue(view_id)},
            {EncodableValue("parentViewId"),
             parent_view_id ? EncodableValue(parent_view_id.value())
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

void FlutterWindowController::hideWindowsSatellites(HWND opt_out_window) {
  if (!g_enable_satellite_hiding) {
    return;
  }

  // Helper function to check whether |window| is a descendant of |ancestor|.
  auto const is_descendant_of{[](HWND window, HWND ancestor) -> bool {
    auto current{ancestor};
    while (current) {
      current = getParentOrOwner(current);
      if (current == window) {
        return true;
      }
    }
    return false;
  }};

  // Helper function to check whether |window| has a child dialog.
  auto const has_dialog{[](Win32Window* window) -> bool {
    for (auto* const child : window->children_) {
      if (child->archetype_ == FlutterWindowArchetype::dialog) {
        return true;
      }
    }
    return false;
  }};

  std::lock_guard const lock(mutex_);
  for (auto const& [_, window] : windows_) {
    if (window->window_handle_ == opt_out_window ||
        is_descendant_of(window->window_handle_, opt_out_window)) {
      continue;
    }

    for (auto* const child : window->children_) {
      if (child->archetype_ != FlutterWindowArchetype::satellite) {
        continue;
      }
      if (!has_dialog(child)) {
        ShowWindow(child->window_handle_, SW_HIDE);
      }
    }
  }
}

void FlutterWindowController::showWindowAndAncestorsSatellites(HWND window) {
  if (!g_enable_satellite_hiding) {
    return;
  }

  auto current{window};
  while (current) {
    for (auto* const child :
         Win32Window::GetThisFromHandle(current)->children_) {
      if (child->archetype_ == FlutterWindowArchetype::satellite) {
        ShowWindow(child->window_handle_, SW_SHOWNOACTIVATE);
      }
    }
    current = getParentOrOwner(current);
  }

  // Hide satellites of all other top-level windows
  if (Win32Window::GetThisFromHandle(window)->archetype_ !=
      FlutterWindowArchetype::satellite) {
    hideWindowsSatellites(window);
  }
}

}  // namespace flutter
