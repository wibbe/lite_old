#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <math.h>
#include "lib/stb/stb_truetype.h"
#include <SDL2/SDL_ttf.h>
#include "renderer.h"

#define MAX_GLYPHSET 256


typedef struct {
  SDL_Texture *texture;
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
static SDL_Renderer *renderer;
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
  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

  int width, height;
  SDL_GetRendererOutputSize(renderer, &width, &height);

  ren_set_clip_rect( (RenRect) { 0, 0, width, height } );
}

void ren_shutdown(void) {
  SDL_DestroyRenderer(renderer);
  renderer = NULL;
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

  SDL_Rect r = {
    .x = rect.x,
    .y = rect.y,
    .w = rect.width,
    .h = rect.height,
  };

  SDL_RenderSetClipRect(renderer, &r);
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
      SDL_Surface *surf = TTF_RenderGlyph_Blended(font->font, ch, (SDL_Color) { .r = 255, .g = 255, .b = 255, .a = 255 });
      set->glyphs[i].texture = SDL_CreateTextureFromSurface(renderer, surf);
      SDL_FreeSurface(surf);
    } else {
      set->glyphs[i].texture = NULL;
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

  printf("Loading font %s\n", filename);

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
        SDL_DestroyTexture(font->sets[i]->glyphs[j].texture);
        
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
    if (g->texture != NULL)
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


void ren_begin_frame(void) {
  SDL_RenderClear(renderer);
}


void ren_end_frame(void) {
  SDL_RenderPresent(renderer);
}


void ren_draw_rect(RenRect rect, RenColor color) {
  if (color.a == 0) { return; }

  SDL_Rect r = {
    .x = rect.x,
    .y = rect.y,
    .w = rect.width,
    .h = rect.height,
  };

  if (color.a == 0xff) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_RenderFillRect(renderer, &r);
  } else {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderFillRect(renderer, &r);
  }
}


int ren_draw_text(RenFont *font, const char *text, int x, int y, RenColor color) {
  if (color.a == 0) {
    x += ren_get_font_width(font, text);
  } else {
    SDL_Rect clip_rect = {
      .x = clip.left, .y = clip.top,
      .w = clip.right - clip.left, .h = clip.bottom - clip.top
    };
    SDL_RenderSetClipRect(renderer, &clip_rect);

    const char *p = text;
    unsigned codepoint;
    while (*p) {
      p = utf8_to_codepoint(p, &codepoint);
      GlyphSet * set = get_glyphset(font, codepoint);
      Glyph * g = &set->glyphs[codepoint & 0xff];

      if (g->texture != NULL) {
        int tex_w, tex_h;
        SDL_QueryTexture(g->texture, NULL, NULL, &tex_w, &tex_h);


        SDL_Rect target_rect = {
          .x = x, .y = y,
          .w = tex_w, .h = tex_h
        };

        SDL_SetTextureColorMod(g->texture, color.r, color.g, color.b);
        SDL_RenderCopy(renderer, g->texture, NULL, &target_rect);

        if (codepoint == '\t')
          x += g->advance - (x % g->advance);
        else
          x += g->advance;
      }
    }
  }

  return x;
}
