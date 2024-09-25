#include "include/flutter/flutter_window_controller.h"

#include "include/flutter/encodable_value.h"
#include "include/flutter/flutter_win32_window.h"
#include "include/flutter/standard_method_codec.h"

#include <iomanip>
#include <sstream>

#include <dwmapi.h>

namespace {

auto const* const kChannel{"flutter/windowing"};
auto const* const kWindowClassName{L"FLUTTER_WIN32_WINDOW"};
auto const* const kErrorCodeInvalidValue{"INVALID_VALUE"};
auto const* const kErrorCodeUnavailable{"UNAVAILABLE"};

// Tracks whether the method channel has been initialized by any window
// controller. Set to true after the first initialization to prevent
// reinitialization by other controllers.
bool g_is_method_channel_initialized{false};

// Controls whether satellites are hidden when their top-level window
// and all its children become inactive. If null, satellite hiding
// is enabled. If not null, it contains the handle of the window that
// disabled the hiding, and it will be reset when the window if fully
// destroyed.
HWND g_disable_satellite_hiding{nullptr};

// Retrieves the value associated with |key| from |map|, ensuring it matches
// the expected type |T|. Returns the value if found and correctly typed,
// otherwise logs an error in |result| and returns std::nullopt.
template <typename T>
auto GetSingleValueForKeyOrSendError(
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
auto GetListValuesForKeyOrSendError(
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
auto ArchetypeToWideString(flutter::WindowArchetype archetype) -> std::wstring {
  switch (archetype) {
    case flutter::WindowArchetype::regular:
      return L"regular";
    case flutter::WindowArchetype::floating_regular:
      return L"floating_regular";
    case flutter::WindowArchetype::dialog:
      return L"dialog";
    case flutter::WindowArchetype::satellite:
      return L"satellite";
    case flutter::WindowArchetype::popup:
      return L"popup";
    case flutter::WindowArchetype::tip:
      return L"tip";
  }
  std::cerr
      << "Unhandled window archetype encountered in archetypeToWideString: "
      << static_cast<int>(archetype) << "\n";
  std::abort();
}

auto GetParentOrOwner(HWND window) -> HWND {
  auto const parent{GetParent(window)};
  return parent ? parent : GetWindow(window, GW_OWNER);
}

auto IsClassRegistered(LPCWSTR class_name) -> bool {
  WNDCLASSEX window_class{};
  return GetClassInfoEx(GetModuleHandle(nullptr), class_name, &window_class) !=
         0;
}

}  // namespace

namespace flutter {

FlutterWindowController::FlutterWindowController(
    std::shared_ptr<FlutterEngine> engine) {
  engine_ = std::move(engine);
  InitializeChannel(engine_->messenger());
}

FlutterWindowController::~FlutterWindowController() {
  if (IsClassRegistered(kWindowClassName)) {
    UnregisterClass(kWindowClassName, GetModuleHandle(nullptr));
  }
}

auto FlutterWindowController::CreateFlutterWindow(
    std::wstring const& title,
    FlutterWindowSize const& size,
    WindowArchetype archetype,
    std::optional<WindowPositioner> positioner,
    std::optional<FlutterViewId> parent_view_id)
    -> std::optional<WindowCreationResult> {
  std::unique_lock lock(mutex_);
  if (!engine_) {
    std::cerr << "Cannot create window without an engine.\n";
    return std::nullopt;
  }

  auto window{std::make_unique<FlutterWin32Window>(engine_, this)};

  if (!IsClassRegistered(kWindowClassName)) {
    auto const idi_app_icon{101};
    WNDCLASSEX window_class{};
    window_class.cbSize = sizeof(WNDCLASSEX);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = Win32Window::WndProc;
    window_class.cbClsExtra = 0;
    window_class.cbWndExtra = 0;
    window_class.hInstance = GetModuleHandle(nullptr);
    window_class.hIcon =
        LoadIcon(window_class.hInstance, MAKEINTRESOURCE(idi_app_icon));
    window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    window_class.hbrBackground = 0;
    window_class.lpszMenuName = nullptr;
    window_class.lpszClassName = kWindowClassName;
    window_class.hIconSm = nullptr;

    RegisterClassEx(&window_class);
  }

  std::optional<HWND> const parent_hwnd{
      parent_view_id.has_value() &&
              windows_.find(parent_view_id.value()) != windows_.end()
          ? std::optional<HWND>{windows_[parent_view_id.value()]->GetHandle()}
          : std::nullopt};

  lock.unlock();

  if (!window->Create(kWindowClassName, title, size, archetype, parent_hwnd,
                      positioner)) {
    return std::nullopt;
  }

  lock.lock();

  // Assume first window is the main window
  if (windows_.empty()) {
    window->SetQuitOnClose(true);
  }

  auto const view_id{window->GetFlutterViewId()};
  windows_[view_id] = std::move(window);

  SendOnWindowCreated(archetype, view_id, parent_view_id);
  SendOnWindowResized(view_id);

  WindowCreationResult result{.view_id = view_id,
                              .parent_id = parent_view_id,
                              .archetype = archetype,
                              .size = GetWindowSize(view_id)};

  return result;
}

auto FlutterWindowController::DestroyFlutterWindow(FlutterViewId view_id)
    -> bool {
  std::unique_lock lock(mutex_);
  auto it{windows_.find(view_id)};
  if (it != windows_.end()) {
    lock.unlock();

    auto* const window{it->second.get()};
    auto const window_handle{window->GetHandle()};

    if (window->archetype_ == WindowArchetype::dialog &&
        GetWindow(window_handle, GW_OWNER)) {
      // Temporarily disable satellite hiding. This prevents satellites from
      // flickering because of briefly hiding and showing between the
      // destruction of a modal dialog and the transfer of focus to the owner
      // window.
      g_disable_satellite_hiding = window_handle;
    }

    DestroyWindow(window->GetHandle());
    return true;
  }
  return false;
}

auto FlutterWindowController::MessageHandler(HWND hwnd,
                                             UINT message,
                                             WPARAM wparam,
                                             LPARAM lparam) -> LRESULT {
  auto* const window{Win32Window::GetThisFromHandle(hwnd)};

  // Handle any controller-specific logic here
  switch (message) {
    case WM_NCDESTROY: {
      std::unique_lock lock{mutex_};
      auto const it{std::find_if(windows_.begin(), windows_.end(),
                                 [hwnd](auto const& window) {
                                   return window.second->GetHandle() == hwnd;
                                 })};
      if (it != windows_.end()) {
        auto const view_id{it->first};
        auto const quit_on_close{it->second.get()->GetQuitOnClose()};

        windows_.erase(it);

        if (quit_on_close) {
          auto it2{windows_.begin()};
          while (it2 != windows_.end()) {
            auto const& that{it2->second};
            lock.unlock();
            DestroyWindow(that->GetHandle());
            lock.lock();
            it2 = windows_.begin();
          }
        }

        SendOnWindowDestroyed(view_id);

        if (g_disable_satellite_hiding == hwnd) {
          // Re-enable satellite hiding by clearing the window handle now that
          // the window is fully destroyed
          g_disable_satellite_hiding = nullptr;
        }
      } else {
        std::cerr << "Cannot find Win32Window for window handle 0x" << std::hex
                  << std::setfill('0') << std::setw(8) << hwnd << '\n';
        return -1;
      }
    }
      return 0;
    case WM_ACTIVATE:
      if (wparam != WA_INACTIVE) {
        if (window->archetype_ != WindowArchetype::popup) {
          // If a non-popup window is activated, close popups for all windows
          decltype(windows_) windows;
          {
            std::lock_guard const lock(mutex_);
            windows = windows_;
          }
          for (auto const& [_, target_window] : windows) {
            target_window->CloseChildPopups();
          }
        } else {
          // If a popup window is activated, close its child popups
          window->CloseChildPopups();
        }
        ShowWindowAndAncestorsSatellites(hwnd);
      }
      break;
    case WM_ACTIVATEAPP:
      if (wparam == FALSE) {
        // Close child popups and hide satellites from all windows if a window
        // belonging to a different application is being activated
        window->CloseChildPopups();
        HideWindowsSatellites(nullptr);
      }
      break;
    case WM_SIZE: {
      std::lock_guard lock{mutex_};
      auto const it{std::find_if(windows_.begin(), windows_.end(),
                                 [hwnd](auto const& window) {
                                   return window.second->GetHandle() == hwnd;
                                 })};
      if (it != windows_.end()) {
        auto const view_id{it->first};
        SendOnWindowResized(view_id);
      } else {
        std::cerr << "Cannot find Win32Window for window handle 0x" << std::hex
                  << std::setfill('0') << std::setw(8) << hwnd << '\n';
        return -1;
      }
    } break;
    default:
      break;
  }

  return window->MessageHandler(hwnd, message, wparam, lparam);
}

void FlutterWindowController::InitializeChannel(BinaryMessenger* messenger) {
  if (channel_) {
    return;
  }

  if (g_is_method_channel_initialized) {
    std::cerr << "Method channel " << kChannel
              << " is already initialized by another window controller.\n";
    std::abort();
  }
  g_is_method_channel_initialized = true;

  channel_ = std::make_unique<MethodChannel<>>(
      messenger, kChannel, &StandardMethodCodec::GetInstance());
  channel_->SetMethodCallHandler(
      [this](MethodCall<> const& call, std::unique_ptr<MethodResult<>> result) {
        if (call.method_name() == "createRegularWindow") {
          HandleCreateWindow(WindowArchetype::regular, call, result);
        } else if (call.method_name() == "createDialogWindow") {
          HandleCreateWindow(WindowArchetype::dialog, call, result);
        } else if (call.method_name() == "createSatelliteWindow") {
          HandleCreateWindow(WindowArchetype::satellite, call, result);
        } else if (call.method_name() == "createPopupWindow") {
          HandleCreateWindow(WindowArchetype::popup, call, result);
        } else if (call.method_name() == "destroyWindow") {
          HandleDestroyWindow(call, result);
        } else {
          result->NotImplemented();
        }
      });
}

void FlutterWindowController::HandleCreateWindow(
    WindowArchetype archetype,
    MethodCall<> const& call,
    std::unique_ptr<MethodResult<>>& result) {
  auto const* const arguments{call.arguments()};
  auto const* const map{std::get_if<EncodableMap>(arguments)};
  if (!map) {
    result->Error(kErrorCodeInvalidValue, "Method call argument is not a map.");
    return;
  }

  std::wstring const title{ArchetypeToWideString(archetype)};

  auto const size_list{
      GetListValuesForKeyOrSendError<int, 2>("size", map, result)};
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

  std::optional<WindowPositioner> positioner;
  std::optional<FlutterWindowRectangle> anchor_rect;

  if (archetype == WindowArchetype::satellite ||
      archetype == WindowArchetype::popup) {
    if (auto const anchor_rect_it{map->find(EncodableValue("anchorRect"))};
        anchor_rect_it != map->end()) {
      if (!anchor_rect_it->second.IsNull()) {
        auto const anchor_rect_list{
            GetListValuesForKeyOrSendError<int, 4>("anchorRect", map, result)};
        if (!anchor_rect_list) {
          return;
        }
        anchor_rect = FlutterWindowRectangle{
            {anchor_rect_list->at(0), anchor_rect_list->at(1)},
            {anchor_rect_list->at(2), anchor_rect_list->at(3)}};
      }
    } else {
      result->Error(kErrorCodeInvalidValue,
                    "Map does not contain required 'anchorRect' key.");
      return;
    }

    auto const positioner_parent_anchor{GetSingleValueForKeyOrSendError<int>(
        "positionerParentAnchor", map, result)};
    if (!positioner_parent_anchor) {
      return;
    }
    auto const positioner_child_anchor{GetSingleValueForKeyOrSendError<int>(
        "positionerChildAnchor", map, result)};
    if (!positioner_child_anchor) {
      return;
    }
    auto const child_anchor{
        static_cast<WindowPositioner::Anchor>(positioner_child_anchor.value())};

    auto const positioner_offset_list{GetListValuesForKeyOrSendError<int, 2>(
        "positionerOffset", map, result)};
    if (!positioner_offset_list) {
      return;
    }
    auto const positioner_constraint_adjustment{
        GetSingleValueForKeyOrSendError<int>("positionerConstraintAdjustment",
                                             map, result)};
    if (!positioner_constraint_adjustment) {
      return;
    }
    positioner = WindowPositioner{
        .anchor_rect = anchor_rect,
        .parent_anchor = static_cast<WindowPositioner::Anchor>(
            positioner_parent_anchor.value()),
        .child_anchor = child_anchor,
        .offset = {positioner_offset_list->at(0),
                   positioner_offset_list->at(1)},
        .constraint_adjustment =
            static_cast<WindowPositioner::ConstraintAdjustment>(
                positioner_constraint_adjustment.value())};
  }

  std::optional<FlutterViewId> parent_view_id;
  if (archetype == WindowArchetype::dialog ||
      archetype == WindowArchetype::satellite ||
      archetype == WindowArchetype::popup) {
    if (auto const parent_it{map->find(EncodableValue("parent"))};
        parent_it != map->end()) {
      if (parent_it->second.IsNull()) {
        if (archetype != WindowArchetype::dialog) {
          result->Error(kErrorCodeInvalidValue,
                        "Value for 'parent' key must not be null.");
          return;
        }
      } else {
        if (auto const* const parent{std::get_if<int>(&parent_it->second)}) {
          parent_view_id = *parent >= 0 ? std::optional<FlutterViewId>(*parent)
                                        : std::nullopt;
          if (!parent_view_id.has_value() &&
              (archetype == WindowArchetype::satellite ||
               archetype == WindowArchetype::popup)) {
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

  if (auto const data_opt{CreateFlutterWindow(
          title, {.width = size_list->at(0), .height = size_list->at(1)},
          archetype, positioner, parent_view_id)}) {
    auto const& data{data_opt.value()};
    result->Success(EncodableValue(EncodableMap{
        {EncodableValue("viewId"), EncodableValue(data.view_id)},
        {EncodableValue("parentViewId"),
         data.parent_id ? EncodableValue(data.parent_id.value())
                        : EncodableValue()},
        {EncodableValue("archetype"),
         EncodableValue(static_cast<int>(data.archetype))},
        {EncodableValue("width"), EncodableValue(data.size.width)},
        {EncodableValue("height"), EncodableValue((data.size.height))}}));
  } else {
    result->Error(kErrorCodeUnavailable, "Can't create window.");
  }
}

void FlutterWindowController::HandleDestroyWindow(
    MethodCall<> const& call,
    std::unique_ptr<MethodResult<>>& result) {
  auto const* const arguments{call.arguments()};
  auto const* const map{std::get_if<EncodableMap>(arguments)};
  if (!map) {
    result->Error(kErrorCodeInvalidValue, "Method call argument is not a map.");
    return;
  }

  auto const view_id{
      GetSingleValueForKeyOrSendError<int>("viewId", map, result)};
  if (!view_id) {
    return;
  }
  if (view_id.value() < 0) {
    result->Error(kErrorCodeInvalidValue, "Value for 'viewId' (" +
                                              std::to_string(view_id.value()) +
                                              ") cannot be negative.");
    return;
  }

  if (!DestroyFlutterWindow(view_id.value())) {
    result->Error(kErrorCodeInvalidValue, "Can't find window with 'viewId' (" +
                                              std::to_string(view_id.value()) +
                                              ").");
    return;
  }

  result->Success();
}

void FlutterWindowController::SendOnWindowCreated(
    WindowArchetype archetype,
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

void FlutterWindowController::SendOnWindowDestroyed(
    FlutterViewId view_id) const {
  if (channel_) {
    channel_->InvokeMethod(
        "onWindowDestroyed",
        std::make_unique<EncodableValue>(EncodableMap{
            {EncodableValue("viewId"), EncodableValue(view_id)},
        }));
  }
}

void FlutterWindowController::SendOnWindowResized(FlutterViewId view_id) const {
  if (channel_) {
    auto const size{GetWindowSize(view_id)};
    channel_->InvokeMethod(
        "onWindowResized",
        std::make_unique<EncodableValue>(EncodableMap{
            {EncodableValue("viewId"), EncodableValue(view_id)},
            {EncodableValue("width"), EncodableValue(size.width)},
            {EncodableValue("height"), EncodableValue(size.height)}}));
  }
}

FlutterWindowSize FlutterWindowController::GetWindowSize(
    flutter::FlutterViewId view_id) const {
  auto* const hwnd{windows_.at(view_id)->GetHandle()};
  RECT frame_rect;
  DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frame_rect,
                        sizeof(frame_rect));

  // Convert to logical coordinates
  auto const dpr{FlutterDesktopGetDpiForHWND(hwnd) /
                 static_cast<double>(USER_DEFAULT_SCREEN_DPI)};
  frame_rect.left = static_cast<LONG>(frame_rect.left / dpr);
  frame_rect.top = static_cast<LONG>(frame_rect.top / dpr);
  frame_rect.right = static_cast<LONG>(frame_rect.right / dpr);
  frame_rect.bottom = static_cast<LONG>(frame_rect.bottom / dpr);

  auto const width{frame_rect.right - frame_rect.left};
  auto const height{frame_rect.bottom - frame_rect.top};
  return {static_cast<int>(width), static_cast<int>(height)};
}

void FlutterWindowController::HideWindowsSatellites(HWND opt_out_hwnd) {
  if (g_disable_satellite_hiding) {
    return;
  }

  // Helper function to check whether |hwnd| is a descendant of |ancestor|.
  auto const is_descendant_of{[](HWND hwnd, HWND ancestor) -> bool {
    auto current{ancestor};
    while (current) {
      current = GetParentOrOwner(current);
      if (current == hwnd) {
        return true;
      }
    }
    return false;
  }};

  // Helper function to check whether |window| has a child dialog.
  auto const has_dialog{[](Win32Window* window) -> bool {
    for (auto* const child : window->children_) {
      if (child->archetype_ == WindowArchetype::dialog) {
        return true;
      }
    }
    return false;
  }};

  std::lock_guard const lock(mutex_);
  for (auto const& [_, window] : windows_) {
    if (window->window_handle_ == opt_out_hwnd ||
        is_descendant_of(window->window_handle_, opt_out_hwnd)) {
      continue;
    }

    for (auto* const child : window->children_) {
      if (child->archetype_ != WindowArchetype::satellite) {
        continue;
      }
      if (!has_dialog(child)) {
        ShowWindow(child->window_handle_, SW_HIDE);
      }
    }
  }
}

void FlutterWindowController::ShowWindowAndAncestorsSatellites(HWND hwnd) {
  if (g_disable_satellite_hiding) {
    return;
  }

  auto current{hwnd};
  while (current) {
    for (auto* const child :
         Win32Window::GetThisFromHandle(current)->children_) {
      if (child->archetype_ == WindowArchetype::satellite) {
        ShowWindow(child->window_handle_, SW_SHOWNOACTIVATE);
      }
    }
    current = GetParentOrOwner(current);
  }

  // Hide satellites of all other top-level windows
  if (Win32Window::GetThisFromHandle(hwnd)->archetype_ !=
      WindowArchetype::satellite) {
    HideWindowsSatellites(hwnd);
  }
}

}  // namespace flutter
