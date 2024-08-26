#include "include/flutter/win32_window.h"

#include "include/flutter/flutter_window_controller.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include <dwmapi.h>

namespace {

constexpr const wchar_t kWindowClassName[] = L"FLUTTER_WIN32_WINDOW";

// The number of Win32Window objects that currently exist.
int g_active_window_count = 0;

auto getLastErrorAsString() -> std::string {
  LPWSTR message_buffer{nullptr};

  if (auto const size{FormatMessage(
          FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
              FORMAT_MESSAGE_IGNORE_INSERTS,
          nullptr, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
          reinterpret_cast<LPTSTR>(&message_buffer), 0, nullptr)}) {
    std::wstring const wide_message(message_buffer, size);
    LocalFree(message_buffer);
    message_buffer = nullptr;

    if (auto const buffer_size{
            WideCharToMultiByte(CP_UTF8, 0, wide_message.c_str(), -1, nullptr,
                                0, nullptr, nullptr)}) {
      std::string message(buffer_size, 0);
      WideCharToMultiByte(CP_UTF8, 0, wide_message.c_str(), -1, &message[0],
                          buffer_size, nullptr, nullptr);
      return message;
    }
  }

  if (message_buffer) {
    LocalFree(message_buffer);
  }
  std::ostringstream oss;
  oss << "Format message failed with 0x" << std::hex << std::setfill('0')
      << std::setw(8) << GetLastError() << '\n';
  return oss.str();
}

std::tuple<flutter::Win32Window::Point, flutter::Win32Window::Size>
applyPositioner(flutter::FlutterWindowPositioner const& positioner,
                flutter::Win32Window::Size const& size,
                HWND parent_hwnd) {
  auto const dpr{FlutterDesktopGetDpiForHWND(parent_hwnd) /
                 static_cast<double>(USER_DEFAULT_SCREEN_DPI)};
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

  PointF const center{.x = (anchor_rect.left + anchor_rect.right) / 2.0,
                      .y = (anchor_rect.top + anchor_rect.bottom) / 2.0};
  PointF child_size{static_cast<double>(size.width),
                    static_cast<double>(size.height)};
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
      static_cast<unsigned int>(origin_dc.x),
      static_cast<unsigned int>(origin_dc.y)};
  flutter::Win32Window::Size const new_size{
      static_cast<unsigned int>(child_size.x),
      static_cast<unsigned int>(child_size.y)};
  return {origin_lc, new_size};
}

// Calculates the required window rectangle, in physical coordinates, that
// will accomodate the requested client size given in logical coordinates.
// The window rectangle accounts for window borders and non-client areas.
auto calculateWindowRect(flutter::Win32Window::Size client_size,
                         DWORD window_style,
                         DWORD extended_window_style,
                         HWND parent_hwnd) -> RECT {
  auto const dpi{FlutterDesktopGetDpiForHWND(parent_hwnd)};
  auto const scale_factor{static_cast<double>(dpi) / USER_DEFAULT_SCREEN_DPI};
  RECT rect{.left = 0,
            .top = 0,
            .right = static_cast<LONG>(client_size.width * scale_factor),
            .bottom = static_cast<LONG>(client_size.height * scale_factor)};

  HMODULE const user32_module{LoadLibraryA("User32.dll")};
  if (user32_module) {
    using AdjustWindowRectExForDpi = BOOL __stdcall(
        LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle, UINT dpi);

    auto* const adjust_window_rect_ext_for_dpi{
        reinterpret_cast<AdjustWindowRectExForDpi*>(
            GetProcAddress(user32_module, "AdjustWindowRectExForDpi"))};
    if (adjust_window_rect_ext_for_dpi) {
      if (adjust_window_rect_ext_for_dpi(&rect, window_style, FALSE,
                                         extended_window_style, dpi)) {
        FreeLibrary(user32_module);
        return rect;
      } else {
        std::cerr << "Failed to run AdjustWindowRectExForDpi: "
                  << getLastErrorAsString() << '\n';
      }
    } else {
      std::cerr << "Failed to retrieve AdjustWindowRectExForDpi address from "
                   "User32.dll.\n";
    }
    FreeLibrary(user32_module);
  } else {
    std::cerr << "Failed to load User32.dll.\n";
  }

  if (!AdjustWindowRectEx(&rect, window_style, FALSE, extended_window_style)) {
    std::cerr << "Failed to run AdjustWindowRectEx: " << getLastErrorAsString()
              << '\n';
    return rect;
  }

  return rect;
}

// Calculate the offset between the position of a window and the position of its
// parent.
POINT CalculateWindowOffset(HWND window, HWND parent) {
  POINT offset{0, 0};
  if (window && parent) {
    RECT window_rect;
    RECT parent_rect;
    if (GetWindowRect(window, &window_rect) &&
        GetWindowRect(parent, &parent_rect)) {
      offset.x = window_rect.left - parent_rect.left;
      offset.y = window_rect.top - parent_rect.top;
    }
  }
  return offset;
}

// Dynamically loads the |EnableNonClientDpiScaling| from the User32 module.
// This API is only needed for PerMonitor V1 awareness mode.
void EnableFullDpiSupportIfAvailable(HWND hwnd) {
  HMODULE user32_module = LoadLibraryA("User32.dll");
  if (!user32_module) {
    return;
  }

  using EnableNonClientDpiScaling = BOOL __stdcall(HWND hwnd);

  auto enable_non_client_dpi_scaling =
      reinterpret_cast<EnableNonClientDpiScaling*>(
          GetProcAddress(user32_module, "EnableNonClientDpiScaling"));
  if (enable_non_client_dpi_scaling != nullptr) {
    enable_non_client_dpi_scaling(hwnd);
  }

  FreeLibrary(user32_module);
}

void EnableTransparentWindowBackground(HWND hwnd) {
  HMODULE user32_module = LoadLibraryA("User32.dll");
  if (!user32_module) {
    return;
  }

  enum WINDOWCOMPOSITIONATTRIB { WCA_ACCENT_POLICY = 19 };

  struct WINDOWCOMPOSITIONATTRIBDATA {
    WINDOWCOMPOSITIONATTRIB Attrib;
    PVOID pvData;
    SIZE_T cbData;
  };

  using SetWindowCompositionAttribute =
      BOOL(__stdcall*)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

  auto set_window_composition_attribute =
      reinterpret_cast<SetWindowCompositionAttribute>(
          GetProcAddress(user32_module, "SetWindowCompositionAttribute"));
  if (set_window_composition_attribute != nullptr) {
    enum ACCENT_STATE { ACCENT_DISABLED = 0 };

    struct ACCENT_POLICY {
      ACCENT_STATE AccentState;
      DWORD AccentFlags;
      DWORD GradientColor;
      DWORD AnimationId;
    };

    ACCENT_POLICY accent = {ACCENT_DISABLED, 2, static_cast<DWORD>(0), 0};
    WINDOWCOMPOSITIONATTRIBDATA data{.Attrib = WCA_ACCENT_POLICY,
                                     .pvData = &accent,
                                     .cbData = sizeof(accent)};
    set_window_composition_attribute(hwnd, &data);

    MARGINS const margins = {-1};
    ::DwmExtendFrameIntoClientArea(hwnd, &margins);
    BOOL const enable = TRUE;
    INT effect_value = 1;
    ::DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &effect_value,
                            sizeof(enable));
  }

  FreeLibrary(user32_module);
}

/// Window attribute that enables dark mode window decorations.
///
/// Redefined in case the developer's machine has a Windows SDK older than
/// version 10.0.22000.0.
/// See:
/// https://docs.microsoft.com/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// Update the window frame's theme to match the system theme.
void UpdateTheme(HWND window) {
  // Registry key for app theme preference.
  const wchar_t kGetPreferredBrightnessRegKey[] =
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
  const wchar_t kGetPreferredBrightnessRegValue[] = L"AppsUseLightTheme";

  // A value of 0 indicates apps should use dark mode. A non-zero or missing
  // value indicates apps should use light mode.
  DWORD light_mode;
  DWORD light_mode_size = sizeof(light_mode);
  LSTATUS const result =
      RegGetValue(HKEY_CURRENT_USER, kGetPreferredBrightnessRegKey,
                  kGetPreferredBrightnessRegValue, RRF_RT_REG_DWORD, nullptr,
                  &light_mode, &light_mode_size);

  if (result == ERROR_SUCCESS) {
    BOOL enable_dark_mode = light_mode == 0;
    DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE,
                          &enable_dark_mode, sizeof(enable_dark_mode));
  }
}

}  // namespace

namespace flutter {

// Manages the Win32Window's window class registration.
class WindowClassRegistrar {
 public:
  ~WindowClassRegistrar() = default;

  // Returns the singleton registrar instance.
  static WindowClassRegistrar* GetInstance() {
    if (!instance_) {
      instance_ = new WindowClassRegistrar();
    }
    return instance_;
  }

  // Returns the name of the window class, registering the class if it hasn't
  // previously been registered.
  const wchar_t* GetWindowClass();

  // Unregisters the window class. Should only be called if there are no
  // instances of the window.
  void UnregisterWindowClass();

 private:
  WindowClassRegistrar() = default;

  static WindowClassRegistrar* instance_;

  bool class_registered_ = false;
};

WindowClassRegistrar* WindowClassRegistrar::instance_ = nullptr;

const wchar_t* WindowClassRegistrar::GetWindowClass() {
  int const idi_app_icon{101};
  if (!class_registered_) {
    WNDCLASS window_class{};
    window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    window_class.lpszClassName = kWindowClassName;
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.cbClsExtra = 0;
    window_class.cbWndExtra = 0;
    window_class.hInstance = GetModuleHandle(nullptr);
    window_class.hIcon =
        LoadIcon(window_class.hInstance, MAKEINTRESOURCE(idi_app_icon));
    window_class.hbrBackground = 0;
    window_class.lpszMenuName = nullptr;
    window_class.lpfnWndProc = flutter::Win32Window::WndProc;
    RegisterClass(&window_class);
    class_registered_ = true;
  }
  return kWindowClassName;
}

void WindowClassRegistrar::UnregisterWindowClass() {
  UnregisterClass(kWindowClassName, nullptr);
  class_registered_ = false;
}

Win32Window::Win32Window() {
  ++g_active_window_count;
}

Win32Window::~Win32Window() {
  --g_active_window_count;
  Destroy();
}

bool Win32Window::Create(std::wstring const& title,
                         Size const& client_size,
                         FlutterWindowArchetype archetype,
                         std::optional<HWND> parent,
                         std::optional<FlutterWindowPositioner> positioner) {
  archetype_ = archetype;

  DWORD window_style{};
  DWORD extended_window_style{};

  switch (archetype) {
    case FlutterWindowArchetype::regular:
      window_style |= WS_OVERLAPPEDWINDOW;
      break;
    case FlutterWindowArchetype::floating_regular:
      // TODO
      break;
    case FlutterWindowArchetype::dialog:
      window_style |= WS_OVERLAPPED | WS_CAPTION;
      extended_window_style |= WS_EX_DLGMODALFRAME;
      if (!parent) {
        // If the dialog has no parent, add a minimize box and a system menu
        // (which includes a close button)
        window_style |= WS_MINIMIZEBOX | WS_SYSMENU;
      } else {
        // If the dialog has a parent, make it modal by disabling the parent
        // window and the parent's satellites
        EnableWindow(parent.value(), FALSE);
        auto* const parent_window{GetThisFromHandle(parent.value())};
        for (auto* const satellite : parent_window->child_satellites_) {
          EnableWindow(satellite->GetHandle(), FALSE);
        }
        // If the parent window has the WS_EX_TOOLWINDOW style, apply the same
        // style to the dialog
        if (GetWindowLongPtr(parent.value(), GWL_EXSTYLE) & WS_EX_TOOLWINDOW) {
          extended_window_style |= WS_EX_TOOLWINDOW;
        }
      }
      break;
    case FlutterWindowArchetype::satellite:
      window_style |= WS_OVERLAPPEDWINDOW;
      extended_window_style |= WS_EX_TOOLWINDOW;
      if (auto* const parent_window{
              GetThisFromHandle(parent.value_or(nullptr))}) {
        if (parent_window->child_content_ != nullptr) {
          SetFocus(parent_window->child_content_);
        }
        parent_window->child_satellites_.insert(this);
      }
      break;
    case FlutterWindowArchetype::popup:
      window_style |= WS_POPUP;
      if (auto* const parent_window{
              GetThisFromHandle(parent.value_or(nullptr))}) {
        if (parent_window->child_content_ != nullptr) {
          SetFocus(parent_window->child_content_);
        }
        parent_window->child_popups_.insert(this);
      }
      break;
    case FlutterWindowArchetype::tip:
      // TODO
      break;
    default:
      std::abort();
  }

  auto window_size{[&]() -> Size {
    auto const window_rect{calculateWindowRect(client_size, window_style,
                                               extended_window_style,
                                               parent.value_or(nullptr))};
    return {static_cast<unsigned>(window_rect.right - window_rect.left),
            static_cast<unsigned>(window_rect.bottom - window_rect.top)};
  }()};

  // Window position in physical coordinates.
  // Default positioning values (CW_USEDEFAULT) are used if the window
  // has no parent or if the origin point is not provided.
  auto const [x, y]{[&]() -> std::tuple<int, int> {
    if (parent) {
      if (positioner) {
        // Adjust origin and size if a positioner is provided
        Point origin{0, 0};
        std::tie(origin, window_size) = applyPositioner(
            positioner.value(), window_size, parent.value_or(nullptr));
        return {origin.x, origin.y};
      } else if (archetype == FlutterWindowArchetype::dialog && parent) {
        // For parented dialogs, center the dialog in the parent frame
        RECT parent_frame;
        DwmGetWindowAttribute(parent.value(), DWMWA_EXTENDED_FRAME_BOUNDS,
                              &parent_frame, sizeof(parent_frame));
        return {
            (parent_frame.left + parent_frame.right - window_size.width) / 2,
            (parent_frame.top + parent_frame.bottom - window_size.height) / 2};
      }
    }
    return {CW_USEDEFAULT, CW_USEDEFAULT};
  }()};

  auto const* window_class{
      WindowClassRegistrar::GetInstance()->GetWindowClass()};

  auto const window{CreateWindowEx(
      extended_window_style, window_class, title.c_str(), window_style, x, y,
      window_size.width, window_size.height, parent.value_or(nullptr), nullptr,
      GetModuleHandle(nullptr), this)};

  if (!window) {
    auto const error_message{getLastErrorAsString()};
    std::cerr << "Cannot create window due to a CreateWindowEx error: "
              << error_message.c_str() << '\n';
    return false;
  }

  // Adjust the window position so that its origin is the top-left corner of the
  // window frame
  RECT frame_rect;
  DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame_rect,
                        sizeof(frame_rect));
  RECT window_rect;
  GetWindowRect(window, &window_rect);
  auto const left_dropshadow_width{frame_rect.left - window_rect.left};
  auto const top_dropshadow_height{window_rect.top - frame_rect.top};
  SetWindowPos(window, nullptr, window_rect.left - left_dropshadow_width,
               window_rect.top - top_dropshadow_height, 0, 0,
               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

  if (parent) {
    offset_from_parent_ = CalculateWindowOffset(window, parent.value());
  }

  UpdateTheme(window);

  ShowWindow(window, SW_SHOW);

  return OnCreate();
}

// static
LRESULT CALLBACK Win32Window::WndProc(HWND window,
                                      UINT message,
                                      WPARAM wparam,
                                      LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto* window_struct = reinterpret_cast<CREATESTRUCT*>(lparam);
    SetWindowLongPtr(window, GWLP_USERDATA,
                     reinterpret_cast<LONG_PTR>(window_struct->lpCreateParams));

    auto* that = static_cast<Win32Window*>(window_struct->lpCreateParams);
    EnableFullDpiSupportIfAvailable(window);
    EnableTransparentWindowBackground(window);
    that->window_handle_ = window;
  } else if (Win32Window* that = GetThisFromHandle(window)) {
    return that->MessageHandler(window, message, wparam, lparam);
  }

  return DefWindowProc(window, message, wparam, lparam);
}

LRESULT
Win32Window::MessageHandler(HWND hwnd,
                            UINT message,
                            WPARAM wparam,
                            LPARAM lparam) {
  switch (message) {
    case WM_DESTROY:
      window_handle_ = nullptr;
      Destroy();
      if (quit_on_close_) {
        PostQuitMessage(0);
      }
      return 0;

    case WM_DPICHANGED: {
      auto* newRectSize = reinterpret_cast<RECT*>(lparam);
      LONG const newWidth = newRectSize->right - newRectSize->left;
      LONG const newHeight = newRectSize->bottom - newRectSize->top;

      SetWindowPos(hwnd, nullptr, newRectSize->left, newRectSize->top, newWidth,
                   newHeight, SWP_NOZORDER | SWP_NOACTIVATE);

      return 0;
    }
    case WM_SIZE: {
      RECT const rect = GetClientArea();
      if (child_content_ != nullptr) {
        // Size and position the child window.
        MoveWindow(child_content_, rect.left, rect.top, rect.right - rect.left,
                   rect.bottom - rect.top, TRUE);
      }
      return 0;
    }

    case WM_ACTIVATE:
      if (wparam != WA_INACTIVE) {
        if (archetype_ != FlutterWindowArchetype::popup) {
          // If this window is not a popup and is being activated, close the
          // popups anchored to other windows
          for (auto const& [_, window] :
               FlutterWindowController::instance().windows()) {
            window->CloseChildPopups();
          }
        }
        // Close child popups if this window is being activated
        CloseChildPopups();
      }

      if (child_content_ != nullptr) {
        SetFocus(child_content_);
      }
      return 0;

    case WM_NCACTIVATE:
      if (wparam == FALSE && archetype_ != FlutterWindowArchetype::popup &&
          !child_popups_.empty()) {
        // If an inactive title bar is to be drawn, and this is a top-level
        // window with popups, force the title bar to be drawn in its active
        // colors
        return TRUE;
      }
      break;

    case WM_ACTIVATEAPP:
      if (wparam == FALSE) {
        // Close child popups if a window belonging to a different application
        // is being activated
        CloseChildPopups();
      }
      return 0;

    case WM_MOVE: {
      if (auto* const owner_window{GetWindow(window_handle_, GW_OWNER)}) {
        offset_from_parent_ =
            CalculateWindowOffset(window_handle_, owner_window);
      }

      // Move satellites attached to this window
      RECT rect;
      GetWindowRect(hwnd, &rect);
      for (auto* satellite : child_satellites_) {
        RECT rect_satellite;
        GetWindowRect(satellite->GetHandle(), &rect_satellite);
        MoveWindow(satellite->GetHandle(),
                   rect.left + satellite->offset_from_parent_.x,
                   rect.top + satellite->offset_from_parent_.y,
                   rect_satellite.right - rect_satellite.left,
                   rect_satellite.bottom - rect_satellite.top, FALSE);
      }
    } break;

    case WM_MOUSEACTIVATE:
      if (child_content_ != nullptr) {
        SetFocus(child_content_);
      }
      return MA_ACTIVATE;

    case WM_DWMCOLORIZATIONCOLORCHANGED:
      UpdateTheme(hwnd);
      return 0;

    default:
      break;
  }

  return DefWindowProc(window_handle_, message, wparam, lparam);
}

void Win32Window::CloseChildPopups() {
  if (!child_popups_.empty()) {
    auto popups{child_popups_};
    child_popups_.clear();
    for (auto* popup : popups) {
      popup->Destroy();
    }
  }
}

void Win32Window::Destroy() {
  OnDestroy();

  if (window_handle_) {
    DestroyWindow(window_handle_);
    window_handle_ = nullptr;
  }
  if (g_active_window_count == 0) {
    WindowClassRegistrar::GetInstance()->UnregisterWindowClass();
  }
}

Win32Window* Win32Window::GetThisFromHandle(HWND window) noexcept {
  return reinterpret_cast<Win32Window*>(
      GetWindowLongPtr(window, GWLP_USERDATA));
}

void Win32Window::SetChildContent(HWND content) {
  child_content_ = content;
  SetParent(content, window_handle_);
  RECT const frame = GetClientArea();

  MoveWindow(content, frame.left, frame.top, frame.right - frame.left,
             frame.bottom - frame.top, true);

  SetFocus(child_content_);
}

RECT Win32Window::GetClientArea() {
  RECT frame;
  GetClientRect(window_handle_, &frame);
  return frame;
}

HWND Win32Window::GetHandle() {
  return window_handle_;
}

void Win32Window::SetQuitOnClose(bool quit_on_close) {
  quit_on_close_ = quit_on_close;
}

auto Win32Window::GetQuitOnClose() const -> bool {
  return quit_on_close_;
}

bool Win32Window::OnCreate() {
  // No-op; provided for subclasses.
  return true;
}

void Win32Window::OnDestroy() {
  switch (archetype_) {
    case FlutterWindowArchetype::regular:
      break;
    case FlutterWindowArchetype::floating_regular:
      break;
    case FlutterWindowArchetype::dialog:
      if (auto* const owner_window_handle{
              GetWindow(window_handle_, GW_OWNER)}) {
        EnableWindow(owner_window_handle, TRUE);
        auto* const owner_window{GetThisFromHandle(owner_window_handle)};
        for (auto* const satellite : owner_window->child_satellites_) {
          EnableWindow(satellite->GetHandle(), TRUE);
        }

        SetForegroundWindow(owner_window_handle);
      }
      break;
    case FlutterWindowArchetype::satellite:
      if (auto* const parent_window{GetParent(window_handle_)}) {
        GetThisFromHandle(parent_window)->child_satellites_.erase(this);
      }
      break;
    case FlutterWindowArchetype::popup:
      if (auto* const parent_window{GetParent(window_handle_)}) {
        GetThisFromHandle(parent_window)->child_popups_.erase(this);
      }
      break;
    case FlutterWindowArchetype::tip:
      break;
    default:
      std::cerr << "Unhandled window archetype encountered in "
                   "Win32Window::OnDestroy: "
                << static_cast<int>(archetype_) << "\n";
      std::abort();
  }
}

}  // namespace flutter
