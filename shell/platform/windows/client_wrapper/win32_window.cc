#include "include/flutter/win32_window.h"

#include "include/flutter/flutter_window_controller.h"

#include <dwmapi.h>

namespace {

constexpr const wchar_t kWindowClassName[] = L"FLUTTER_WIN32_WINDOW";

// The number of Win32Window objects that currently exist.
int g_active_window_count = 0;

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
                         Point const& origin,
                         Size const& size,
                         FlutterWindowArchetype archetype,
                         HWND parent) {
  Destroy();

  archetype_ = archetype;

  wchar_t const* window_class{
      WindowClassRegistrar::GetInstance()->GetWindowClass()};

  POINT const target_point = {static_cast<LONG>(origin.x),
                              static_cast<LONG>(origin.y)};
  HMONITOR const monitor =
      MonitorFromPoint(target_point, MONITOR_DEFAULTTONEAREST);
  UINT const dpi = FlutterDesktopGetDpiForMonitor(monitor);
  auto const scale_factor = dpi / 96.0;

  // TODO(loicsharma): Hide the window until the first frame is rendered.
  DWORD window_style{WS_VISIBLE};

  switch (archetype) {
    case FlutterWindowArchetype::regular:
      window_style |= WS_OVERLAPPEDWINDOW;
      break;
    case FlutterWindowArchetype::popup:
      if (auto* const parent_window{GetThisFromHandle(parent)}) {
        if (parent_window->child_content_ != nullptr) {
          SetFocus(parent_window->child_content_);
        }
        parent_window->child_popups_.insert(this);
      }
      window_style |= WS_POPUP;
      break;
    // TODO: Handle the remaining archetypes
    default:
      std::abort();
  }

  // Scale helper to convert logical scaler values to physical using passed in
  // scale factor
  auto const scale{[](int source, double scale_factor) {
    return static_cast<int>(source * scale_factor);
  }};

  HWND window{CreateWindow(
      window_class, title.c_str(), window_style, scale(origin.x, scale_factor),
      scale(origin.y, scale_factor), scale(size.width, scale_factor),
      scale(size.height, scale_factor), parent, nullptr,
      GetModuleHandle(nullptr), this)};

  if (!window) {
    return false;
  }

  UpdateTheme(window);

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
  if (archetype_ == FlutterWindowArchetype::popup) {
    if (auto* const parent_window{GetParent(window_handle_)}) {
      GetThisFromHandle(parent_window)->child_popups_.erase(this);
    }
  }
}

}  // namespace flutter
