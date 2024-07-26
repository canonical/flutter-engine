#ifndef FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_FLUTTER_WIN32_WINDOW_H_
#define FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_FLUTTER_WIN32_WINDOW_H_

#include "flutter_view_controller.h"

#include "win32_window.h"

namespace flutter {

// A window that does nothing but host a Flutter view.
class FlutterWin32Window : public Win32Window {
 public:
  // Creates a new FlutterWin32Window hosting a Flutter view running |engine|.
  explicit FlutterWin32Window(std::shared_ptr<FlutterEngine> engine);
  ~FlutterWin32Window() override = default;

  auto flutter_controller() -> std::unique_ptr<FlutterViewController> const&;

 protected:
  // Win32Window:
  bool OnCreate() override;
  void OnDestroy() override;
  LRESULT MessageHandler(HWND hwnd,
                         UINT message,
                         WPARAM wparam,
                         LPARAM lparam) override;

 private:
  // The engine this window is attached to.
  std::shared_ptr<FlutterEngine> engine_;

  // The Flutter instance hosted by this window.
  std::unique_ptr<FlutterViewController> flutter_controller_;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_FLUTTER_WIN32_WINDOW_H_
