#ifndef FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_FLUTTER_WINDOW_CONTROLLER_H_
#define FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_FLUTTER_WINDOW_CONTROLLER_H_

#include <mutex>

#include "flutter_engine.h"
#include "method_channel.h"
#include "windowing.h"

namespace flutter {

class FlutterWindowController {
 public:
  explicit FlutterWindowController(std::shared_ptr<FlutterEngine> engine);
  ~FlutterWindowController();

  // Prevent copying.
  FlutterWindowController(FlutterWindowController const&) = delete;
  FlutterWindowController& operator=(FlutterWindowController const&) = delete;

  auto CreateFlutterWindow(std::wstring const& title,
                           FlutterWindowSize const& size,
                           WindowArchetype archetype,
                           std::optional<WindowPositioner> positioner,
                           std::optional<FlutterViewId> parent_view_id)
      -> std::optional<WindowCreationResult>;
  auto DestroyFlutterWindow(FlutterViewId view_id) -> bool;

 private:
  friend class Win32Window;

  auto MessageHandler(HWND hwnd,
                      UINT message,
                      WPARAM wparam,
                      LPARAM lparam) -> LRESULT;
  void InitializeChannel(BinaryMessenger* messenger);
  void HandleCreateWindow(WindowArchetype archetype,
                          MethodCall<> const& call,
                          std::unique_ptr<MethodResult<>>& result);
  void HandleDestroyWindow(flutter::MethodCall<> const& call,
                           std::unique_ptr<flutter::MethodResult<>>& result);
  void SendOnWindowCreated(WindowArchetype archetype,
                           FlutterViewId view_id,
                           std::optional<FlutterViewId> parent_view_id) const;
  void SendOnWindowDestroyed(FlutterViewId view_id) const;
  void SendOnWindowResized(FlutterViewId view_id) const;
  auto GetWindowSize(FlutterViewId view_id) const -> FlutterWindowSize;

  // Hides all satellite windows in the application, except those that are
  // descendants of |opt_out_hwnd| or have a dialog as a child. By default,
  // |opt_out_hwnd| is null, so no window is excluded.
  void HideWindowsSatellites(HWND opt_out_hwnd = nullptr);
  // Shows the satellite windows of |hwnd| and of its ancestors.
  void ShowWindowAndAncestorsSatellites(HWND hwnd);

  mutable std::mutex mutex_;
  std::unique_ptr<MethodChannel<>> channel_;
  std::shared_ptr<FlutterEngine> engine_;
  std::unordered_map<FlutterViewId, std::shared_ptr<Win32Window>> windows_;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_FLUTTER_WINDOW_CONTROLLER_H_
