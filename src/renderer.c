#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>

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

#include "lib/stb/stb_truetype.h"
#include "renderer.h"
#include "fontcollectionloader.h"

#define MAX_GLYPHSET 256

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

  IDWriteTextFormat * text_format;
};

extern HWND hwnd;
extern ID2D1Factory * d2d_factory;
extern IDWriteFactory * write_factory;
static ID2D1HwndRenderTarget * render_target = NULL;
static IDWriteFontCollectionLoader * font_collection_loader = NULL;
static IDWriteFontCollection * font_collection = NULL;
static UINT font_collection_key = 0xABCDEF;

static int can_paint = 0;
static int has_clip_rect = 0;

static SDL_Window *window;
static struct { int left, top, right, bottom; } clip;


extern wchar_t * to_wstr(const char * in, int * text_length);
extern float to_dips_size(float points);


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

static D2D1_MATRIX_3X2_F identity_matrix(void)
{
   D2D1_MATRIX_3X2_F mat = {
      1.0f, 0.0f,
      0.0f, 1.0f,
      0.0f, 0.0f,
   };
   return mat;
}

static HRESULT create_device_resources(void)
{
   HRESULT result = S_OK;

   if (!render_target) {
      RECT rect;
      GetClientRect(hwnd, &rect);

      D2D1_SIZE_U size = {
         .width = rect.right - rect.left,
         .height = rect.bottom - rect.top,
      };

      D2D1_RENDER_TARGET_PROPERTIES render_target_properties = {
         .type = D2D1_RENDER_TARGET_TYPE_DEFAULT,
         .pixelFormat = { .format = DXGI_FORMAT_UNKNOWN, .alphaMode = D2D1_ALPHA_MODE_UNKNOWN },
         .dpiX = 0.0f,
         .dpiY = 0.0f,
         .usage = D2D1_RENDER_TARGET_USAGE_NONE,
         .minLevel = D2D1_FEATURE_LEVEL_DEFAULT,
      };

      D2D1_HWND_RENDER_TARGET_PROPERTIES hwnd_render_target_properties = {
         .hwnd = hwnd,
         .pixelSize = size,
         .presentOptions = D2D1_PRESENT_OPTIONS_NONE,
      };

      printf("RenderTarget: %dx%d\n", size.width, size.height);

      result = ID2D1Factory_CreateHwndRenderTarget(d2d_factory, &render_target_properties, &hwnd_render_target_properties, &render_target);
      if (FAILED(result))
      {
         printf("Could not create HWND render target\n");
      }

      has_clip_rect = 0;
   }

   return result;
}

static void discard_device_resources(void) {
  if (render_target)
    ID2D1HwndRenderTarget_Release(render_target);
  render_target = NULL;
}


void ren_init(SDL_Window *win) {
  assert(win);
  window = win;
  SDL_Surface *surf = SDL_GetWindowSurface(window);
  ren_set_clip_rect( (RenRect) { 0, 0, surf->w, surf->h } );

  IFontCollectionLoaderCreate(&font_collection_loader);
  IDWriteFactory_RegisterFontCollectionLoader(write_factory, font_collection_loader);

  printf("Creating custom font collection...");
  HRESULT result = IDWriteFactory_CreateCustomFontCollection(write_factory, font_collection_loader, &font_collection_key, sizeof(font_collection_key), &font_collection);
  if (FAILED(result))
    printf("failed\n");
  else
    printf("done\n");
}

void ren_close(void) {
  IDWriteFontCollectionLoader_Release(font_collection_loader);
  font_collection_loader = NULL;
}

void ren_resize(int width, int height) {
   if (render_target) {
      D2D1_SIZE_U size = {
         .width = width,
         .height = height,
      };

      HRESULT result = ID2D1HwndRenderTarget_Resize(render_target, &size);
      if (FAILED(result))
         printf("Could not resize window!\n");
   }
}

void ren_begin_frame(void) {
  HRESULT result = create_device_resources();

  if (SUCCEEDED(result) && render_target) {
    can_paint = 1;
    ID2D1HwndRenderTarget_BeginDraw(render_target);
  } else {
    can_paint = 0;
  }
}

void ren_end_frame(void) {
  if (can_paint)
  {
     if (has_clip_rect) {
      ID2D1RenderTarget_PopAxisAlignedClip((ID2D1RenderTarget *)render_target);
      has_clip_rect = 0;
    }

    HRESULT result = ID2D1HwndRenderTarget_EndDraw(render_target, NULL, NULL);
    if (FAILED(result)) {
      printf("Error while painting app\n");
      ID2D1HwndRenderTarget_Release(render_target);
      render_target = NULL;
    }
  }

  can_paint = 0;
}

void ren_update_rects(RenRect *rects, int count) {
  SDL_UpdateWindowSurfaceRects(window, (SDL_Rect*) rects, count);
  static bool initial_frame = true;
  if (initial_frame) {
    SDL_ShowWindow(window);
    initial_frame = false;
  }
}


void ren_set_clip_rect(RenRect rect) {
  clip.left   = rect.x;
  clip.top    = rect.y;
  clip.right  = rect.x + rect.width;
  clip.bottom = rect.y + rect.height;

  if (render_target && can_paint) {
    if (has_clip_rect) {
      ID2D1RenderTarget_PopAxisAlignedClip((ID2D1RenderTarget *)render_target);
      has_clip_rect = 0;
    }

    D2D1_RECT_F clip_rect = {
      .left   = rect.x,
      .top    = rect.y,
      .right  = rect.x + rect.width,
      .bottom = rect.y + rect.height,
    };
    ID2D1RenderTarget_PushAxisAlignedClip((ID2D1RenderTarget *)render_target, &clip_rect, D2D1_ANTIALIAS_MODE_ALIASED);
    has_clip_rect = 1;
  }
}


void ren_get_size(int *x, int *y) {
  SDL_Surface *surf = SDL_GetWindowSurface(window);
  *x = surf->w;
  *y = surf->h;

  if (render_target) {
    D2D1_SIZE_F size = ID2D1HwndRenderTarget_GetSize(render_target);
    *x = size.width;
    *y = size.height;
  }
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

  printf("Loading font %s\n", filename);
  wchar_t * font_filename = to_wstr(filename, NULL);

  IDWriteFontFile * font_file[1];
  HRESULT result = IDWriteFactory_CreateFontFileReference(write_factory, font_filename, NULL, &font_file[0]);
  free(font_filename);
  if (FAILED(result)) {
    printf("Could not load font file\n");
    return 0;
  }

  IDWriteFontFace * font_face;
  result = IDWriteFactory_CreateFontFace(write_factory, DWRITE_FONT_FACE_TYPE_TRUETYPE, 1, font_file, 0, DWRITE_FONT_SIMULATIONS_NONE, &font_face);
  IDWriteFontFile_Release(font_file[0]);
  if (FAILED(result)) {
    printf("Could not create font face\n");
    return 0;
  }

  IDWriteFont * dwrite_font;
  result = IDWriteFontCollection_GetFontFromFontFace(font_collection, font_face, &dwrite_font);
  if (FAILED(result)) {
    printf("Could not get font from font face\n");
    IDWriteFontFace_Release(font_face);
    return 0;
  }

  IDWriteFontFamily * font_family;
  result = IDWriteFont_GetFontFamily(dwrite_font, &font_family);
  if (FAILED(result)) {
    printf("Could not get font family\n");
    IDWriteFontFamily_Release(font_family);
    IDWriteFont_Release(dwrite_font);
    IDWriteFontFace_Release(font_face);
    return 0;
  }


  IDWriteLocalizedStrings * family_names;
  result = IDWriteFontFamily_GetFamilyNames(font_family, &family_names);
  if (FAILED(result)) {
    printf("Could not get face names\n");
    IDWriteLocalizedStrings_Release(family_names);
    IDWriteFontFamily_Release(font_family);
    IDWriteFont_Release(dwrite_font);
    IDWriteFontFace_Release(font_face);
    return 0;
  }

  // Use the first family name in the string. (This might not work for every font)
  UINT32 index = 0;
  UINT32 length = 0;
  IDWriteLocalizedStrings_GetStringLength(family_names, index, &length);

  wchar_t * family_name = malloc(sizeof(wchar_t) * (length + 1));
  IDWriteLocalizedStrings_GetString(family_names, index, family_name, length + 1);
  wprintf(L"Family Name: %s\n", family_name);

  IDWriteTextFormat * text_format;
  result = IDWriteFactory_CreateTextFormat(write_factory,
                                           family_name,
                                           font_collection,
                                           DWRITE_FONT_WEIGHT_NORMAL,
                                           DWRITE_FONT_STYLE_NORMAL,
                                           DWRITE_FONT_STRETCH_NORMAL,
                                           //to_dips_size(size),
                                           size,
                                           L"en-us",
                                           &text_format);
  free(family_name);

  if (FAILED(result)) {
    printf("Could not create text format object\n");
    IDWriteLocalizedStrings_Release(family_names);
    IDWriteFontFamily_Release(font_family);
    IDWriteFont_Release(dwrite_font);
    IDWriteFontFace_Release(font_face);
    return 0;
  }

  IDWriteTextFormat_SetTextAlignment(text_format, DWRITE_TEXT_ALIGNMENT_LEADING);
  IDWriteTextFormat_SetParagraphAlignment(text_format, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

  //free(family_name);
  IDWriteLocalizedStrings_Release(family_names);
  IDWriteFontFamily_Release(font_family);
  IDWriteFont_Release(dwrite_font);
  IDWriteFontFace_Release(font_face);

  /* init font */
  font = check_alloc(calloc(1, sizeof(RenFont)));
  font->size = size;
  font->text_format = text_format;

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

  if (font->text_format != NULL) {
    IDWriteTextFormat_Release(font->text_format);
    font->text_format = NULL;
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

  SDL_Surface *surf = SDL_GetWindowSurface(window);
  RenColor *d = (RenColor*) surf->pixels;
  d += x1 + y1 * surf->w;
  int dr = surf->w - (x2 - x1);

  if (color.a == 0xff) {
    rect_draw_loop(color);
  } else {
    rect_draw_loop(blend_pixel(*d, color));
  }

  if (can_paint) {
    D2D1_COLOR_F d2d_color = {
      .r = color.r / 255.0f,
      .g = color.g / 255.0f,
      .b = color.b / 255.0f,
      .a = color.a / 255.0f,
    };
    D2D1_BRUSH_PROPERTIES props = {
      .opacity = color.a,
      .transform = identity_matrix(),
    };

    ID2D1SolidColorBrush * solid_brush = NULL;
    HRESULT result = ID2D1HwndRenderTarget_CreateSolidColorBrush(render_target, &d2d_color, &props, &solid_brush);
    if (SUCCEEDED(result))
    {
      D2D1_RECT_F d2d_rect = {
        .left   = rect.x,
        .top    = rect.y,
        .right  = rect.x + rect.width,
        .bottom = rect.y + rect.height,
      };
      ID2D1HwndRenderTarget_FillRectangle(render_target, &d2d_rect, (ID2D1Brush *)solid_brush);
      ID2D1Brush_Release((ID2D1Brush *)solid_brush);
    }
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
  SDL_Surface *surf = SDL_GetWindowSurface(window);
  RenColor *s = image->pixels;
  RenColor *d = (RenColor*) surf->pixels;
  s += sub->x + sub->y * image->width;
  d += x + y * surf->w;
  int sr = image->width - sub->width;
  int dr = surf->w - sub->width;

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

  if (can_paint) {
    int str_len;
    wchar_t * str = to_wstr(text, &str_len);
    D2D1_RECT_F d2d_rect = {
      .left   = x,
      .top    = y,
      .right  = x + 1000,
      .bottom = y + 1000,
    };

    D2D1_COLOR_F d2d_color = {
      .r = color.r / 255.0f,
      .g = color.g / 255.0f,
      .b = color.b / 255.0f,
      .a = color.a / 255.0f,
    };
    D2D1_BRUSH_PROPERTIES props = {
      .opacity = color.a,
      .transform = identity_matrix(),
    };

    ID2D1SolidColorBrush * solid_brush = NULL;
    HRESULT result = ID2D1HwndRenderTarget_CreateSolidColorBrush(render_target, &d2d_color, &props, &solid_brush);
    if (SUCCEEDED(result))
    {
      ID2D1HwndRenderTarget_DrawText(render_target, str, str_len, font->text_format, &d2d_rect, (ID2D1Brush *)solid_brush, D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
      ID2D1Brush_Release((ID2D1Brush *)solid_brush);
    }



    /*
   app->renderTarget->DrawText(text, len,
                               app->fonts[font].format,
                               toD2DRect(bounds),
                               app->brushes[brush].brush);
    */
  }

  return x;
}
