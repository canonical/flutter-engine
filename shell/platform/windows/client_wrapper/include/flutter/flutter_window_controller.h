#ifndef FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_FLUTTER_WINDOW_CONTROLLER_H_
#define FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_FLUTTER_WINDOW_CONTROLLER_H_

#include <flutter_windows.h>

#include <mutex>

#include "flutter_engine.h"
#include "flutter_view.h"
#include "flutter_win32_window.h"
#include "method_channel.h"

namespace flutter {

struct FlutterWindowSize {
  int width = 0;
  int height = 0;
};

struct FlutterWindowCreationResult {
  FlutterViewId view_id;
  std::optional<FlutterViewId> parent_id = std::nullopt;
  FlutterWindowArchetype archetype;
  FlutterWindowSize size;
};

// A singleton controller for Flutter windows.
class FlutterWindowController {
 public:
  ~FlutterWindowController() = default;

  // Prevent copying and moving.
  FlutterWindowController(FlutterWindowController const&) = delete;
  FlutterWindowController(FlutterWindowController&&) = delete;
  FlutterWindowController& operator=(FlutterWindowController const&) = delete;
  FlutterWindowController& operator=(FlutterWindowController&&) = delete;

  using ViewWindowMap =
      std::unordered_map<FlutterViewId, std::unique_ptr<FlutterWin32Window>>;

  static FlutterWindowController& instance() {
    static FlutterWindowController instance;
    return instance;
  }

  void setEngine(std::shared_ptr<FlutterEngine> engine);
  auto createWindow(std::wstring const& title,
                    Win32Window::Size const& size,
                    FlutterWindowArchetype archetype,
                    std::optional<FlutterWindowPositioner> positioner,
                    std::optional<FlutterViewId> parent_view_id)
      -> std::optional<FlutterWindowCreationResult>;
  auto destroyWindow(FlutterViewId view_id, bool destroy_native_window) -> bool;
  auto windows() const -> ViewWindowMap const&;
  auto channel() const -> std::unique_ptr<MethodChannel<>> const&;

 private:
  friend class FlutterWin32Window;

  FlutterWindowController() = default;

  void initializeChannel();
  void sendOnWindowCreated(FlutterWindowArchetype archetype,
                           FlutterViewId view_id,
                           std::optional<FlutterViewId> parent_view_id) const;
  void sendOnWindowDestroyed(FlutterViewId view_id) const;
  void sendOnWindowResized(FlutterViewId view_id) const;
  void cleanupClosedWindows();
  FlutterWindowSize getWindowSize(flutter::FlutterViewId view_id) const;

  mutable std::mutex mutex_;
  std::unique_ptr<MethodChannel<>> channel_;
  std::shared_ptr<FlutterEngine> engine_;
  ViewWindowMap windows_;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_FLUTTER_WINDOW_CONTROLLER_H_
