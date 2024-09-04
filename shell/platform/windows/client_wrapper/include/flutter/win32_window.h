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
// rendering and input handling.
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

  // Creates a Win32 window with the specified |title| and |client_size|. The
  // window style is determined by |archetype|. For
  // |FlutterWindowArchetype::satellite| and |FlutterWindowArchetype::popup|,
  // both |parent| and |positioner| must be provided; |positioner| is used only
  // for these archetypes. For |FlutterWindowArchetype::dialog|, a modal dialog
  // is created if |parent| is specified; otherwise, the dialog is modeless.
  // After creation, |OnCreate| is called and its result is returned.
  auto Create(std::wstring const& title,
              Size const& client_size,
              FlutterWindowArchetype archetype,
              std::optional<HWND> parent,
              std::optional<FlutterWindowPositioner> positioner) -> bool;

  // Release OS resources associated with window.
  void Destroy();

  // Inserts |content| into the window tree.
  void SetChildContent(HWND content);

  // Returns the backing window handle to enable clients to set icon and other
  // window properties. Returns nullptr if the window has been destroyed.
  auto GetHandle() -> HWND;

  // If true, closing this window will quit the application.
  void SetQuitOnClose(bool quit_on_close);
  auto GetQuitOnClose() const -> bool;

  // Returns the bounds of the current client area.
  auto GetClientArea() -> RECT;

 protected:
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
  friend class WindowClassRegistrar;
  friend class FlutterWindowController;

  // OS callback called by message pump. Handles the WM_NCCREATE message which
  // is passed when the non-client area is being created and enables automatic
  // non-client DPI scaling so that the non-client area automatically
  // responds to changes in DPI. All other messages are handled by
  // MessageHandler.
  static auto CALLBACK WndProc(HWND window,
                               UINT message,
                               WPARAM wparam,
                               LPARAM lparam) -> LRESULT;

  // Retrieves a class instance pointer for |window|.
  static auto GetThisFromHandle(HWND window) noexcept -> Win32Window*;

  // Controls whether satellites are hidden when their top-level window
  // and all its children become inactive. Enabled by default, this setting
  // applies globally.
  static void EnableSatelliteHiding(bool enable);

  // The window's archetype (e.g., regular, dialog, popup).
  FlutterWindowArchetype archetype_{FlutterWindowArchetype::regular};

  // Windows that have this window as their parent or owner.
  std::set<Win32Window*> children_;

  // The number of popups in |children_|, used to quickly check whether this
  // window has any popups.
  size_t num_child_popups_{};

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

  // Hides all satellite windows in the application, except for those that are
  // descendants of the current window or have a dialog as a child. If
  // |include_child_satellites| is true, the current window's satellites are
  // also considered for hiding.
  void HideWindowsSatellites(bool include_child_satellites);

  // Shows the satellite windows of this window and of its ancestors.
  void ShowWindowAndAncestorsSatellites();

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
