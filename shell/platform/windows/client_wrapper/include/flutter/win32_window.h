#ifndef FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_WIN32_WINDOW_H_
#define FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_WIN32_WINDOW_H_

#include "win32_wrapper.h"
#include "windowing.h"

#include <windows.h>

#include <memory>
#include <optional>
#include <set>
#include <string>

namespace flutter {

// A class abstraction for a high DPI-aware Win32 Window. Intended to be
// inherited from by classes that wish to specialize with custom
// rendering and input handling.
class Win32Window {
 public:
  Win32Window();
  explicit Win32Window(std::shared_ptr<Win32Wrapper> wrapper);
  virtual ~Win32Window();

  // Retrieves a class instance pointer for |hwnd|.
  static auto GetThisFromHandle(HWND hwnd) -> Win32Window*;

  // Returns the backing window handle to enable clients to set icon and other
  // window properties. Returns nullptr if the window has been destroyed.
  auto GetHandle() const -> HWND;

  // If |quit_on_close| is true, closing this window will quit the application.
  void SetQuitOnClose(bool quit_on_close);

  // Returns true if closing this window will cause the application to quit.
  auto GetQuitOnClose() const -> bool;

  // Returns the bounds of the current client area.
  auto GetClientArea() const -> RECT;

  // Returns the current window archetype.
  auto GetArchetype() const -> WindowArchetype;

 protected:
  // Creates a native Win32 window. |title| is the window title string.
  // |client_size| specifies the requested size of the client rectangle (i.e.,
  // the size of the view). The window style is determined by |archetype|.
  // After successful creation, |OnCreate| is called, and its result is
  // returned. Otherwise, the return value is false.
  auto Create(std::wstring const& title,
              WindowSize const& client_size,
              WindowArchetype archetype) -> bool;

  // Release OS resources associated with window.
  void Destroy();

  // Inserts |content| into the window tree.
  void SetChildContent(HWND content);

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

 private:
  friend class FlutterWindowController;

  // OS callback called by message pump. Handles the WM_NCCREATE message which
  // is passed when the non-client area is being created and enables automatic
  // non-client DPI scaling so that the non-client area automatically
  // responds to changes in DPI. All other messages are handled by the
  // controller's MessageHandler.
  static auto CALLBACK WndProc(HWND hwnd,
                               UINT message,
                               WPARAM wparam,
                               LPARAM lparam) -> LRESULT;

  // Wrapper for Win32 API calls.
  std::shared_ptr<Win32Wrapper> win32_;

  // The window's archetype (e.g., regular, dialog, popup).
  WindowArchetype archetype_{WindowArchetype::regular};

  // Indicates whether closing this window will quit the application.
  bool quit_on_close_{false};

  // Handle for the top-level window.
  HWND window_handle_{nullptr};

  // Handle for hosted child content window.
  HWND child_content_{nullptr};
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_WIN32_WINDOW_H_
