#ifndef FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_WIN32_WINDOW_H_
#define FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_WIN32_WINDOW_H_

#include "windowing.h"

#include <windows.h>

#include <optional>
#include <set>
#include <string>

namespace flutter {

class FlutterWindowController;

// A class abstraction for a high DPI-aware Win32 Window. Intended to be
// inherited from by classes that wish to specialize with custom
// rendering and input handling.
class Win32Window {
 public:
  explicit Win32Window(FlutterWindowController* window_controller);
  virtual ~Win32Window() = default;

 protected:
  // Creates a native Win32 window. |class_name| is the name of the window class
  // registered for this window. |title| is the window title string.
  // |client_size| specifies the requested size of the client rectangle (i.e.,
  // the size of the view). The window style is determined by |archetype|. For
  // |FlutterWindowArchetype::satellite| and |FlutterWindowArchetype::popup|,
  // both |parent| and |positioner| must be provided; |positioner| is used only
  // for these archetypes. For |FlutterWindowArchetype::dialog|, a modal dialog
  // is created if |parent| is specified; otherwise, the dialog is modeless.
  // After successful creation, |OnCreate| is called, and its result is
  // returned. Otherwise, the return value is false.
  auto Create(LPCWSTR class_name,
              std::wstring const& title,
              FlutterWindowSize const& client_size,
              WindowArchetype archetype,
              std::optional<HWND> parent,
              std::optional<WindowPositioner> positioner) -> bool;

  // Release OS resources associated with window.
  void Destroy();

  // Inserts |content| into the window tree.
  void SetChildContent(HWND content);

  // Returns the backing window handle to enable clients to set icon and other
  // window properties. Returns nullptr if the window has been destroyed.
  auto GetHandle() const -> HWND;

  // If true, closing this window will quit the application.
  void SetQuitOnClose(bool quit_on_close);
  auto GetQuitOnClose() const -> bool;

  // Returns the bounds of the current client area.
  auto GetClientArea() const -> RECT;

  // Processes and route salient window messages for mouse handling,
  // size change and DPI. Delegates handling of these to member overloads that
  // inheriting classes can handle.
  virtual auto MessageHandler(HWND hwnd,
                              UINT message,
                              WPARAM wparam,
                              LPARAM lparam) -> LRESULT;

  // Called when Create is called, allowing subclass window-related setup.
  // Subclasses should return false if setup fails.
  virtual auto OnCreate() -> bool;

  // Called when Destroy is called.
  virtual void OnDestroy();

  // The controller this window is controlled by.
  FlutterWindowController* window_controller_;

 private:
  friend class FlutterWindowController;

  // OS callback called by message pump. Handles the WM_NCCREATE message which
  // is passed when the non-client area is being created and enables automatic
  // non-client DPI scaling so that the non-client area automatically
  // responds to changes in DPI. All other messages are handled by
  // MessageHandler.
  static auto CALLBACK WndProc(HWND hwnd,
                               UINT message,
                               WPARAM wparam,
                               LPARAM lparam) -> LRESULT;

  // Retrieves a class instance pointer for |hwnd|.
  static auto GetThisFromHandle(HWND hwnd) -> Win32Window*;

  // The window's archetype (e.g., regular, dialog, popup).
  WindowArchetype archetype_{WindowArchetype::regular};

  // Windows that have this window as their parent or owner.
  std::set<Win32Window*> children_;

  // The number of popups in |children_|, used to quickly check whether this
  // window has any popups.
  size_t num_child_popups_{0};

  // Indicates whether closing this window will quit the application.
  bool quit_on_close_{false};

  // Handle for the top-level window.
  HWND window_handle_{nullptr};

  // Handle for hosted child content window.
  HWND child_content_{nullptr};

  // Offset between this window's position and its owner's position.
  POINT offset_from_owner_{0, 0};

  // Controls whether the non-client area can be redrawn as inactive.
  // Enabled by default, but temporarily disabled during child popup destruction
  // to prevent flickering.
  bool enable_redraw_non_client_as_inactive_{true};

  // Closes the popups of this window.
  void CloseChildPopups();

  // Enables or disables this window and all its descendants.
  void EnableWindowAndDescendants(bool enable);

  // Enforces modal behavior by enabling the deepest dialog in the subtree
  // rooted at the top-level window, along with its descendants, while
  // disabling all other windows in the subtree. This ensures that the dialog
  // and its children remain active and interactive. If no dialog is found,
  // all windows in the subtree are enabled.
  void UpdateModalState();
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_WIN32_WINDOW_H_
