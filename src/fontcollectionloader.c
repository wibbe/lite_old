
#include "fontcollectionloader.h"
#include <stdio.h>
#include <dirent.h>

extern IDWriteFactory * write_factory;

extern void get_exe_filename(char *buf, int sz);
extern wchar_t * to_wstr(const char * in, int * text_length);

static GUID IDWriteFontCollectionLoader_GUID = { 0xcca920e4, 0x52f0, 0x492b, { 0xbf, 0xa8, 0x29, 0xc7, 0x2e, 0xe0, 0xa4, 0x68 } };
static GUID IDWriteFontFileEnumerator_GUID = { 0x72755049, 0x5ff7, 0x435d, { 0x83, 0x48, 0x4b, 0xe9, 0x7c, 0xfa, 0x6c, 0x7c } };


ULONG STDMETHODCALLTYPE IFontCollectionLoader_AddRef(IDWriteFontCollectionLoader * this);
ULONG STDMETHODCALLTYPE IFontCollectionLoader_Release(IDWriteFontCollectionLoader * this);
HRESULT STDMETHODCALLTYPE IFontCollectionLoader_QueryInterface(IDWriteFontCollectionLoader * this, REFIID vTableGuid, void ** ppv);
HRESULT STDMETHODCALLTYPE IFontCollectionLoader_CreateEnumeratorFromKey(IDWriteFontCollectionLoader * this, IDWriteFactory * factory, const void * collection_key, UINT32  collection_key_size, IDWriteFontFileEnumerator ** font_file_enumerator);

ULONG STDMETHODCALLTYPE IFontFileEnumerator_AddRef(IDWriteFontFileEnumerator * this);
ULONG STDMETHODCALLTYPE IFontFileEnumerator_Release(IDWriteFontFileEnumerator * this);
HRESULT STDMETHODCALLTYPE IFontFileEnumerator_QueryInterface(IDWriteFontFileEnumerator * this, REFIID vTableGuid, void ** ppv);
HRESULT STDMETHODCALLTYPE IFontFileEnumerator_MoveNext(IDWriteFontFileEnumerator * this, BOOL * hasCurrentFile);
HRESULT STDMETHODCALLTYPE IFontFileEnumerator_GetCurrentFontFile(IDWriteFontFileEnumerator * this, IDWriteFontFile ** fontFile);


HRESULT IFontCollectionLoaderCreate(IDWriteFontCollectionLoader ** font_collection_loader) {
  *font_collection_loader = NULL;

  IFontCollectionLoader * loader = malloc(sizeof(IFontCollectionLoader));
  memset(loader, 0, sizeof(IFontFileEnumerator));
  loader->font_len = 0;

  loader->base.lpVtbl = malloc(sizeof(IDWriteFontCollectionLoaderVtbl));
  loader->base.lpVtbl->AddRef = &IFontCollectionLoader_AddRef;
  loader->base.lpVtbl->Release = &IFontCollectionLoader_Release;
  loader->base.lpVtbl->QueryInterface = &IFontCollectionLoader_QueryInterface;
  loader->base.lpVtbl->CreateEnumeratorFromKey = &IFontCollectionLoader_CreateEnumeratorFromKey;

  // Enumerate all the fonts
  char exe_path[MAX_PATH];
  get_exe_filename(exe_path, MAX_PATH);

  *strstr(exe_path, "lite.exe") = '\0';
  strcat(exe_path, "data\\fonts");

  DIR * dir = opendir(exe_path);
  if (dir) {
    int i = 1;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
      if (strstr(entry->d_name, ".ttf") == NULL)
        continue;

      char font_filename[MAX_PATH];
      memcpy(font_filename, exe_path, MAX_PATH);
      strcat(font_filename, "\\");
      strcat(font_filename, entry->d_name);

      loader->fonts[loader->font_len] = to_wstr(font_filename, NULL);
      printf("Found font %s\n", font_filename);

      loader->font_len++;
    }

    closedir(dir);
  } else {
    printf("No fonts present\n");
  }

  *font_collection_loader = (IDWriteFontCollectionLoader *)loader;
  IFontCollectionLoader_AddRef(*font_collection_loader);
  return S_OK;
}

ULONG STDMETHODCALLTYPE IFontCollectionLoader_AddRef(IDWriteFontCollectionLoader * this) {
  IFontCollectionLoader * self = (IFontCollectionLoader *)this;
  self->count++;
  return self->count;
}


ULONG STDMETHODCALLTYPE IFontCollectionLoader_Release(IDWriteFontCollectionLoader * this) {
  IFontCollectionLoader * self = (IFontCollectionLoader *)this;

  self->count--;
  DWORD count = self->count;

  if (count == 0)
  {
    for (int i = 0; i < self->font_len; i++)
      free(self->fonts[i]);

    free(self->base.lpVtbl);
    free(self);
  }

  return count;
}

HRESULT STDMETHODCALLTYPE IFontCollectionLoader_QueryInterface(IDWriteFontCollectionLoader * this, REFIID iid, void ** ppv_object) {
  printf("IFontCollectionLoader_QueryInterface()\n");

  if (IsEqualIID(iid, &IDWriteFontCollectionLoader_GUID))
  {
    *ppv_object = this;
    IFontCollectionLoader_AddRef(this);
    return S_OK;
  }
  else
  {
    *ppv_object = NULL;
    return E_NOINTERFACE;
  }
}

HRESULT STDMETHODCALLTYPE IFontCollectionLoader_CreateEnumeratorFromKey(IDWriteFontCollectionLoader * this,
                                                                        IDWriteFactory * factory,
                                                                        const void * collection_key,
                                                                        UINT32 collection_key_size,
                                                                        IDWriteFontFileEnumerator ** font_file_enumerator) {
  IFontCollectionLoader * self = (IFontCollectionLoader *)this;
  HRESULT hr = S_OK;
  *font_file_enumerator = NULL;


  IFontFileEnumerator * enumerator = malloc(sizeof(IFontFileEnumerator));
  memset(enumerator, 0, sizeof(IFontFileEnumerator));
  enumerator->collection_key = collection_key;
  enumerator->collection_key_size = collection_key_size;
  enumerator->index = 0;
  enumerator->font_collection = self;
  IFontCollectionLoader_AddRef(this);

  enumerator->base.lpVtbl = malloc(sizeof(IDWriteFontFileEnumeratorVtbl));
  enumerator->base.lpVtbl->AddRef = &IFontFileEnumerator_AddRef;
  enumerator->base.lpVtbl->Release = &IFontFileEnumerator_Release;
  enumerator->base.lpVtbl->QueryInterface = &IFontFileEnumerator_QueryInterface;
  enumerator->base.lpVtbl->MoveNext = &IFontFileEnumerator_MoveNext;
  enumerator->base.lpVtbl->GetCurrentFontFile = &IFontFileEnumerator_GetCurrentFontFile;

  *font_file_enumerator = (IDWriteFontFileEnumerator *)enumerator;
  IFontFileEnumerator_AddRef(*font_file_enumerator);

  return hr;
}




ULONG STDMETHODCALLTYPE IFontFileEnumerator_AddRef(IDWriteFontFileEnumerator * this) {
  IFontFileEnumerator * self = (IFontFileEnumerator *)this;
  self->count++;
  return self->count;
}

ULONG STDMETHODCALLTYPE IFontFileEnumerator_Release(IDWriteFontFileEnumerator * this) {
  IFontFileEnumerator * self = (IFontFileEnumerator *)this;

  self->count--;
  DWORD count = self->count;

  if (count == 0)
  {
    free(self->base.lpVtbl);
    free(self);
  }

  return count;
}

HRESULT STDMETHODCALLTYPE IFontFileEnumerator_QueryInterface(IDWriteFontFileEnumerator * this, REFIID iid, void ** ppv_object) {

  if (IsEqualIID(iid, &IDWriteFontFileEnumerator_GUID))
  {
    *ppv_object = this;
    IFontFileEnumerator_AddRef(this);
    return S_OK;
  }
  else
  {
    *ppv_object = NULL;
    return E_NOINTERFACE;
  }
}

HRESULT STDMETHODCALLTYPE IFontFileEnumerator_MoveNext(IDWriteFontFileEnumerator * this, BOOL * has_current_file) {
  IFontFileEnumerator * self = (IFontFileEnumerator *)this;
  HRESULT result = S_OK;

  *has_current_file = FALSE;

  if (self->current_file != NULL)
  {
    IDWriteFontFile_Release(self->current_file);
    self->current_file = NULL;
  }

  if (self->index < self->font_collection->font_len)
  {
    result = IDWriteFactory_CreateFontFileReference(write_factory, self->font_collection->fonts[self->index], NULL, &self->current_file);

    if (SUCCEEDED(result)) {
      *has_current_file = TRUE;
      self->index++;
    }
  }

  return result;
}

HRESULT STDMETHODCALLTYPE IFontFileEnumerator_GetCurrentFontFile(IDWriteFontFileEnumerator * this, IDWriteFontFile ** font_file) {
  IFontFileEnumerator * self = (IFontFileEnumerator *)this;

  if (self->current_file != NULL) {
    IDWriteFontFile_AddRef(self->current_file);
    *font_file = self->current_file;
  } else {
    *font_file = NULL;
  }

  return self->current_file == NULL ? E_FAIL : S_OK;
}


