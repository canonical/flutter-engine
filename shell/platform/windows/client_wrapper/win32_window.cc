#include "include/flutter/win32_window.h"

#include "include/flutter/flutter_window_controller.h"

#include <algorithm>
#include <cassert>
#include <iomanip>
#include <sstream>

#include <dwmapi.h>

namespace {

constexpr const wchar_t kWindowClassName[] = L"FLUTTER_WIN32_WINDOW";

// The number of Win32Window objects that currently exist.
int g_active_window_count = 0;

// Retrieves the calling thread's last-error code message as a string,
// or a fallback message if the error message cannot be formatted.
auto GetLastErrorAsString() -> std::string {
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

// Computes the screen-space rectangle of a window placed according to the
// specified |positioner|. |child_size| is the size of the window frame.
// |anchor_rect| is the rectangle relative to which the window is placed.
// |parent_rect| is the rectangle of the parent window (|anchor_rect| will be
// clipped to this). |output_rect| is the rectangle of the output where the
// window will be placed. All sizes and rectangles are in physical coordinates.
auto PlaceWindow(flutter::FlutterWindowPositioner const& positioner,
                 flutter::FlutterWindowSize child_size,
                 flutter::FlutterWindowRectangle const& anchor_rect,
                 flutter::FlutterWindowRectangle const& parent_rect,
                 flutter::FlutterWindowRectangle const& output_rect)
    -> flutter::FlutterWindowRectangle {
  flutter::FlutterWindowRectangle default_result;

  auto const offset_for{[](flutter::FlutterWindowSize const& size,
                           flutter::FlutterWindowPositioner::Anchor anchor)
                            -> flutter::FlutterWindowPoint {
    switch (anchor) {
      case flutter::FlutterWindowPositioner::Anchor::top_left:
        return {0, 0};
      case flutter::FlutterWindowPositioner::Anchor::top:
        return {-size.width / 2, 0};
      case flutter::FlutterWindowPositioner::Anchor::top_right:
        return {-1 * size.width, 0};
      case flutter::FlutterWindowPositioner::Anchor::left:
        return {0, -size.height / 2};
      case flutter::FlutterWindowPositioner::Anchor::none:
        return {-size.width / 2, -size.height / 2};
      case flutter::FlutterWindowPositioner::Anchor::right:
        return {-1 * size.width, -size.height / 2};
      case flutter::FlutterWindowPositioner::Anchor::bottom_left:
        return {0, -1 * size.height};
      case flutter::FlutterWindowPositioner::Anchor::bottom:
        return {-size.width / 2, -1 * size.height};
      case flutter::FlutterWindowPositioner::Anchor::bottom_right:
        return {-1 * size.width, -1 * size.height};
      default:
        std::cerr << "Bad placement anchor\n";
        std::abort();
    }
  }};

  auto const anchor_position_for{[](flutter::FlutterWindowRectangle const& rect,
                                    flutter::FlutterWindowPositioner::Anchor
                                        anchor) -> flutter::FlutterWindowPoint {
    switch (anchor) {
      case flutter::FlutterWindowPositioner::Anchor::top_left:
        return rect.top_left;
      case flutter::FlutterWindowPositioner::Anchor::top:
        return rect.top_left +
               flutter::FlutterWindowSize{rect.size.width / 2, 0};
      case flutter::FlutterWindowPositioner::Anchor::top_right:
        return rect.top_left + flutter::FlutterWindowSize{rect.size.width, 0};
      case flutter::FlutterWindowPositioner::Anchor::left:
        return rect.top_left +
               flutter::FlutterWindowSize{0, rect.size.height / 2};
      case flutter::FlutterWindowPositioner::Anchor::none:
        return rect.top_left + flutter::FlutterWindowSize{rect.size.width / 2,
                                                          rect.size.height / 2};
      case flutter::FlutterWindowPositioner::Anchor::right:
        return rect.top_left + flutter::FlutterWindowSize{rect.size.width,
                                                          rect.size.height / 2};
      case flutter::FlutterWindowPositioner::Anchor::bottom_left:
        return rect.top_left + flutter::FlutterWindowSize{0, rect.size.height};
      case flutter::FlutterWindowPositioner::Anchor::bottom:
        return rect.top_left + flutter::FlutterWindowSize{rect.size.width / 2,
                                                          rect.size.height};
      case flutter::FlutterWindowPositioner::Anchor::bottom_right:
        return rect.top_left +
               flutter::FlutterWindowSize{rect.size.width, rect.size.height};
      default:
        std::cerr << "Bad placement anchor\n";
        std::abort();
    }
  }};

  auto const constrain_to{
      [](flutter::FlutterWindowRectangle const& r,
         flutter::FlutterWindowPoint const& p) -> flutter::FlutterWindowPoint {
        return {std::clamp(p.x, r.top_left.x, r.top_left.x + r.size.width),
                std::clamp(p.y, r.top_left.y, r.top_left.y + r.size.height)};
      }};

  auto const flip_anchor_x{[](flutter::FlutterWindowPositioner::Anchor anchor)
                               -> flutter::FlutterWindowPositioner::Anchor {
    switch (anchor) {
      case flutter::FlutterWindowPositioner::Anchor::top_left:
        return flutter::FlutterWindowPositioner::Anchor::top_right;
      case flutter::FlutterWindowPositioner::Anchor::top_right:
        return flutter::FlutterWindowPositioner::Anchor::top_left;
      case flutter::FlutterWindowPositioner::Anchor::left:
        return flutter::FlutterWindowPositioner::Anchor::right;
      case flutter::FlutterWindowPositioner::Anchor::right:
        return flutter::FlutterWindowPositioner::Anchor::left;
      case flutter::FlutterWindowPositioner::Anchor::bottom_left:
        return flutter::FlutterWindowPositioner::Anchor::bottom_right;
      case flutter::FlutterWindowPositioner::Anchor::bottom_right:
        return flutter::FlutterWindowPositioner::Anchor::bottom_left;
      default:
        return anchor;
    }
  }};

  auto const flip_anchor_y{[](flutter::FlutterWindowPositioner::Anchor anchor)
                               -> flutter::FlutterWindowPositioner::Anchor {
    switch (anchor) {
      case flutter::FlutterWindowPositioner::Anchor::top_left:
        return flutter::FlutterWindowPositioner::Anchor::bottom_left;
      case flutter::FlutterWindowPositioner::Anchor::top:
        return flutter::FlutterWindowPositioner::Anchor::bottom;
      case flutter::FlutterWindowPositioner::Anchor::top_right:
        return flutter::FlutterWindowPositioner::Anchor::bottom_right;
      case flutter::FlutterWindowPositioner::Anchor::bottom_left:
        return flutter::FlutterWindowPositioner::Anchor::top_left;
      case flutter::FlutterWindowPositioner::Anchor::bottom:
        return flutter::FlutterWindowPositioner::Anchor::top;
      case flutter::FlutterWindowPositioner::Anchor::bottom_right:
        return flutter::FlutterWindowPositioner::Anchor::top_right;
      default:
        return anchor;
    }
  }};

  auto const flip_offset_x{
      [](flutter::FlutterWindowPoint const& p) -> flutter::FlutterWindowPoint {
        return {-1 * p.x, p.y};
      }};

  auto const flip_offset_y{
      [](flutter::FlutterWindowPoint const& p) -> flutter::FlutterWindowPoint {
        return {p.x, -1 * p.y};
      }};

  {
    auto result =
        constrain_to(parent_rect, anchor_position_for(
                                      anchor_rect, positioner.parent_anchor) +
                                      positioner.offset) +
        offset_for(child_size, positioner.child_anchor);

    if (output_rect.contains({result, child_size})) {
      return flutter::FlutterWindowRectangle{result, child_size};
    }

    default_result = flutter::FlutterWindowRectangle{result, child_size};
  }

  if (positioner.constraint_adjustment &
      flutter::FlutterWindowPositioner::ConstraintAdjustment::flip_x) {
    auto result =
        constrain_to(parent_rect,
                     anchor_position_for(
                         anchor_rect, flip_anchor_x(positioner.parent_anchor)) +
                         flip_offset_x(positioner.offset)) +
        offset_for(child_size, flip_anchor_x(positioner.child_anchor));

    if (output_rect.contains({result, child_size})) {
      return flutter::FlutterWindowRectangle{result, child_size};
    }
  }

  if (positioner.constraint_adjustment &
      flutter::FlutterWindowPositioner::ConstraintAdjustment::flip_y) {
    auto result =
        constrain_to(parent_rect,
                     anchor_position_for(
                         anchor_rect, flip_anchor_y(positioner.parent_anchor)) +
                         flip_offset_y(positioner.offset)) +
        offset_for(child_size, flip_anchor_y(positioner.child_anchor));

    if (output_rect.contains({result, child_size})) {
      return flutter::FlutterWindowRectangle{result, child_size};
    }
  }

  if (positioner.constraint_adjustment &
          flutter::FlutterWindowPositioner::ConstraintAdjustment::flip_x &&
      positioner.constraint_adjustment &
          flutter::FlutterWindowPositioner::ConstraintAdjustment::flip_y) {
    auto result =
        constrain_to(
            parent_rect,
            anchor_position_for(anchor_rect, flip_anchor_x(flip_anchor_y(
                                                 positioner.parent_anchor))) +
                flip_offset_x(flip_offset_y(positioner.offset))) +
        offset_for(child_size,
                   flip_anchor_x(flip_anchor_y(positioner.child_anchor)));

    if (output_rect.contains({result, child_size})) {
      return flutter::FlutterWindowRectangle{result, child_size};
    }
  }

  {
    auto result =
        constrain_to(parent_rect, anchor_position_for(
                                      anchor_rect, positioner.parent_anchor) +
                                      positioner.offset) +
        offset_for(child_size, positioner.child_anchor);

    if (positioner.constraint_adjustment &
        flutter::FlutterWindowPositioner::ConstraintAdjustment::slide_x) {
      auto const left_overhang = result.x - output_rect.top_left.x;
      auto const right_overhang =
          (result.x + child_size.width) -
          (output_rect.top_left.x + output_rect.size.width);

      if (left_overhang < 0) {
        result.x -= left_overhang;
      } else if (right_overhang > 0) {
        result.x -= right_overhang;
      }
    }

    if (positioner.constraint_adjustment &
        flutter::FlutterWindowPositioner::ConstraintAdjustment::slide_y) {
      auto const top_overhang = result.y - output_rect.top_left.y;
      auto const bot_overhang =
          (result.y + child_size.height) -
          (output_rect.top_left.y + output_rect.size.height);

      if (top_overhang < 0) {
        result.y -= top_overhang;
      } else if (bot_overhang > 0) {
        result.y -= bot_overhang;
      }
    }

    if (output_rect.contains({result, child_size})) {
      return flutter::FlutterWindowRectangle{result, child_size};
    }
  }

  {
    auto result =
        constrain_to(parent_rect, anchor_position_for(
                                      anchor_rect, positioner.parent_anchor) +
                                      positioner.offset) +
        offset_for(child_size, positioner.child_anchor);

    if (positioner.constraint_adjustment &
        flutter::FlutterWindowPositioner::ConstraintAdjustment::resize_x) {
      auto const left_overhang = result.x - output_rect.top_left.x;
      auto const right_overhang =
          (result.x + child_size.width) -
          (output_rect.top_left.x + output_rect.size.width);

      if (left_overhang < 0) {
        result.x -= left_overhang;
        child_size.width += left_overhang;
      }

      if (right_overhang > 0) {
        child_size.width -= right_overhang;
      }
    }

    if (positioner.constraint_adjustment &
        flutter::FlutterWindowPositioner::ConstraintAdjustment::resize_y) {
      auto const top_overhang = result.y - output_rect.top_left.y;
      auto const bot_overhang =
          (result.y + child_size.height) -
          (output_rect.top_left.y + output_rect.size.height);

      if (top_overhang < 0) {
        result.y -= top_overhang;
        child_size.height += top_overhang;
      }

      if (bot_overhang > 0) {
        child_size.height -= bot_overhang;
      }
    }

    if (output_rect.contains({result, child_size})) {
      return flutter::FlutterWindowRectangle{result, child_size};
    }
  }

  return default_result;
}

// Estimates the size of the window frame, in physical coordinates, based on
// the given |window_size| (in physical coordinates) and the specified
// |window_style|, |extended_window_style|, and parent window |parent_hwnd|.
auto GetFrameSizeForWindowSize(flutter::FlutterWindowSize const& window_size,
                               DWORD window_style,
                               DWORD extended_window_style,
                               HWND parent_hwnd) -> flutter::FlutterWindowSize {
  RECT frame_rect{0, 0, static_cast<LONG>(window_size.width),
                  static_cast<LONG>(window_size.height)};

  WNDCLASS window_class{0};
  window_class.lpfnWndProc = DefWindowProc;
  window_class.hInstance = GetModuleHandle(nullptr);
  window_class.lpszClassName = L"FLUTTER_WIN32_WINDOW_TEMPORARY";
  RegisterClass(&window_class);

  window_style &= ~WS_VISIBLE;
  if (auto const window{CreateWindowEx(
          extended_window_style, window_class.lpszClassName, L"", window_style,
          CW_USEDEFAULT, CW_USEDEFAULT, window_size.width, window_size.height,
          parent_hwnd, nullptr, GetModuleHandle(nullptr), nullptr)}) {
    DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame_rect,
                          sizeof(frame_rect));
    DestroyWindow(window);
  }

  UnregisterClass(window_class.lpszClassName, nullptr);

  return {static_cast<int>(frame_rect.right - frame_rect.left),
          static_cast<int>(frame_rect.bottom - frame_rect.top)};
}

// Calculates the required window size, in physical coordinates, to
// accommodate the given |client_size| (in logical coordinates) for a window
// with the specified |window_style| and |extended_window_style|. The result
// accounts for window borders, non-client areas, and drop-shadow effects.
auto GetWindowSizeForClientSize(flutter::FlutterWindowSize const& client_size,
                                DWORD window_style,
                                DWORD extended_window_style,
                                HWND parent_hwnd)
    -> flutter::FlutterWindowSize {
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
        return {static_cast<int>(rect.right - rect.left),
                static_cast<int>(rect.bottom - rect.top)};
      } else {
        std::cerr << "Failed to run AdjustWindowRectExForDpi: "
                  << GetLastErrorAsString() << '\n';
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
    std::cerr << "Failed to run AdjustWindowRectEx: " << GetLastErrorAsString()
              << '\n';
  }
  return {static_cast<int>(rect.right - rect.left),
          static_cast<int>(rect.bottom - rect.top)};
}

// Calculates the offset from the top-left corner of |from| to the top-left
// corner of |to|. If either window handle is null or if the window positions
// cannot be retrieved, the offset will be (0, 0).
auto GetOffsetBetweenWindows(HWND from, HWND to) -> POINT {
  POINT offset{0, 0};
  if (to && from) {
    RECT to_rect;
    RECT from_rect;
    if (GetWindowRect(to, &to_rect) && GetWindowRect(from, &from_rect)) {
      offset.x = to_rect.left - from_rect.left;
      offset.y = to_rect.top - from_rect.top;
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

// Dynamically loads |SetWindowCompositionAttribute| from the User32 module uses
// it to make the window's background transparent.
void EnableTransparentWindowBackground(HWND hwnd) {
  HMODULE const user32_module{LoadLibraryA("User32.dll")};
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

  auto set_window_composition_attribute{
      reinterpret_cast<SetWindowCompositionAttribute>(
          GetProcAddress(user32_module, "SetWindowCompositionAttribute"))};
  if (set_window_composition_attribute != nullptr) {
    enum ACCENT_STATE { ACCENT_DISABLED = 0 };

    struct ACCENT_POLICY {
      ACCENT_STATE AccentState;
      DWORD AccentFlags;
      DWORD GradientColor;
      DWORD AnimationId;
    };

    // Set the accent policy to disable window composition
    ACCENT_POLICY accent{ACCENT_DISABLED, 2, static_cast<DWORD>(0), 0};
    WINDOWCOMPOSITIONATTRIBDATA data{.Attrib = WCA_ACCENT_POLICY,
                                     .pvData = &accent,
                                     .cbData = sizeof(accent)};
    set_window_composition_attribute(hwnd, &data);

    // Extend the frame into the client area and set the window's system
    // backdrop type for visual effects
    MARGINS const margins{-1};
    ::DwmExtendFrameIntoClientArea(hwnd, &margins);
    INT effect_value{1};
    ::DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &effect_value,
                            sizeof(BOOL));
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
  if (g_active_window_count == 0) {
    WindowClassRegistrar::GetInstance()->UnregisterWindowClass();
  }
}

auto Win32Window::Create(std::wstring const& title,
                         FlutterWindowSize const& client_size,
                         FlutterWindowArchetype archetype,
                         std::optional<HWND> parent,
                         std::optional<FlutterWindowPositioner> positioner)
    -> bool {
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
        // If the parent window has the WS_EX_TOOLWINDOW style, apply the same
        // style to the dialog
        if (GetWindowLongPtr(parent.value(), GWL_EXSTYLE) & WS_EX_TOOLWINDOW) {
          extended_window_style |= WS_EX_TOOLWINDOW;
        }
        GetThisFromHandle(parent.value())->children_.insert(this);
      }
      break;
    case FlutterWindowArchetype::satellite:
      window_style |= WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX;
      extended_window_style |= WS_EX_TOOLWINDOW;
      if (auto* const parent_window{
              GetThisFromHandle(parent.value_or(nullptr))}) {
        if (parent_window->child_content_ != nullptr) {
          SetFocus(parent_window->child_content_);
        }
        parent_window->children_.insert(this);
      } else {
        std::cerr << "The parent of a satellite must not be null.\n";
        std::abort();
      }
      break;
    case FlutterWindowArchetype::popup:
      window_style |= WS_POPUP;
      if (auto* const parent_window{
              GetThisFromHandle(parent.value_or(nullptr))}) {
        if (parent_window->child_content_ != nullptr) {
          SetFocus(parent_window->child_content_);
        }
        parent_window->children_.insert(this);
        ++parent_window->num_child_popups_;
      }
      break;
    case FlutterWindowArchetype::tip:
      // TODO
      break;
    default:
      std::cerr << "Unhandled window archetype: " << static_cast<int>(archetype)
                << "\n";
      std::abort();
  }

  // Window rectangle in physical coordinates.
  // Default positioning values (CW_USEDEFAULT) are used
  // if the window has no parent or positioner. Parented dialogs will be
  // centered in the parent's frame.
  auto const window_rect{[&]() -> FlutterWindowRectangle {
    auto const window_size{GetWindowSizeForClientSize(
        client_size, window_style, extended_window_style,
        parent.value_or(nullptr))};
    if (parent) {
      if (positioner) {
        auto const frame_size{GetFrameSizeForWindowSize(
            window_size, window_style, extended_window_style, parent.value())};

        // The rectangle of the parent's client area, in physical coordinates
        auto const parent_rect{
            [](HWND parent_window) -> FlutterWindowRectangle {
              RECT client_rect;
              GetClientRect(parent_window, &client_rect);
              POINT top_left{client_rect.left, client_rect.top};
              ClientToScreen(parent_window, &top_left);
              POINT bottom_right{client_rect.right, client_rect.bottom};
              ClientToScreen(parent_window, &bottom_right);
              return {
                  {top_left.x, top_left.y},
                  {bottom_right.x - top_left.x, bottom_right.y - top_left.y}};
            }(parent.value())};

        // The anchor rectangle, in physical coordinates
        auto const anchor_rect{[](FlutterWindowPositioner const& positioner,
                                  HWND parent_window,
                                  FlutterWindowRectangle const& parent_rect)
                                   -> FlutterWindowRectangle {
          if (positioner.anchor_rect) {
            auto const dpr{FlutterDesktopGetDpiForHWND(parent_window) /
                           static_cast<double>(USER_DEFAULT_SCREEN_DPI)};
            return {
                {parent_rect.top_left.x +
                     static_cast<int>(positioner.anchor_rect->top_left.x * dpr),
                 parent_rect.top_left.y +
                     static_cast<int>(positioner.anchor_rect->top_left.y *
                                      dpr)},
                {static_cast<int>(positioner.anchor_rect->size.width * dpr),
                 static_cast<int>(positioner.anchor_rect->size.height * dpr)}};
          } else {
            // If the anchor rect specified in the positioner is std::nullopt,
            // return an anchor rect that is equal to the window frame area
            RECT frame;
            DwmGetWindowAttribute(parent_window, DWMWA_EXTENDED_FRAME_BOUNDS,
                                  &frame, sizeof(frame));
            return {{frame.left, frame.top},
                    {frame.right - frame.left, frame.bottom - frame.top}};
          }
        }(positioner.value(), parent.value(), parent_rect)};

        // Rectangle of the monitor that has the largest area of intersection
        // with the anchor rectangle, in physical coordinates
        auto const output_rect{
            [](RECT anchor_rect)
                -> FlutterWindowRectangle {
              auto* monitor{
                  MonitorFromRect(&anchor_rect, MONITOR_DEFAULTTONEAREST)};
              MONITORINFO mi;
              mi.cbSize = sizeof(MONITORINFO);
              auto const bounds{
                  GetMonitorInfo(monitor, &mi) ? mi.rcWork : RECT{0, 0, 0, 0}};
              return {{bounds.left, bounds.top},
                      {bounds.right - bounds.left, bounds.bottom - bounds.top}};
            }({.left = static_cast<LONG>(anchor_rect.top_left.x),
                .top = static_cast<LONG>(anchor_rect.top_left.y),
                .right = static_cast<LONG>(anchor_rect.top_left.x +
                                           anchor_rect.size.width),
                .bottom = static_cast<LONG>(anchor_rect.top_left.y +
                                            anchor_rect.size.height)})};

        auto const rect{PlaceWindow(
            positioner.value(), frame_size, anchor_rect,
            positioner->anchor_rect ? parent_rect : anchor_rect, output_rect)};

        return {rect.top_left,
                {rect.size.width + window_size.width - frame_size.width,
                 rect.size.height + window_size.height - frame_size.height}};
      } else if (archetype == FlutterWindowArchetype::dialog) {
        // For parented dialogs, center the dialog in the parent frame
        RECT parent_frame;
        DwmGetWindowAttribute(parent.value(), DWMWA_EXTENDED_FRAME_BOUNDS,
                              &parent_frame, sizeof(parent_frame));
        FlutterWindowPoint const top_left{
            static_cast<int>(
                (parent_frame.left + parent_frame.right - window_size.width) *
                0.5),
            static_cast<int>(
                (parent_frame.top + parent_frame.bottom - window_size.height) *
                0.5)};
        return {top_left, window_size};
      }
    }
    return {{CW_USEDEFAULT, CW_USEDEFAULT}, window_size};
  }()};

  auto const* window_class{
      WindowClassRegistrar::GetInstance()->GetWindowClass()};

  CreateWindowEx(extended_window_style, window_class, title.c_str(),
                 window_style, window_rect.top_left.x, window_rect.top_left.y,
                 window_rect.size.width, window_rect.size.height,
                 parent.value_or(nullptr), nullptr, GetModuleHandle(nullptr),
                 this);

  if (!window_handle_) {
    auto const error_message{GetLastErrorAsString()};
    std::cerr << "Cannot create window due to a CreateWindowEx error: "
              << error_message.c_str() << '\n';
    return false;
  }

  // Adjust the window position so its origin aligns with the top-left corner
  // of the window frame, not the window rectangle (which includes the
  // drop-shadow). This adjustment must be done post-creation since the frame
  // rectangle is only available after the window has been created.
  RECT frame_rc;
  DwmGetWindowAttribute(window_handle_, DWMWA_EXTENDED_FRAME_BOUNDS, &frame_rc,
                        sizeof(frame_rc));
  RECT window_rc;
  GetWindowRect(window_handle_, &window_rc);
  auto const left_dropshadow_width{frame_rc.left - window_rc.left};
  auto const top_dropshadow_height{window_rc.top - frame_rc.top};
  SetWindowPos(window_handle_, nullptr, window_rc.left - left_dropshadow_width,
               window_rc.top - top_dropshadow_height, 0, 0,
               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

  if (parent) {
    if (auto* const owner_window{GetWindow(window_handle_, GW_OWNER)}) {
      offset_from_owner_ =
          GetOffsetBetweenWindows(owner_window, window_handle_);
    }
  }

  UpdateTheme(window_handle_);

  if (archetype == FlutterWindowArchetype::dialog && parent) {
    UpdateModalState();
  }

  ShowWindow(window_handle_, SW_SHOW);

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
      if (wparam == SIZE_MAXIMIZED) {
        // Hide satellites of the maximized window
        for (auto* const child : children_) {
          if (child->archetype_ == FlutterWindowArchetype::satellite) {
            ShowWindow(child->GetHandle(), SW_HIDE);
          }
        }
      } else if (wparam == SIZE_RESTORED) {
        // Show satellites of the restored window
        for (auto* const child : children_) {
          if (child->archetype_ == FlutterWindowArchetype::satellite) {
            ShowWindow(child->GetHandle(), SW_SHOWNOACTIVATE);
          }
        }
      }
      if (child_content_ != nullptr) {
        // Resize and reposition the child content window
        auto const rect{GetClientArea()};
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
        CloseChildPopups();
        FlutterWindowController::instance().showWindowAndAncestorsSatellites(
            window_handle_);
      }

      if (child_content_ != nullptr) {
        SetFocus(child_content_);
      }
      return 0;

    case WM_NCACTIVATE:
      if (wparam == FALSE && archetype_ != FlutterWindowArchetype::popup) {
        if (!enable_redraw_non_client_as_inactive_ || num_child_popups_ > 0) {
          // If an inactive title bar is to be drawn, and this is a top-level
          // window with popups, force the title bar to be drawn in its active
          // colors
          return TRUE;
        }
      }
      break;

    case WM_ACTIVATEAPP:
      if (wparam == FALSE) {
        // Close child popups and hide satellites from all windows if a window
        // belonging to a different application is being activated
        CloseChildPopups();
        FlutterWindowController::instance().hideWindowsSatellites(nullptr);
      }
      return 0;

    case WM_MOVE: {
      if (auto* const owner_window{GetWindow(window_handle_, GW_OWNER)}) {
        offset_from_owner_ =
            GetOffsetBetweenWindows(owner_window, window_handle_);
      }

      // Move satellites attached to this window
      RECT rect;
      GetWindowRect(hwnd, &rect);
      for (auto* const child : children_) {
        if (child->archetype_ == FlutterWindowArchetype::satellite) {
          RECT rect_satellite;
          GetWindowRect(child->GetHandle(), &rect_satellite);
          MoveWindow(child->GetHandle(),
                     rect.left + child->offset_from_owner_.x,
                     rect.top + child->offset_from_owner_.y,
                     rect_satellite.right - rect_satellite.left,
                     rect_satellite.bottom - rect_satellite.top, FALSE);
        }
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
  if (num_child_popups_ == 0) {
    return;
  }

  std::set<Win32Window*> popups;
  for (auto* const child : children_) {
    if (child->archetype_ == FlutterWindowArchetype::popup) {
      popups.insert(child);
    }
  }

  for (auto it{children_.begin()}; it != children_.end();) {
    if ((*it)->archetype_ == FlutterWindowArchetype::popup) {
      it = children_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto* popup : popups) {
    auto const parent_handle{GetParent(popup->window_handle_)};
    auto* const parent{GetThisFromHandle(parent_handle)};

    // Popups' parents are drawn with active colors even though they are
    // actually inactive. When a popup is destroyed, the parent might be
    // redrawn as inactive (reflecting its true state) before being redrawn as
    // active. To prevent flickering during this transition, disables
    // redrawing the non-client area as inactive.
    parent->enable_redraw_non_client_as_inactive_ = false;
    DestroyWindow(popup->GetHandle());
    parent->enable_redraw_non_client_as_inactive_ = true;

    // Repaint parent window to make sure its title bar is painted with the
    // color based on its actual activation state
    if (parent->num_child_popups_ == 0) {
      SetWindowPos(parent_handle, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
  }
}

void Win32Window::EnableWindowAndDescendants(bool enable) {
  EnableWindow(window_handle_, enable);
  for (auto* const child : children_) {
    child->EnableWindowAndDescendants(enable);
  }
}

void Win32Window::UpdateModalState() {
  auto const find_deepest_dialog{
      [](Win32Window* window, auto&& self) -> Win32Window* {
        Win32Window* deepest_dialog{nullptr};
        if (window->archetype_ == FlutterWindowArchetype::dialog) {
          deepest_dialog = window;
        }
        for (auto* const child : window->children_) {
          if (auto* const child_deepest_dialog{self(child, self)}) {
            deepest_dialog = child_deepest_dialog;
          }
        }
        return deepest_dialog;
      }};

  auto const get_parent_or_owner{[](HWND window) -> HWND {
    auto const parent{GetParent(window)};
    return parent ? parent : GetWindow(window, GW_OWNER);
  }};

  auto* root_ancestor_handle{window_handle_};
  while (auto* next{get_parent_or_owner(root_ancestor_handle)}) {
    root_ancestor_handle = next;
  }
  auto* const root_ancestor{GetThisFromHandle(root_ancestor_handle)};

  if (auto* const deepest_dialog{
          find_deepest_dialog(root_ancestor, find_deepest_dialog)}) {
    root_ancestor->EnableWindowAndDescendants(false);
    deepest_dialog->EnableWindowAndDescendants(true);
  } else {
    root_ancestor->EnableWindowAndDescendants(true);
  }
}

void Win32Window::Destroy() {
  OnDestroy();
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
        GetThisFromHandle(owner_window_handle)->children_.erase(this);
        UpdateModalState();
        SetFocus(owner_window_handle);
      }
      break;
    case FlutterWindowArchetype::satellite:
      if (auto* const owner_window_handle{
              GetWindow(window_handle_, GW_OWNER)}) {
        auto* const owner_window{GetThisFromHandle(owner_window_handle)};
        owner_window->children_.erase(this);
      }
      break;
    case FlutterWindowArchetype::popup:
      if (auto* const parent_window_handle{GetParent(window_handle_)}) {
        auto* const parent_window{GetThisFromHandle(parent_window_handle)};
        parent_window->children_.erase(this);
        assert(parent_window->num_child_popups_ > 0);
        --parent_window->num_child_popups_;
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
