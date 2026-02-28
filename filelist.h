#ifndef COMMON_FILELIST_H
#define COMMON_FILELIST_H

#include <vector>

enum MethodType {
  METHOD_REPL = 0,
  METHOD_DIFF = 1,
  METHOD_APPEND = 2,
};

struct PatchEntry {
  char method[5];
  MethodType methodType;
  char originalName[33];
  char patchName[33];
  int offsetX, offsetY;
#ifdef FPTOOL
  FvpPackage::FileEntry *fileEntryPtr;
#else
  void *aux;
#endif
};

extern std::vector<PatchEntry> defaultEntryList;

#endif // COMMON_FILELIST_H
