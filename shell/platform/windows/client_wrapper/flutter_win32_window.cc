#include "include/flutter/flutter_win32_window.h"

#include "include/flutter/encodable_value.h"
#include "include/flutter/flutter_window_controller.h"

#include <windows.h>

namespace flutter {

FlutterWin32Window::FlutterWin32Window(std::shared_ptr<FlutterEngine> engine)
    : engine_(std::move(engine)) {}

auto FlutterWin32Window::flutter_controller()
    -> std::unique_ptr<FlutterViewController> const& {
  return flutter_controller_;
}

bool FlutterWin32Window::OnCreate() {
  if (!Win32Window::OnCreate()) {
    return false;
  }

  RECT const frame = GetClientArea();

  // The size here must match the window dimensions to avoid unnecessary surface
  // creation / destruction in the startup path.
  flutter_controller_ = std::make_unique<FlutterViewController>(
      frame.right - frame.left, frame.bottom - frame.top, engine_);
  // Ensure that basic setup of the controller was successful.
  if (!flutter_controller_->view()) {
    return false;
  }

  SetChildContent(flutter_controller_->view()->GetNativeWindow());

  // TODO(loicsharma): Hide the window until the first frame is rendered.
  // Single window apps use the engine's next frame callback to show the window.
  // This doesn't work for multi window apps as the engine cannot have multiple
  // next frame callbacks. If multiple windows are created, only the last one
  // will be shown.
  return true;
}

void FlutterWin32Window::OnDestroy() {
  if (flutter_controller_) {
    FlutterWindowController::instance().destroyWindow(
        flutter_controller_->view_id(), false);
    if (flutter_controller_) {
      flutter_controller_ = nullptr;
    }
  }

  Win32Window::OnDestroy();
}

LRESULT
FlutterWin32Window::MessageHandler(HWND hwnd,
                                   UINT const message,
                                   WPARAM const wparam,
                                   LPARAM const lparam) {
  // Give Flutter, including plugins, an opportunity to handle window messages.
  if (flutter_controller_) {
    std::optional<LRESULT> result =
        flutter_controller_->HandleTopLevelWindowProc(hwnd, message, wparam,
                                                      lparam);
    if (result) {
      return *result;
    }
  }

  switch (message) {
    case WM_FONTCHANGE:
      engine_->ReloadSystemFonts();
      break;
    case WM_SIZE:
      if (flutter_controller_) {
        FlutterWindowController::instance().sendOnWindowResized(
            flutter_controller_->view_id());
      }
      break;
    default:
      break;
  }

  return Win32Window::MessageHandler(hwnd, message, wparam, lparam);
}

}  // namespace flutter
