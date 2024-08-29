#ifndef FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_WIN32_WINDOW_H_
#define FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_WIN32_WINDOW_H_

#include <windows.h>

#include <optional>
#include <set>
#include <string>

namespace flutter {

enum class FlutterWindowArchetype {
  regular,
  floating_regular,
  dialog,
  satellite,
  popup,
  tip
};

struct FlutterWindowPositioner {
  struct Size {
    int32_t width;
    int32_t height;
  };

  struct Rect {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
  };

  struct Offset {
    int32_t dx;
    int32_t dy;
  };

  enum class Anchor {
    none,
    top,
    bottom,
    left,
    right,
    top_left,
    bottom_left,
    top_right,
    bottom_right
  };

  enum class Gravity {
    none,
    top,
    bottom,
    left,
    right,
    top_left,
    bottom_left,
    top_right,
    bottom_right
  };

  enum class ConstraintAdjustment {
    none = 0,
    slide_x = 1,
    slide_y = 2,
    flip_x = 4,
    flip_y = 8,
    resize_x = 16,
    resize_y = 32
  };

  std::optional<Rect> anchor_rect;
  Anchor anchor;
  Gravity gravity;
  Offset offset;
  uint32_t constraint_adjustment;
};

// A class abstraction for a high DPI-aware Win32 Window. Intended to be
// inherited from by classes that wish to specialize with custom
// rendering and input handling
class Win32Window {
 public:
  struct Point {
    unsigned int x;
    unsigned int y;
    Point(unsigned int x, unsigned int y) : x(x), y(y) {}
  };

  struct Size {
    unsigned int width;
    unsigned int height;
    Size(unsigned int width, unsigned int height)
        : width(width), height(height) {}
  };

  Win32Window();
  virtual ~Win32Window();

  // Creates a win32 window with |title| that is positioned and sized using
  // |origin| and |size|. New windows are created on the default monitor. Window
  // sizes are specified to the OS in physical pixels, hence to ensure a
  // consistent size this function will scale the inputted width and height as
  // as appropriate for the default monitor. The window is invisible until
  // |Show| is called. Returns true if the window was created successfully.
  bool Create(std::wstring const& title,
              Size const& client_size,
              FlutterWindowArchetype archetype,
              std::optional<HWND> parent,
              std::optional<FlutterWindowPositioner> positioner);

  // Release OS resources associated with window.
  void Destroy();

  // Inserts |content| into the window tree.
  void SetChildContent(HWND content);

  // Returns the backing Window handle to enable clients to set icon and other
  // window properties. Returns nullptr if the window has been destroyed.
  HWND GetHandle();

  // If true, closing this window will quit the application.
  void SetQuitOnClose(bool quit_on_close);
  auto GetQuitOnClose() const -> bool;

  // Return a RECT representing the bounds of the current client area.
  RECT GetClientArea();

 protected:
  // Processes and route salient window messages for mouse handling,
  // size change and DPI. Delegates handling of these to member overloads that
  // inheriting classes can handle.
  virtual LRESULT MessageHandler(HWND hwnd,
                                 UINT message,
                                 WPARAM wparam,
                                 LPARAM lparam);

  // Called when CreateAndShow is called, allowing subclass window-related
  // setup. Subclasses should return false if setup fails.
  virtual bool OnCreate();

  // Called when Destroy is called.
  virtual void OnDestroy();

  FlutterWindowArchetype archetype_{FlutterWindowArchetype::regular};
  std::set<Win32Window*> child_popups_;
  std::set<Win32Window*> child_satellites_;

 private:
  friend class WindowClassRegistrar;

  // OS callback called by message pump. Handles the WM_NCCREATE message which
  // is passed when the non-client area is being created and enables automatic
  // non-client DPI scaling so that the non-client area automatically
  // responds to changes in DPI. All other messages are handled by
  // MessageHandler.
  static LRESULT CALLBACK WndProc(HWND window,
                                  UINT message,
                                  WPARAM wparam,
                                  LPARAM lparam);

  // Retrieves a class instance pointer for |window|
  static Win32Window* GetThisFromHandle(HWND window) noexcept;

  bool quit_on_close_ = false;

  // window handle for top level window.
  HWND window_handle_ = nullptr;

  // window handle for hosted content.
  HWND child_content_ = nullptr;

  // Offset between the position of this window and the position of its
  // parent.
  POINT offset_from_parent_{0, 0};

  // Prevents the non-client area from being redrawn as inactive when child
  // popups are being destroyed.
  bool suppress_nc_inactive_redraw{false};

  void CloseChildPopups();
  void HideWindowsSatellites(bool include_child_satellites);
  void ShowWindowAndAncestorsSatellites();
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_WINDOWS_CLIENT_WRAPPER_INCLUDE_FLUTTER_WIN32_WINDOW_H_
