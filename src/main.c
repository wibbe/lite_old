#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>


#define WIND32_MEAN_AND_LEAN
#include <windows.h>
#include <windowsx.h>

#include "api/api.h"
#include "renderer.h"
#include "event.h"

#ifndef HINST_THISCOMPONENT
IMAGE_DOS_HEADER __ImageBase;
#define HINST_THISCOMPONENT ((HINSTANCE)&__ImageBase)
#endif

HWND hwnd;
int last_mouse_x = -1;
int last_mouse_y = -1;


static double get_scale(void) {
  HINSTANCE lib = LoadLibrary("user32.dll");
  int (*GetDpiForWindow)(HWND) = (void*) GetProcAddress(lib, "GetDpiForWindow");
  float dpi = GetDpiForWindow(hwnd) / 96.0;
  FreeLibrary(lib);

  return dpi;
}


void get_exe_filename(char *buf, int sz) {
#if _WIN32
  int len = GetModuleFileName(NULL, buf, sz - 1);
  buf[len] = '\0';
#elif __linux__
  char path[512];
  sprintf(path, "/proc/%d/exe", getpid());
  int len = readlink(path, buf, sz - 1);
  buf[len] = '\0';
#elif __APPLE__
  unsigned size = sz;
  _NSGetExecutablePath(buf, &size);
#else
  strcpy(buf, "./lite");
#endif
}

wchar_t * to_wstr(const char * in, int * text_length)
{
   int len = MultiByteToWideChar(CP_UTF8, 0, in, -1, NULL, 0);
   if (len == 0)
      return NULL;

   wchar_t * out = malloc(sizeof(wchar_t) * len);
   MultiByteToWideChar(CP_UTF8, 0, in, -1, out, len);

   if (text_length != NULL)
      *text_length = len;

   return out;
}


static void init_window_icon(void) {
#if 0
#ifndef _WIN32
  #include "../icon.inl"
  (void) icon_rgba_len; /* unused */
  SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(
    icon_rgba, 64, 64,
    32, 64 * 4,
    0x000000ff,
    0x0000ff00,
    0x00ff0000,
    0xff000000);
  SDL_SetWindowIcon(window, surf);
  SDL_FreeSurface(surf);
#endif
#endif
}


static void handle_key(int down, int key) {
  char single_char_str[2] = {0};
  char * name = NULL;

  switch (key) {
    case VK_SHIFT:
      name = "left shift";
      break;
    case VK_LSHIFT:
      name = "left shift";
      break;
    case VK_RSHIFT:
      name = "right shift";
      break;
    case VK_RETURN:
      name = "return";
      break;
    case VK_ESCAPE:
      name = "escape";
      break;
    case VK_LEFT:
      name = "left";
      break;
    case VK_RIGHT:
      name = "right";
      break;
    case VK_UP:
      name = "up";
      break;
    case VK_DOWN:
      name = "down";
      break;
    case VK_END:
      name = "end";
      break;
    case VK_HOME:
      name = "home";
      break;
    case VK_SPACE:
      name = "space";
      break;
    case VK_BACK:
      name = "backspace";
      break;
    case VK_TAB:
      name = "tab";
      break;
    case VK_PRIOR:
      name = "pageup";
      break;
    case VK_NEXT:
      name = "pagedown";
      break;
    case VK_CONTROL:
      name = "left ctrl";
      break;
    case VK_LCONTROL:
      name = "left ctrl";
      break;
    case VK_RCONTROL:
      name = "right ctrl";
      break;
    case VK_MENU:
      name = "alt";
      break;
    case VK_INSERT:
      name = "insert";
      break;
    case VK_DELETE:
      name = "delete";
      break;
    default:
      {
        if ((key >= 48 && key <= 57) || (key >= 65 && key <= 90)) {
          single_char_str[0] = tolower(key);
          name = single_char_str;
        }
      }
      break;
  }

  if (name != NULL) {
    if (down) {
      event_t event = { .type = EVENT_KEYPRESSED };
      strcpy(event.keypressed.name, name);
      event_push(event);
    } else {
      event_t event = { .type = EVENT_KEYRELEASED };
      strcpy(event.keyreleased.name, name);
      event_push(event);
    }
  }
}


static LRESULT window_proc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
   LRESULT result = 0;
   BOOL handled = FALSE;

  switch (message) {
    case WM_CLOSE:
       DestroyWindow(hwnd);
       break;

    case WM_DESTROY:
      PostQuitMessage(0);
      result = 1;
      handled = TRUE;
      break;

    case WM_DISPLAYCHANGE:
      InvalidateRect(hwnd, NULL, FALSE);
      result = 0;
      handled = TRUE;
      break;

    case WM_PAINT:
      ValidateRect(hwnd, NULL);
      result = 0;
      handled = TRUE;
      break;

    case WM_SIZE:
      {
        event_t event = { .type = EVENT_RESIZE, .resize.width = LOWORD(l_param), .resize.height = HIWORD(l_param) };
        ren_resize(event.resize.width, event.resize.height);
        event_push(event);
        result = 0;
        handled = TRUE;
      }
      break;

    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
      handle_key(1, w_param);
      result = 0;
      handled = TRUE;
      break;

    case WM_SYSKEYUP:
    case WM_KEYUP:
      handle_key(0, w_param);
      result = 0;
      handled = TRUE;
      break;

    case WM_MOUSEWHEEL:
      {
        event_t event = { .type = EVENT_MOUSEWHEEL, .mousewheel.delta = GET_WHEEL_DELTA_WPARAM(w_param) / WHEEL_DELTA };
        event_push(event);
        result = 0;
        handled = TRUE;
      }
      break;

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
      {
        event_t event = { .type = EVENT_MOUSEPRESS, .mousepress.x = GET_X_LPARAM(l_param), .mousepress.y = GET_Y_LPARAM(l_param), .mousepress.clicks = 1 };

        switch (message)
        {
           case WM_LBUTTONDOWN:
              SetCapture(hwnd);
              event.mousepress.button = 1;
              break;

           case WM_RBUTTONDOWN:
              event.mousepress.button = 3;
              break;

           case WM_MBUTTONDOWN:
              event.mousepress.button = 2;
              break;
        }

        event_push(event);
        result = 0;
        handled = TRUE;
      }
      break;

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
      {
        event_t event = { .type = EVENT_MOUSERELEASE, .mouserelease.x = GET_X_LPARAM(l_param), .mouserelease.y = GET_Y_LPARAM(l_param) };

        switch (message)
        {
           case WM_LBUTTONUP:
              ReleaseCapture();
              event.mouserelease.button = 1;
              break;

           case WM_RBUTTONUP:
              event.mouserelease.button = 3;
              break;

           case WM_MBUTTONUP:
              event.mouserelease.button = 2;
              break;
        }

        event_push(event);
        result = 0;
        handled = TRUE;
      }
      break;

    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDBLCLK:
      {
        event_t event = { .type = EVENT_MOUSEPRESS, .mousepress.x = GET_X_LPARAM(l_param), .mousepress.y = GET_Y_LPARAM(l_param), .mousepress.clicks = 2 };

        switch (message)
        {
           case WM_LBUTTONDBLCLK:
              SetCapture(hwnd);
              event.mousepress.button = 1;
              break;

           case WM_RBUTTONDBLCLK:
              event.mousepress.button = 3;
              break;
        }

        event_push(event);
        result = 0;
        handled = TRUE;
      }
      break;

    case WM_MOUSEMOVE:
      {
        event_t event = { .type = EVENT_MOUSEMOVED, .mousemoved.x = GET_X_LPARAM(l_param), .mousemoved.y = GET_Y_LPARAM(l_param) };

        if (last_mouse_x == -1) {
          last_mouse_x = event.mousemoved.x;
          event.mousemoved.xrel = 0;
        } else {
          event.mousemoved.xrel = event.mousemoved.x - last_mouse_x;
          last_mouse_x = event.mousemoved.x;
        }

        if (last_mouse_y == -1) {
          last_mouse_y = event.mousemoved.y;
          event.mousemoved.yrel = 0;
        } else {
          event.mousemoved.yrel = event.mousemoved.y - last_mouse_y;
          last_mouse_y = event.mousemoved.y;
        }

        event_push(event);
        result = 0;
        handled = TRUE;
      }
      break;

    case WM_CHAR:
      {
        int ch = (int)w_param;
        if (ch >= 32 && ch <= 127)
        {
          event_t event = { .type = EVENT_TEXTINPUT, .textinput.ch = (char)ch };
          event_push(event);
        }
        result = 0;
        handled = TRUE;
      }
      break;
  }

  if (!handled)
    result = DefWindowProcW(hwnd, message, w_param, l_param);

  return result;
}


//int main(int argc, char **argv) {
int WinMain(HINSTANCE h_instance, HINSTANCE h_prev_instance, LPSTR cmd_line, int show_cmd) {

  HINSTANCE lib = LoadLibrary("user32.dll");
  int (*SetProcessDPIAware)() = (void*) GetProcAddress(lib, "SetProcessDPIAware");
  SetProcessDPIAware();
  FreeLibrary(lib);

  const wchar_t * CLASS_NAME = L"LiteClass";
  WNDCLASSW window_class = { sizeof(WNDCLASSW) };
  window_class.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
  window_class.lpfnWndProc = window_proc;
  window_class.cbClsExtra = 0;
  window_class.cbWndExtra = sizeof(LONG_PTR);
  window_class.hInstance = HINST_THISCOMPONENT;
  window_class.hbrBackground = NULL;
  window_class.lpszMenuName = NULL;
  window_class.hCursor = LoadCursor(NULL, IDI_APPLICATION);
  window_class.lpszClassName = CLASS_NAME;

  RegisterClassW(&window_class);

  int screen_width = GetSystemMetrics(SM_CXSCREEN);
  int screen_height = GetSystemMetrics(SM_CYSCREEN);

  int window_width = ceil(screen_width * 0.8f);
  int window_height = ceil(screen_height * 0.8f);

  int window_left = (screen_width - window_width) / 2;
  int window_top = (screen_height - window_height) / 2;

  hwnd = CreateWindowExW(0,
                         CLASS_NAME,
                         L"",
                         WS_OVERLAPPEDWINDOW,
                         window_left,
                         window_top,
                         window_width,
                         window_height,
                         NULL,
                         NULL,
                         HINST_THISCOMPONENT,
                         NULL);
  if (hwnd == NULL)
    return 1;

  ShowWindow(hwnd, SW_SHOWNORMAL);
  UpdateWindow(hwnd);

  init_window_icon();
  ren_init(window_width, window_height);

  lua_State *L = luaL_newstate();
  luaL_openlibs(L);
  api_load_libs(L);

  lua_newtable(L);
  for (int i = 0; i < __argc; i++) {
    lua_pushstring(L, __argv[i]);
    lua_rawseti(L, -2, i + 1);
  }
  lua_setglobal(L, "ARGS");

  lua_pushstring(L, "1.11");
  lua_setglobal(L, "VERSION");

  lua_pushstring(L, "windows");
  lua_setglobal(L, "PLATFORM");

  lua_pushnumber(L, get_scale());
  lua_setglobal(L, "SCALE");

  char exename[2048];
  get_exe_filename(exename, sizeof(exename));
  lua_pushstring(L, exename);
  lua_setglobal(L, "EXEFILE");


  (void) luaL_dostring(L,
    "local core\n"
    "xpcall(function()\n"
    "  SCALE = tonumber(os.getenv(\"LITE_SCALE\")) or SCALE\n"
    "  PATHSEP = package.config:sub(1, 1)\n"
    "  EXEDIR = EXEFILE:match(\"^(.+)[/\\\\].*$\")\n"
    "  package.path = EXEDIR .. '/data/?.lua;' .. package.path\n"
    "  package.path = EXEDIR .. '/data/?/init.lua;' .. package.path\n"
    "  core = require('core')\n"
    "  core.init()\n"
    "  core.run()\n"
    "end, function(err)\n"
    "  print('Error: ' .. tostring(err))\n"
    "  print(debug.traceback(nil, 2))\n"
    "  if core and core.on_error then\n"
    "    pcall(core.on_error, err)\n"
    "  end\n"
    "  os.exit(1)\n"
    "end)");

  ren_close();
  lua_close(L);

  return EXIT_SUCCESS;
}
