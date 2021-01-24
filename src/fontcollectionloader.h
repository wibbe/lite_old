#ifndef FONT_COLLECTION_H
#define FONT_COLLECTION_H

#define D2D_USE_C_DEFINITIONS
#define D2D1_INIT_GUID
#define COBJMACROS
#define CINTERFACE
#define WIND32_MEAN_AND_LEAN

#include <windows.h>
#include <windowsx.h>
#include "dwrite.h"


typedef struct IFontCollectionLoader {
  IDWriteFontCollectionLoader base;
  DWORD count;

  wchar_t * fonts[128];
  int font_len;

} IFontCollectionLoader;

typedef struct IFontFileEnumerator {
  IDWriteFontFileEnumerator base;
  DWORD count;
  const void * collection_key;
  UINT32 collection_key_size;

  IFontCollectionLoader * font_collection;
  IDWriteFontFile * current_file;
  int index;
} IFontFileEnumerator;


HRESULT IFontCollectionLoaderCreate(IDWriteFontCollectionLoader ** font_collection_loader);

#endif

