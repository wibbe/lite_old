#include <stdio.h>
#include <SDL2/SDL.h>

#define D2D_USE_C_DEFINITIONS
#define D2D1_INIT_GUID
#define COBJMACROS
#define CINTERFACE
#define WIND32_MEAN_AND_LEAN

#include <windows.h>
#include <windowsx.h>
#include "dwrite.h"
//#include <dwrite/dwrite.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <wincodec.h>

#include "api/api.h"
#include "renderer.h"

#ifdef _WIN32
  #include <windows.h>
#elif __linux__
  #include <unistd.h>
#elif __APPLE__
  #include <mach-o/dyld.h>
#endif

#include "event.h"

#ifndef HINST_THISCOMPONENT
IMAGE_DOS_HEADER __ImageBase;
#define HINST_THISCOMPONENT ((HINSTANCE)&__ImageBase)
#endif

// Define the factory GUID ourselves
static GUID D2DFactory_GUID = { 0x06152247, 0x6f50, 0x465a, { 0x92, 0x45, 0x11, 0x8b, 0xfd, 0x3b, 0x60, 0x07 } };
static GUID DWriteFactory_GUID = { 0xb859ee5a, 0xd838, 0x4b5b, { 0xa2, 0xe8, 0x1a, 0xdc, 0x7d, 0x93, 0xdb, 0x48 } };

SDL_Window *window;

HWND hwnd;
ID2D1Factory * d2d_factory;
IDWriteFactory * write_factory;
int last_mouse_x = -1;
int last_mouse_y = -1;


static double get_scale(void) {
  float dpi;
  SDL_GetDisplayDPI(0, NULL, &dpi, NULL);
#if _WIN32
  return dpi / 96.0;
#else
  return 1.0;
#endif
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

float to_dips_size(float points)
{
   return (points / 72.0f) * 96.0f;
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
}

static HRESULT create_device_independent_resources(void) {
  HRESULT result = S_OK;

  result = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &D2DFactory_GUID, NULL, (void **)&d2d_factory);
  if (FAILED(result))
  {
    printf("Could not create D2D Factory\n");
    return result;
  }

  IUnknown * factory;
  result = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &DWriteFactory_GUID, &factory);
  if (FAILED(result))
  {
    printf("Could not create DirectWrite Factory\n");
    ID2D1Factory_Release(d2d_factory);
    return result;
  }

  write_factory = (IDWriteFactory *)factory;
  return result;
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
  }

  if (!handled)
    result = DefWindowProcW(hwnd, message, w_param, l_param);

  return result;
}


int main(int argc, char **argv) {
  const wchar_t * CLASS_NAME = L"LiteClass";

  printf("Test\n");

  if (FAILED(CoInitialize(NULL)))
  {
    printf("CoInitialize failed\n");
    return 1;
  }

  printf("Creating Device Independent Resources...\n");

  HRESULT result = create_device_independent_resources();
  if (FAILED(result))
  {
    CoUninitialize();
    return 1;
  }

  //SetProcessDPIAware();

  WNDCLASSW window_class = { sizeof(WNDCLASSW) };
  window_class.style = CS_HREDRAW | CS_VREDRAW;
  window_class.lpfnWndProc = window_proc;
  window_class.cbClsExtra = 0;
  window_class.cbWndExtra = sizeof(LONG_PTR);
  window_class.hInstance = HINST_THISCOMPONENT;
  window_class.hbrBackground = NULL;
  window_class.lpszMenuName = NULL;
  window_class.hCursor = LoadCursor(NULL, IDI_APPLICATION);
  window_class.lpszClassName = CLASS_NAME;

  RegisterClassW(&window_class);

  float dpiX = 96.0f;
  float dpiY = 96.0f;

  ID2D1Factory_GetDesktopDpi(d2d_factory, &dpiX, &dpiY);

  int screenWidth = GetSystemMetrics(SM_CXSCREEN);
  int screenHeight = GetSystemMetrics(SM_CYSCREEN);

  int windowWidth = ceil(screenWidth * 0.8f * dpiX / 96.0f);
  int windowHeight = ceil(screenHeight * 0.8f * dpiY / 96.0f);

  int windowLeft = (screenWidth - windowWidth) / 2;
  int windowTop = (screenHeight - windowHeight) / 2;

  printf("Creating Window...\n");

  hwnd = CreateWindowExW(0,
                         CLASS_NAME,
                         L"",
                         WS_OVERLAPPEDWINDOW,
                         windowLeft,
                         windowTop,
                         windowWidth,
                         windowHeight,
                         NULL,
                         NULL,
                         HINST_THISCOMPONENT,
                         NULL);
  result = hwnd ? S_OK : E_FAIL;
  if (FAILED(result))
  {
    printf("Failed to create window\n");
    IDWriteFactory_Release(write_factory);
    ID2D1Factory_Release(d2d_factory);
    CoUninitialize();
    return 1;
  }

  printf("Window created\n");

  ShowWindow(hwnd, SW_SHOWNORMAL);
  UpdateWindow(hwnd);

  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  SDL_EnableScreenSaver();
  SDL_EventState(SDL_DROPFILE, SDL_ENABLE);
  atexit(SDL_Quit);

#ifdef SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR /* Available since 2.0.8 */
  SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
#endif
#if SDL_VERSION_ATLEAST(2, 0, 5)
  SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
#endif

  SDL_DisplayMode dm;
  SDL_GetCurrentDisplayMode(0, &dm);

  window = SDL_CreateWindow(
    "", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, dm.w * 0.8, dm.h * 0.8,
    SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);
  init_window_icon();
  ren_init(window);


  lua_State *L = luaL_newstate();
  luaL_openlibs(L);
  api_load_libs(L);


  lua_newtable(L);
  for (int i = 0; i < argc; i++) {
    lua_pushstring(L, argv[i]);
    lua_rawseti(L, -2, i + 1);
  }
  lua_setglobal(L, "ARGS");

  lua_pushstring(L, "1.11");
  lua_setglobal(L, "VERSION");

  lua_pushstring(L, SDL_GetPlatform());
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
  SDL_DestroyWindow(window);

  IDWriteFactory_Release(write_factory);
  ID2D1Factory_Release(d2d_factory);
  CoUninitialize();

  return EXIT_SUCCESS;
}
