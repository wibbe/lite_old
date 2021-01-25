#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>

#define WIND32_MEAN_AND_LEAN
#include <windows.h>
#include <windowsx.h>

#include "lib/stb/stb_truetype.h"
#include "renderer.h"

#define MAX_GLYPHSET 256

#include <GL/gl.h>

typedef char GLchar;
typedef ptrdiff_t GLintptr;
typedef ptrdiff_t GLsizeiptr;

#ifndef HINST_THISCOMPONENT
IMAGE_DOS_HEADER __ImageBase;
#define HINST_THISCOMPONENT ((HINSTANCE)&__ImageBase)
#endif

#define GL_LIST \
    GL_F(void,      BlendEquation,           GLenum mode) \
    GL_F(void,      ActiveTexture,           GLenum texture)

typedef BOOL WINAPI wglChoosePixelFormatARBF(HDC hdc, const int *piAttribIList, const FLOAT *pfAttribFList, UINT nMaxFormats, int *piFormats, UINT *nNumFormats);
typedef HGLRC WINAPI wglCreateContextAttribsARBF(HDC hDC, HGLRC hShareContext, const int *attribList);

wglChoosePixelFormatARBF *wglChoosePixelFormatARB = 0;
wglCreateContextAttribsARBF *wglCreateContextAttribsARB = 0;

#define WGL_DRAW_TO_WINDOW_ARB                  0x2001
#define WGL_ACCELERATION_ARB                    0x2003
#define WGL_FULL_ACCELERATION_ARB               0x2027
#define WGL_SUPPORT_OPENGL_ARB                  0x2010
#define WGL_DOUBLE_BUFFER_ARB                   0x2011
#define WGL_PIXEL_TYPE_ARB                      0x2013
#define WGL_TYPE_RGBA_ARB                       0x202B
#define WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB        0x20A9
#define WGL_CONTEXT_MAJOR_VERSION_ARB           0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB           0x2092
#define WGL_CONTEXT_FLAGS_ARB                   0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB            0x9126
#define WGL_CONTEXT_DEBUG_BIT_ARB               0x0001
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002
#define GL_BGRA 0x80E1

// Common functions.
#define GL_F(ret, name, ...) typedef ret name##proc(__VA_ARGS__); name##proc * gl##name;
GL_LIST
#undef GL_F




struct RenImage {
  RenColor *pixels;
  int width, height;
};

typedef struct {
  RenImage *image;
  stbtt_bakedchar glyphs[256];
} GlyphSet;

struct RenFont {
  void *data;
  stbtt_fontinfo stbfont;
  GlyphSet *sets[MAX_GLYPHSET];
  float size;
  int height;
};

extern HWND hwnd;

static struct { int left, top, right, bottom; } clip;
static RenImage * back_buffer = NULL;
static int back_buffer_texture = 0;


static void* check_alloc(void *ptr) {
  if (!ptr) {
    fprintf(stderr, "Fatal error: memory allocation failed\n");
    exit(EXIT_FAILURE);
  }
  return ptr;
}


static const char* utf8_to_codepoint(const char *p, unsigned *dst) {
  unsigned res, n;
  switch (*p & 0xf0) {
    case 0xf0 :  res = *p & 0x07;  n = 3;  break;
    case 0xe0 :  res = *p & 0x0f;  n = 2;  break;
    case 0xd0 :
    case 0xc0 :  res = *p & 0x1f;  n = 1;  break;
    default   :  res = *p;         n = 0;  break;
  }
  while (n--) {
    res = (res << 6) | (*(++p) & 0x3f);
  }
  *dst = res;
  return p + 1;
}

static int set_window_pixel_format(HDC dc) {
  int pixel_format = 0;
  int extended_pixel_format = 0;
  if (wglChoosePixelFormatARB) {
    int attrib[] = {
      WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
      WGL_ACCELERATION_ARB, WGL_FULL_ACCELERATION_ARB,
      WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
      WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
      WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
      WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB, GL_TRUE,
      0,
    };
    wglChoosePixelFormatARB(dc, attrib, 0, 1, &pixel_format, &extended_pixel_format);
  }

  if (!extended_pixel_format) {
    PIXELFORMATDESCRIPTOR desired_pixel_format_desc = { 0 };

    desired_pixel_format_desc.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    desired_pixel_format_desc.nVersion = 1;
    desired_pixel_format_desc.dwFlags = PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW | PFD_DOUBLEBUFFER;
    desired_pixel_format_desc.iPixelType = PFD_TYPE_RGBA;
    desired_pixel_format_desc.cColorBits = 32;
    desired_pixel_format_desc.cAlphaBits = 8;
    desired_pixel_format_desc.cDepthBits = 32;
    desired_pixel_format_desc.dwLayerMask = PFD_MAIN_PLANE;

    pixel_format = ChoosePixelFormat(dc, &desired_pixel_format_desc);
    if (!pixel_format) {
      printf("ChoosePixelFormat failed\n");
      return 0;
    }
  }

  PIXELFORMATDESCRIPTOR pixel_format_desc;
  DescribePixelFormat(dc, pixel_format, sizeof(pixel_format_desc), &pixel_format_desc);
  if (!SetPixelFormat(dc, pixel_format, &pixel_format_desc)) {
    printf("SetPixelFormat failed\n");
    return 0;
  }

  return 1;
}

static int gl_init(void) {
  WNDCLASSA wc = {0};
  wc.style = CS_OWNDC;
  wc.lpfnWndProc = DefWindowProcA;
  wc.hInstance = HINST_THISCOMPONENT;
  wc.lpszClassName = "WGLLoaderClass";
  if (!RegisterClassA(&wc)) {
    return 0;
  }

  HWND window = CreateWindowExA(0, wc.lpszClassName, "WGLLoader", 0, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, wc.hInstance, 0);
  if (!window) {
    return 0;
  }

  HDC dc = GetDC(window);
  set_window_pixel_format(dc);
  HGLRC glc = wglCreateContext(dc);
  if (!wglMakeCurrent(dc, glc)) {
    return 0;
  }

  HINSTANCE dll = LoadLibraryA("opengl32.dll");
  if (!dll) {
    return 0;
  }

  typedef PROC WINAPI wglGetProcAddressF(LPCSTR lpszProc);

  wglGetProcAddressF* wglGetProcAddress = (wglGetProcAddressF*)GetProcAddress(dll, "wglGetProcAddress");
  wglChoosePixelFormatARB = (wglChoosePixelFormatARBF *)wglGetProcAddress("wglChoosePixelFormatARB");
  wglCreateContextAttribsARB = (wglCreateContextAttribsARBF *)wglGetProcAddress("wglCreateContextAttribsARB");

#define GL_F(ret, name, ...) \
            gl##name = (name##proc *)wglGetProcAddress("gl" #name); \
            if (!gl##name) { \
                printf("Function gl" #name " couldn't be loaded from opengl32.dll\n"); \
                goto end; \
            }
        GL_LIST
#undef GL_F
end:

    wglMakeCurrent(0, 0);
    wglDeleteContext(glc);
    ReleaseDC(window, dc);
    DestroyWindow(window);

    return 1;
}

static int window_init_gl(HWND hwnd) {
  HDC dc = GetDC(hwnd);
  set_window_pixel_format(dc);

  static int gl_attribs[] =
  {
    WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
    WGL_CONTEXT_MINOR_VERSION_ARB, 2,
    WGL_CONTEXT_FLAGS_ARB, 0,
    WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
    0,
  };

  HGLRC glc = 0;
  if (wglCreateContextAttribsARB) {
      glc = wglCreateContextAttribsARB(dc, 0, gl_attribs);
  } else {
    printf("wglCreateContextAttribsARB not available\n");
    return 0;
  }

  if (!glc) {
    printf("wglCreateContextAttribsARB failed\n");
    return 0;
  }

  if (!wglMakeCurrent(dc, glc)) {
    printf("wglMakeCurrent failed\n");
    return 0;
  }

  ReleaseDC(hwnd, dc);
  return 1;
}

void ren_init(int width, int height) {
  ren_set_clip_rect( (RenRect) { 0, 0, width, height } );
  back_buffer = ren_new_image(width, height);

  gl_init();
  window_init_gl(hwnd);

  glEnable(GL_TEXTURE_2D);

  glGenTextures(1, &back_buffer_texture);
  glBindTexture(GL_TEXTURE_2D, back_buffer_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

void ren_close(void) {
  ren_free_image(back_buffer);
}

void ren_resize(int width, int height) {
  window_bitmap.bmiHeader.biWidth = width;
  window_bitmap.bmiHeader.biHeight = -height;

  ren_free_image(back_buffer);
  back_buffer = ren_new_image(width, height);
}

void ren_update_rects(RenRect *rects, int count) {
  /*
  SDL_UpdateWindowSurfaceRects(window, (SDL_Rect*) rects, count);
  static bool initial_frame = true;
  if (initial_frame) {
    SDL_ShowWindow(window);
    initial_frame = false;
  }
  */
}


void ren_set_clip_rect(RenRect rect) {
  clip.left   = rect.x;
  clip.top    = rect.y;
  clip.right  = rect.x + rect.width;
  clip.bottom = rect.y + rect.height;
}


void ren_get_size(int *x, int *y) {
  *x = back_buffer->width;
  *y = back_buffer->height;
}

void ren_present(void) {
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  glViewport(0, 0, back_buffer->width, back_buffer->height);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0.0, back_buffer->width, 0.0, back_buffer->height, 1.0, -1.0);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glBindTexture(GL_TEXTURE_2D, back_buffer_texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, back_buffer->width, back_buffer->height, 0, GL_BGRA, GL_UNSIGNED_BYTE, back_buffer->pixels);

  glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, 0.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(back_buffer->width, 0.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(back_buffer->width, back_buffer->height);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, back_buffer->height);
  glEnd();

  HDC dc = GetDC(hwnd);
  SwapBuffers(dc);
  ReleaseDC(hwnd, dc);
}


RenImage* ren_new_image(int width, int height) {
  assert(width > 0 && height > 0);
  RenImage *image = malloc(sizeof(RenImage) + width * height * sizeof(RenColor));
  check_alloc(image);
  image->pixels = (void*) (image + 1);
  image->width = width;
  image->height = height;
  return image;
}


void ren_free_image(RenImage *image) {
  free(image);
}


static GlyphSet* load_glyphset(RenFont *font, int idx) {
  GlyphSet *set = check_alloc(calloc(1, sizeof(GlyphSet)));

  /* init image */
  int width = 128;
  int height = 128;
retry:
  set->image = ren_new_image(width, height);

  /* load glyphs */
  float s =
    stbtt_ScaleForMappingEmToPixels(&font->stbfont, 1) /
    stbtt_ScaleForPixelHeight(&font->stbfont, 1);
  int res = stbtt_BakeFontBitmap(
    font->data, 0, font->size * s, (void*) set->image->pixels,
    width, height, idx * 256, 256, set->glyphs);

  /* retry with a larger image buffer if the buffer wasn't large enough */
  if (res < 0) {
    width *= 2;
    height *= 2;
    ren_free_image(set->image);
    goto retry;
  }

  /* adjust glyph yoffsets and xadvance */
  int ascent, descent, linegap;
  stbtt_GetFontVMetrics(&font->stbfont, &ascent, &descent, &linegap);
  float scale = stbtt_ScaleForMappingEmToPixels(&font->stbfont, font->size);
  int scaled_ascent = ascent * scale + 0.5;
  for (int i = 0; i < 256; i++) {
    set->glyphs[i].yoff += scaled_ascent;
    set->glyphs[i].xadvance = floor(set->glyphs[i].xadvance);
  }

  /* convert 8bit data to 32bit */
  for (int i = width * height - 1; i >= 0; i--) {
    uint8_t n = *((uint8_t*) set->image->pixels + i);
    set->image->pixels[i] = (RenColor) { .r = 255, .g = 255, .b = 255, .a = n };
  }

  return set;
}


static GlyphSet* get_glyphset(RenFont *font, int codepoint) {
  int idx = (codepoint >> 8) % MAX_GLYPHSET;
  if (!font->sets[idx]) {
    font->sets[idx] = load_glyphset(font, idx);
  }
  return font->sets[idx];
}


RenFont* ren_load_font(const char *filename, float size) {
  RenFont *font = NULL;
  FILE *fp = NULL;

  /* init font */
  font = check_alloc(calloc(1, sizeof(RenFont)));
  font->size = size;

  /* load font into buffer */
  fp = fopen(filename, "rb");
  if (!fp) { return NULL; }
  /* get size */
  fseek(fp, 0, SEEK_END); int buf_size = ftell(fp); fseek(fp, 0, SEEK_SET);
  /* load */
  font->data = check_alloc(malloc(buf_size));
  int _ = fread(font->data, 1, buf_size, fp); (void) _;
  fclose(fp);
  fp = NULL;

  /* init stbfont */
  int ok = stbtt_InitFont(&font->stbfont, font->data, 0);
  if (!ok) { goto fail; }

  /* get height and scale */
  int ascent, descent, linegap;
  stbtt_GetFontVMetrics(&font->stbfont, &ascent, &descent, &linegap);
  float scale = stbtt_ScaleForMappingEmToPixels(&font->stbfont, size);
  font->height = (ascent - descent + linegap) * scale + 0.5;

  /* make tab and newline glyphs invisible */
  stbtt_bakedchar *g = get_glyphset(font, '\n')->glyphs;
  g['\t'].x1 = g['\t'].x0;
  g['\n'].x1 = g['\n'].x0;

  return font;

fail:
  if (fp) { fclose(fp); }
  if (font) { free(font->data); }
  free(font);
  return NULL;
}


void ren_free_font(RenFont *font) {
  for (int i = 0; i < MAX_GLYPHSET; i++) {
    GlyphSet *set = font->sets[i];
    if (set) {
      ren_free_image(set->image);
      free(set);
    }
  }

  free(font->data);
  free(font);
}


void ren_set_font_tab_width(RenFont *font, int n) {
  GlyphSet *set = get_glyphset(font, '\t');
  set->glyphs['\t'].xadvance = n;
}


int ren_get_font_tab_width(RenFont *font) {
  GlyphSet *set = get_glyphset(font, '\t');
  return set->glyphs['\t'].xadvance;
}


int ren_get_font_width(RenFont *font, const char *text) {
  int x = 0;
  const char *p = text;
  unsigned codepoint;
  while (*p) {
    p = utf8_to_codepoint(p, &codepoint);
    GlyphSet *set = get_glyphset(font, codepoint);
    stbtt_bakedchar *g = &set->glyphs[codepoint & 0xff];
    x += g->xadvance;
  }
  return x;
}


int ren_get_font_height(RenFont *font) {
  return font->height;
}


static inline RenColor blend_pixel(RenColor dst, RenColor src) {
  int ia = 0xff - src.a;
  dst.r = ((src.r * src.a) + (dst.r * ia)) >> 8;
  dst.g = ((src.g * src.a) + (dst.g * ia)) >> 8;
  dst.b = ((src.b * src.a) + (dst.b * ia)) >> 8;
  return dst;
}


static inline RenColor blend_pixel2(RenColor dst, RenColor src, RenColor color) {
  src.a = (src.a * color.a) >> 8;
  int ia = 0xff - src.a;
  dst.r = ((src.r * color.r * src.a) >> 16) + ((dst.r * ia) >> 8);
  dst.g = ((src.g * color.g * src.a) >> 16) + ((dst.g * ia) >> 8);
  dst.b = ((src.b * color.b * src.a) >> 16) + ((dst.b * ia) >> 8);
  return dst;
}


#define rect_draw_loop(expr)        \
  for (int j = y1; j < y2; j++) {   \
    for (int i = x1; i < x2; i++) { \
      *d = expr;                    \
      d++;                          \
    }                               \
    d += dr;                        \
  }

void ren_draw_rect(RenRect rect, RenColor color) {
  if (color.a == 0) { return; }

  int x1 = rect.x < clip.left ? clip.left : rect.x;
  int y1 = rect.y < clip.top  ? clip.top  : rect.y;
  int x2 = rect.x + rect.width;
  int y2 = rect.y + rect.height;
  x2 = x2 > clip.right  ? clip.right  : x2;
  y2 = y2 > clip.bottom ? clip.bottom : y2;

  RenColor *d = back_buffer->pixels;
  d += x1 + y1 * back_buffer->width;
  int dr = back_buffer->width - (x2 - x1);

  if (color.a == 0xff) {
    rect_draw_loop(color);
  } else {
    rect_draw_loop(blend_pixel(*d, color));
  }
}


void ren_draw_image(RenImage *image, RenRect *sub, int x, int y, RenColor color) {
  if (color.a == 0) { return; }

  /* clip */
  int n;
  if ((n = clip.left - x) > 0) { sub->width  -= n; sub->x += n; x += n; }
  if ((n = clip.top  - y) > 0) { sub->height -= n; sub->y += n; y += n; }
  if ((n = x + sub->width  - clip.right ) > 0) { sub->width  -= n; }
  if ((n = y + sub->height - clip.bottom) > 0) { sub->height -= n; }

  if (sub->width <= 0 || sub->height <= 0) {
    return;
  }

  /* draw */
  RenColor *s = image->pixels;
  RenColor *d = back_buffer->pixels;
  s += sub->x + sub->y * image->width;
  d += x + y * back_buffer->width;
  int sr = image->width - sub->width;
  int dr = back_buffer->width - sub->width;

  for (int j = 0; j < sub->height; j++) {
    for (int i = 0; i < sub->width; i++) {
      *d = blend_pixel2(*d, *s, color);
      d++;
      s++;
    }
    d += dr;
    s += sr;
  }
}


int ren_draw_text(RenFont *font, const char *text, int x, int y, RenColor color) {
  RenRect rect;
  const char *p = text;
  unsigned codepoint;
  while (*p) {
    p = utf8_to_codepoint(p, &codepoint);
    GlyphSet *set = get_glyphset(font, codepoint);
    stbtt_bakedchar *g = &set->glyphs[codepoint & 0xff];
    rect.x = g->x0;
    rect.y = g->y0;
    rect.width = g->x1 - g->x0;
    rect.height = g->y1 - g->y0;
    ren_draw_image(set->image, &rect, x + g->xoff, y + g->yoff, color);
    x += g->xadvance;
  }
  return x;
}
