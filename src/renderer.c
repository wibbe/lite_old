#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>
#include "lib/stb/stb_truetype.h"
#include <SDL2/SDL_ttf.h>
#include "renderer.h"

#define MAX_GLYPHSET 256


typedef struct {
  SDL_Surface * surf;
  int advance;
} Glyph;


typedef struct {
  Glyph glyphs[256];
} GlyphSet;

struct RenFont {
  TTF_Font * font;
  GlyphSet *sets[MAX_GLYPHSET];
  float size;
  int tab_width;
};


static SDL_Window *window;
static struct { int left, top, right, bottom; } clip;


extern double get_scale(void);


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


static void* check_alloc(void *ptr) {
  if (!ptr) {
    fprintf(stderr, "Fatal error: memory allocation failed\n");
    exit(EXIT_FAILURE);
  }
  return ptr;
}


void ren_init(SDL_Window *win) {
  assert(win);
  window = win;
  SDL_Surface *surf = SDL_GetWindowSurface(window);
  ren_set_clip_rect( (RenRect) { 0, 0, surf->w, surf->h } );
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
}


void ren_get_size(int *x, int *y) {
  SDL_Surface *surf = SDL_GetWindowSurface(window);
  *x = surf->w;
  *y = surf->h;
}


static GlyphSet * load_glyphset(RenFont * font, int idx) {
  GlyphSet *set = check_alloc(calloc(1, sizeof(GlyphSet)));

  /* load glyphs */
  for (int i = 0; i < 256; ++i) {
    TTF_GlyphMetrics(font->font, idx * 256 + i, NULL, NULL, NULL, NULL, &set->glyphs[i].advance);
    short ch = idx * 256 + i;
    if (TTF_GlyphIsProvided(font->font, ch)) {
      set->glyphs[i].surf = TTF_RenderGlyph_Blended(font->font, ch, (SDL_Color) { .r = 255, .g = 255, .b = 255, .a = 255 });
    } else {
      set->glyphs[i].surf = NULL;
    }
  }

  return set;
}


static GlyphSet * get_glyphset(RenFont * font, int codepoint) {
  int idx = (codepoint >> 8) % MAX_GLYPHSET;
  if (!font->sets[idx]) {
    font->sets[idx] = load_glyphset(font, idx);
  }
  return font->sets[idx];
}


RenFont* ren_load_font(const char * filename, float size) {
  RenFont *font = NULL;

  TTF_Font * ttf_font = TTF_OpenFont(filename ,size * get_scale());
  if (ttf_font == NULL) {
    return NULL;
  }

  /* init font */
  font = check_alloc(calloc(1, sizeof(RenFont)));
  font->size = size;
  font->font = ttf_font;

  TTF_GlyphMetrics(ttf_font, '\t', NULL, NULL, NULL, NULL, &font->tab_width);

  return font;
}


void ren_free_font(RenFont *font) {
  TTF_CloseFont(font->font);

  for (int i = 0; i < MAX_GLYPHSET; ++i)
  {
    if (font->sets[i] != NULL)
    {
      for (int j = 0; j < 256; ++j)
        SDL_FreeSurface(font->sets[i]->glyphs[j].surf);

      free(font->sets[i]);
    }
  }

  free(font);
}


void ren_set_font_tab_width(RenFont *font, int n) {
  GlyphSet *set = get_glyphset(font, '\t');
  set->glyphs['\t'].advance = n;
}


int ren_get_font_tab_width(RenFont *font) {
  GlyphSet *set = get_glyphset(font, '\t');
  return set->glyphs['\t'].advance;
}


int ren_get_font_width(RenFont *font, const char *text) {
  int x = 0;
  const char *p = text;
  unsigned codepoint;
  while (*p) {
    p = utf8_to_codepoint(p, &codepoint);
    GlyphSet * set = get_glyphset(font, codepoint);
    Glyph * g = &set->glyphs[codepoint & 0xff];
    if (g->surf != NULL)
      if (codepoint == '\t')
        x += g->advance - (x % g->advance);
      else
        x += g->advance;
  }
  return x;
}


int ren_get_font_height(RenFont *font) {
  return TTF_FontHeight(font->font);
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
}


int ren_draw_text(RenFont *font, const char *text, int x, int y, RenColor color) {
  if (color.a == 0) {
    x += ren_get_font_width(font, text);
  } else {
    SDL_Surface *surf = SDL_GetWindowSurface(window);

    SDL_Rect clip_rect = {
      .x = clip.left, .y = clip.top,
      .w = clip.right - clip.left, .h = clip.bottom - clip.top
    };
    SDL_SetClipRect(surf, &clip_rect);

    const char *p = text;
    unsigned codepoint;
    while (*p) {
      p = utf8_to_codepoint(p, &codepoint);
      GlyphSet * set = get_glyphset(font, codepoint);
      Glyph * g = &set->glyphs[codepoint & 0xff];

      if (g->surf != NULL) {
        SDL_Rect target_rect = {
          .x = x, .y = y,
          .w = g->surf->w, .h = g->surf->h
        };

        SDL_SetSurfaceColorMod(g->surf, color.r, color.g, color.b);
        SDL_BlitSurface(g->surf, NULL, surf, &target_rect);

        if (codepoint == '\t')
          x += g->advance - (x % g->advance);
        else
          x += g->advance;
      }
    }

    SDL_SetClipRect(surf, NULL);
  }

  return x;
}
