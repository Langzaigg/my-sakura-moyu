#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <windows.h>

#include <algorithm>
#include <detours.h>
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

#include "../../filelist.h"
#include "common.h"
#include "fvp/reader.h"
#include "overlay/i_d3d9.h"
#include "strsub.h"
#include "subtitle.h"

#define NAME_SIZE 0x200

std::vector<PatchEntry> defaultEntryList;

bool g_bDebugConsole = false;
FILE *g_pLogFile = nullptr;

void DebugLog(const char *format, ...) {
  if (!g_bDebugConsole) return;

  va_list args;
  va_start(args, format);

  va_list args1;
  va_copy(args1, args);
  vprintf(format, args1);
  va_end(args1);

  if (g_pLogFile) {
    vfprintf(g_pLogFile, format, args);
    fflush(g_pLogFile);
  }

  va_end(args);
}

void WaitBeforeExit() {
  if (g_bDebugConsole) {
    printf("\n程序遇到错误或请求退出。按回车键关闭控制台...\n");
    freopen("CONIN$", "r", stdin);
    int c;
    while ((c = getchar()) != '\n' && c != EOF) {
    }
    getchar();
  }
}

void FatalError(HookError err) {
  const char *msg;
  switch (err) {
    case HookError::ErrMissingD3d9:
      msg = "无法载入Direct3D9，请检查是否安装了相应的运行库。";
      break;
    case HookError::ErrTextMapLoadFailed:
      msg = "文本载入失败。";
      break;
    case HookError::ErrHookGlobal:
    case HookError::ErrHookText:
    case HookError::ErrHookImage:
    case HookError::ErrHookSub:
    case HookError::ErrDetourCommit:
      msg =
          "无法挂载函数接口，可能的原因：\n"
          "1. 游戏版本不匹配\n"
          "2. 安全软件（如 360、腾讯管家等）拦截了注入\n"
          "   请尝试关闭安全软件或将游戏目录加入白名单。";
      break;
    default:
      msg = "发生了未知错误。";
      break;
  }
  MessageBoxA(GetDesktopWindow(), msg, "错误", MB_ICONSTOP | MB_OK);
  WaitBeforeExit();
}

void WarnError(HookError err) {
  const char *msg;
  switch (err) {
    case HookError::ErrMissingPatchTsv:
      msg = "缺少 patch.tsv 配置清单文件！\n程序将以无图像补丁模式运行。";
      break;
    default:
      return;
  }
  MessageBoxA(GetDesktopWindow(), msg, "提示", MB_ICONWARNING | MB_OK);
}

bool IsDebugEnabled() {
  int argc = 0;
  LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (!argv) {
    LPCWSTR cmd = GetCommandLineW();
    return (wcsstr(cmd, L"-d") || wcsstr(cmd, L"-debug") ||
            wcsstr(cmd, L"--debug"));
  }

  bool bDebug = false;
  for (int i = 1; i < argc; ++i) {
    if (_wcsicmp(argv[i], L"-d") == 0 || _wcsicmp(argv[i], L"-debug") == 0 ||
        _wcsicmp(argv[i], L"--debug") == 0) {
      bDebug = true;
      break;
    }
  }
  LocalFree(argv);
  return bDebug;
}

void GetModuleNameFromAddress(PVOID address, char *buffer, int size) {
  HMODULE hModule = NULL;
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         (LPCSTR)address, &hModule)) {
    GetModuleFileNameA(hModule, buffer, size);
  } else {
    strcpy_s(buffer, size, "Unknown Module");
  }
}

LONG WINAPI MyUnhandledExceptionFilter(EXCEPTION_POINTERS *ExceptionInfo) {
  char crashMsg[1024];
  char moduleName[NAME_SIZE];

  GetModuleNameFromAddress(ExceptionInfo->ExceptionRecord->ExceptionAddress,
                           moduleName, NAME_SIZE);

  sprintf_s(crashMsg, sizeof(crashMsg),
            "====================================================\n"
            "CRASH REPORT\n"
            "====================================================\n"
            "Exception Code   : 0x%08X\n"
            "Fault Address    : 0x%p\n"
            "Module Name      : %s\n"
            "====================================================\n",
            ExceptionInfo->ExceptionRecord->ExceptionCode,
            ExceptionInfo->ExceptionRecord->ExceptionAddress, moduleName);

  if (g_bDebugConsole) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);

    printf("\n%s", crashMsg);

    if (g_pLogFile) {
      fprintf(g_pLogFile, "%s", crashMsg);
      fflush(g_pLogFile);
    }

    WaitBeforeExit();
  } else {
    FILE *fCrash = nullptr;
    fopen_s(&fCrash, "crash_dump.txt", "a+");
    if (fCrash) {
      SYSTEMTIME st;
      GetLocalTime(&st);
      fprintf(fCrash, "[%04d-%02d-%02d %02d:%02d:%02d] \n", st.wYear, st.wMonth,
              st.wDay, st.wHour, st.wMinute, st.wSecond);
      fprintf(fCrash, "%s\n\n", crashMsg);
      fclose(fCrash);
    }

    MessageBoxA(NULL,
                "程序发生了意外错误并已停止工作。\n请查看游戏目录下的 "
                "crash_dump.txt 获取详细信息。",
                "致命错误 (Fatal Error)", MB_ICONERROR | MB_OK);
  }

  return EXCEPTION_EXECUTE_HANDLER;
}

template <typename TYPE>
inline void FromPtr(TYPE &pFunc, PVOID pRaw) {
  *(PVOID *)(&pFunc) = pRaw;
}

template <typename TYPE>
inline PVOID ToPtr(TYPE pFunc) {
  TYPE pRaw = pFunc;
  return *(PVOID *)(&pRaw);
}

int (*StringMap::pInflateInit)(z_streamp, const char *, int);
int (*StringMap::pInflate)(z_streamp, int);
int (*StringMap::pInflateEnd)(z_streamp);

/** Executable Structures **/
BOOL TestFile(LPCSTR lpPath);

class FileSys {
 public:
  typedef void (FileSys::*PFUNC_Open)(PCSTR pPath);
  static PFUNC_Open pOpen;
  void Open(PCSTR pPath) {
    char pNewPath[NAME_SIZE];
    UINT L, R;
    L = 0;
    R = defaultEntryList.size();
    while (L < R) {
      UINT M;
      int C;
      M = (L + R) >> 1;
      C = strcmp(pPath, defaultEntryList[M].originalName);
      if (C < 0) {
        R = M;
      } else if (C > 0) {
        L = M + 1;
      } else {
        if (!defaultEntryList[M].valid) break;
        if (strcpy_s(pNewPath, "patch/") ||
            strcat_s(pNewPath, defaultEntryList[M].patchName)) {
          break;
        }
        pPath = pNewPath;
        break;
      }
    }
    (this->*pOpen)(pPath);
  }
};
FileSys::PFUNC_Open FileSys::pOpen = NULL;

struct VMARG;
struct VMSTR;
struct VMENV;

class SakuraApp {
 private:
  static DWORD dwAudioSubChannel;
  static PCSTR AudioSub[4];

 public:
  static DWORD dwStringOffset[0x80];

  typedef void (SakuraApp::*FUNC_VOID)();
  static FUNC_VOID pRunStep;
  void RunStep() { (this->*pRunStep)(); }
  typedef void (SakuraApp::*FUNC_NATIVE)(VMARG *);
  static FUNC_NATIVE pSysMovie;
  void SysMovie(VMARG *);
  static FUNC_NATIVE pSysMovieStop;
  void SysMovieStop(VMARG *);
  static FUNC_NATIVE pAudioLoad;
  void AudioLoad(VMARG *);
  static FUNC_NATIVE pAudioPlay;
  void AudioPlay(VMARG *);
  static FUNC_NATIVE pAudioStop;
  void AudioStop(VMARG *);
  static FUNC_NATIVE pTextTest;
  void TextTest(VMARG *);
  inline VMENV *GetEnv();
  inline PVOID GetScript() { return *((PVOID *)this + 1718251); }
  inline VMSTR *GetStringByIndex(int iStrIndex) {
    return (VMSTR *)((PDWORD)this + 1741435 + iStrIndex);
  }
  PCSTR GetStringArg(VMARG *Arg);
};
DWORD SakuraApp::dwAudioSubChannel = 4;
PCSTR SakuraApp::AudioSub[4];
DWORD SakuraApp::dwStringOffset[0x80];
SakuraApp::FUNC_VOID SakuraApp::pRunStep = NULL;
SakuraApp::FUNC_NATIVE SakuraApp::pSysMovie = NULL;
SakuraApp::FUNC_NATIVE SakuraApp::pSysMovieStop = NULL;
SakuraApp::FUNC_NATIVE SakuraApp::pAudioLoad = NULL;
SakuraApp::FUNC_NATIVE SakuraApp::pAudioPlay = NULL;
SakuraApp::FUNC_NATIVE SakuraApp::pAudioStop = NULL;
SakuraApp::FUNC_NATIVE SakuraApp::pTextTest = NULL;

extern BOOL doImagePatch;

struct HEAPBLOCK {
  static const size_t sVmSize0 = 35, sVmSize1 = sVmSize0 + 5;
  static const DWORD dwVmStart = 0x74daa;
  constexpr static const BYTE bVmBytes[] =
      "\x02\x50\x7e\x03\x00"
      "\x0b\xfa\x00\x0e\x08"
      "warning\x00\x0c\x05\x08"
      "\x02\xa5\x73\x03\x00"  // sub_000373a5(250, "patch/warning", 5, 0b)
      "\x0b\xb8\x0b\x03\x89\x00"
      "\x03\x85\x00"
      "\x06\x00\x00\x00\x00";
  typedef void (HEAPBLOCK::*PFUNC_ReadFile)(PCSTR pPath);
  static PFUNC_ReadFile pReadFile;
  void ReadFile(PCSTR pPath) {
    (this->*pReadFile)(pPath);
    if (!strcmp(pPath, "Sakura.hcb") && doImagePatch) {
      DWORD dwScriptSize;
      dwScriptSize = dwSize;
      (this->*pSetSize)(dwSize + sVmSize1);
      memcpy((PBYTE)pBuf + dwScriptSize, bVmBytes, sVmSize1);
      *((PBYTE)pBuf + dwVmStart) = 0x06;
      *(PDWORD)((PBYTE)pBuf + dwVmStart + 1) = dwScriptSize;
      *(PDWORD)((PBYTE)pBuf + dwScriptSize + sVmSize0 + 1) = dwVmStart + 5;
    }
  }
  typedef void (HEAPBLOCK::*PFUNC_SetSize)(DWORD dwSize_);
  static PFUNC_SetSize pSetSize;

  void *pBuf;
  DWORD dwSize;
};
HEAPBLOCK::PFUNC_ReadFile HEAPBLOCK::pReadFile = NULL;
HEAPBLOCK::PFUNC_SetSize HEAPBLOCK::pSetSize = NULL;

struct VMARG {
  BYTE bType, bPad0[3];
  DWORD dwData;
};

struct VMSTR {
 public:
  typedef void (VMSTR::*PFUNC_SetString)(PCSTR pString);
  static PFUNC_SetString pSetString;
};
VMSTR::PFUNC_SetString VMSTR::pSetString = NULL;

WCHAR hTextBufW[0x400];
CHAR hTextBuf[0x800];
struct VMENV {
  SakuraApp *pApp;
  PVOID pScript;
  VMARG pStack[0x100];
  DWORD dwFlags;
  DWORD dwWaitTimeLeft;
  BYTE bYield, bPad0[3];
  VMARG argReturnValue;
  DWORD dwPtrCode;
  DWORD dwPtrStack;
  DWORD dwPtrFrame;
  WORD wRefCount0[0x80];
  WORD wRefCount1[0x100];

 public:
  static StringMap *pSubMap;
  typedef int (VMENV::*PFUNC_GetIndex)();
  static PFUNC_GetIndex pGetIndex;
  typedef void (VMENV::*PFUNC_LoadText)();
  static PFUNC_LoadText pLoadText;
  void LoadText() {
    (this->*pLoadText)();
    VMARG &ArgTop = this->pStack[this->dwPtrStack - 1];
    if (pSubMap) {
      StringMap::ENTRY *pEntry;
      pEntry = pSubMap->GetEntry(ArgTop.dwData);
      if (pEntry) {
        PCSTR pOrgText;
        int iStrIndex;
        VMSTR *pVmStr;
        pOrgText = (PCSTR)this->pScript + ArgTop.dwData;
        iStrIndex = (this->*pGetIndex)();
        SakuraApp::dwStringOffset[iStrIndex] = pEntry->dwOffset;
        pVmStr = this->pApp->GetStringByIndex(iStrIndex);

        if (pEntry->pText) {
          (pVmStr->*(VMSTR::pSetString))(pEntry->pText);

          if (g_bDebugConsole) {
            bool bIsBlank = true;

            if (pOrgText) {
              for (int i = 0; pOrgText[i] != '\0'; i++) {
                if ((unsigned char)pOrgText[i] > 0x20) {
                  bIsBlank = false;
                  break;
                }
              }
            }

            if (bIsBlank && pEntry->pText) {
              for (int i = 0; pEntry->pText[i] != '\0'; i++) {
                if ((unsigned char)pEntry->pText[i] > 0x20) {
                  bIsBlank = false;
                  break;
                }
              }
            }

            if (!bIsBlank) {
              DebugLog("Text ID:%6d\n", pEntry->dwIndex);

              MultiByteToWideChar(932, 0, pOrgText, -1, hTextBufW, 0x400);
              WideCharToMultiByte(65001, 0, hTextBufW, -1, hTextBuf, 0x800,
                                  NULL, NULL);
              DebugLog("%s\n", hTextBuf);

              MultiByteToWideChar(936, 0, pEntry->pText, -1, hTextBufW, 0x400);
              WideCharToMultiByte(65001, 0, hTextBufW, -1, hTextBuf, 0x800,
                                  NULL, NULL);
              DebugLog("%s\n\n", hTextBuf);
            }
          }

        } else {
          int Size;
          Size = MultiByteToWideChar(932, 0, pOrgText, -1, hTextBufW, 0x400);
          if (Size) {
            Size = WideCharToMultiByte(936, 0, hTextBufW, -1, hTextBuf, 0x800,
                                       NULL, NULL);
            if (Size) {
              (pVmStr->*(VMSTR::pSetString))(hTextBuf);
            } else {
              (pVmStr->*(VMSTR::pSetString))("<无法转换文本>");
            }
          } else {
            (pVmStr->*(VMSTR::pSetString))("<无法转换文本>");
          }
        }
        ArgTop.bType = 5;
        ArgTop.dwData = iStrIndex;
      }
    }
  }
};
StringMap *VMENV::pSubMap = NULL;
VMENV::PFUNC_GetIndex VMENV::pGetIndex = NULL;
VMENV::PFUNC_LoadText VMENV::pLoadText = NULL;

VMENV *SakuraApp::GetEnv() {
  return (VMENV *)((PDWORD)this + 1718256) + *((PDWORD)this + 1743284);
}

PCSTR SakuraApp::GetStringArg(VMARG *Arg) {
  if (Arg->bType == 4) {
    return (PCSTR)GetEnv()->pScript + Arg->dwData;
  } else if (Arg->bType == 5) {
    return *(PCSTR *)GetStringByIndex(Arg->dwData);
  } else {
    return NULL;
  }
}

/** Native VM Functions **/
void SakuraApp::SysMovie(VMARG *Arg) {
  PCSTR pMoviePath;
  pMoviePath = GetStringArg(Arg);
  DebugLog("Movie Load: %s\n", pMoviePath ? pMoviePath : "NULL");
  (this->*pSysMovie)(Arg);
  if (pMoviePath) {
    PCSTR pAssSubPath;
    if (!strcmp(pMoviePath, "movie/01.wmv")) {
      pAssSubPath = PATH_SUB VD1_NAME;
      GameApp.AttachSub(new Subtitle((char *)pAssSubPath));
    } else if (!strcmp(pMoviePath, "movie/02.wmv")) {
      pAssSubPath = PATH_SUB VD2_NAME;
      GameApp.AttachSub(new Subtitle((char *)pAssSubPath));
    }
  }
}
void SakuraApp::SysMovieStop(VMARG *Arg) {
  DebugLog("Movie Stop\n");
  (this->*pSysMovieStop)(Arg);
  GameApp.DetachSub();
}
void SakuraApp::AudioLoad(VMARG *Arg) {
  PCSTR pAudioPath;
  (this->*pAudioLoad)(Arg);
  if (Arg->bType == 2 && 0 <= Arg->dwData && Arg->dwData < 4) {
    pAudioPath = GetStringArg(Arg + 1);
    DebugLog("Audio Load: %d %s\n", Arg->dwData,
             pAudioPath ? pAudioPath : "NULL");
    if (pAudioPath) {
      if (!strcmp(pAudioPath, "BGM/073")) {  // ED1
        AudioSub[Arg->dwData] = PATH_SUB ED1_NAME;
      } else if (!strcmp(pAudioPath, "BGM/074")) {  // ED2
        AudioSub[Arg->dwData] = PATH_SUB ED2_NAME;
      } else {
        AudioSub[Arg->dwData] = NULL;
      }
    }
  }
}
void SakuraApp::AudioPlay(VMARG *Arg) {
  (this->*pAudioPlay)(Arg);
  if (0 <= Arg->dwData && Arg->dwData < 4) {
    DebugLog("Audio Play: %d\n", Arg->dwData);
    if (AudioSub[Arg->dwData]) {
      dwAudioSubChannel = Arg->dwData;
      GameApp.AttachSub(new Subtitle((char *)AudioSub[Arg->dwData]));
    }
  }
}
void SakuraApp::AudioStop(VMARG *Arg) {
  (this->*pAudioStop)(Arg);
  if (0 <= Arg->dwData && Arg->dwData < 4) {
    DebugLog("Audio Stop: %d\n", Arg->dwData);
    if (AudioSub[Arg->dwData] && Arg->dwData == dwAudioSubChannel) {
      GameApp.DetachSub();
      dwAudioSubChannel = 4;
    }
  }
}
void SakuraApp::TextTest(VMARG *Arg) {
  // TODO: Hook the free function as well.
  // A bit more checking would make this more robust.
  // Assuming that the VM would not test unrelated entries.

  if (Arg->bType == 5 && Arg->dwData < 0x80) {
    VMARG OriArg;
    OriArg.bType = 4;
    OriArg.dwData = dwStringOffset[Arg->dwData];
    (this->*pTextTest)(&OriArg);
  } else {
    (this->*pTextTest)(Arg);
  }
}

/** Patch File List Loader **/
HookError LoadFileList() {
  char exePath[NAME_SIZE], tsvPath[NAME_SIZE];
  if (!GetModuleFileNameA(NULL, exePath, NAME_SIZE))
    return HookError::ErrMissingPatchTsv;

  char *sep = strrchr(exePath, '\\');
  if (!sep) sep = strrchr(exePath, '/');
  if (sep)
    *(sep + 1) = '\0';
  else
    exePath[0] = '\0';

  if (strcpy_s(tsvPath, exePath) || strcat_s(tsvPath, "patch.tsv"))
    return HookError::ErrMissingPatchTsv;

  std::ifstream file(tsvPath);
  if (!file.is_open()) return HookError::ErrMissingPatchTsv;

  std::string line;
  if (!std::getline(file, line)) return HookError::ErrMissingPatchTsv;

  if (line.size() >= 3 && (unsigned char)line[0] == 0xEF &&
      (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF) {
    line = line.substr(3);
  }

  std::map<std::string, int> headerMap;
  std::stringstream ss(line);
  std::string cell;
  int index = 0;
  while (std::getline(ss, cell, '\t')) {
    cell.erase(cell.find_last_not_of(" \n\r\t") + 1);
    headerMap[cell] = index++;
  }

  const std::vector<std::string> required = {"originalName", "patchName",
                                             "method", "offsetX", "offsetY"};
  for (const auto &req : required) {
    if (headerMap.find(req) == headerMap.end()) {
      DebugLog("Error: Missing required header [%s] in patch.tsv\n",
               req.c_str());
      return HookError::ErrMissingPatchTsv;
    }
  }

  defaultEntryList.clear();
  int lineNum = 2;
  while (std::getline(file, line)) {
    ++lineNum;
    if (line.empty() || line[0] == '\r' || line[0] == '\n') continue;

    std::stringstream dataStream(line);
    std::vector<std::string> row;
    while (std::getline(dataStream, cell, '\t')) {
      cell.erase(cell.find_last_not_of(" \n\r\t") + 1);
      row.push_back(cell);
    }

    if (row.size() < headerMap.size()) continue;

    try {
      PatchEntry entry{};

      strncpy_s(entry.method, row[headerMap["method"]].c_str(), _TRUNCATE);
      strncpy_s(entry.originalName, row[headerMap["originalName"]].c_str(),
                _TRUNCATE);
      strncpy_s(entry.patchName, row[headerMap["patchName"]].c_str(),
                _TRUNCATE);

      entry.offsetX = std::stoi(row[headerMap["offsetX"]]);
      entry.offsetY = std::stoi(row[headerMap["offsetY"]]);

      if (!strcmp(entry.method, "R"))
        entry.methodType = METHOD_REPL;
      else if (!strcmp(entry.method, "P"))
        entry.methodType = METHOD_DIFF;
      else if (!strcmp(entry.method, "A"))
        entry.methodType = METHOD_APPEND;
      else
        continue;

      defaultEntryList.push_back(entry);
    } catch (...) {
      DebugLog("Warning: Skipping malformed line %d in patch.tsv\n", lineNum);
      continue;
    }
  }

  file.close();

  /* FileSys::Open uses binary search, list must be sorted by originalName */
  std::sort(defaultEntryList.begin(), defaultEntryList.end(),
            [](const PatchEntry &a, const PatchEntry &b) {
              return strcmp(a.originalName, b.originalName) < 0;
            });

  return HookError::Success;
}

/** Global Environment **/
HMODULE hSakuraExe;
BOOL doTextPatch, doImagePatch, doSubPatch;
CHAR lpDllPath[NAME_SIZE];
HMODULE hDirect3D9Library;
BOOL g_hasSaveDir;

/** Global Functions **/
typedef int(WINAPI *PFUNC_MessageBoxA)(HWND hWnd, LPCSTR lpText,
                                       LPCSTR lpCaption, UINT uType);
typedef HWND(WINAPI *PFUNC_CreateWindowExA)(DWORD dwExStyle, LPCSTR lpClassName,
                                            LPCSTR lpWindowName, DWORD dwStyle,
                                            int X, int Y, int nWidth,
                                            int nHeight, HWND hWndParent,
                                            HMENU hMenu, HINSTANCE hInstance,
                                            LPVOID lpParam);
typedef INT(WINAPI *PFUNC_StrcmpIA)(LPCSTR lpString1, LPCSTR lpString2);
typedef INT(CDECL *PFUNC_IsLeadByte)(UINT Char);
typedef HANDLE(WINAPI *PFUNC_CreateFileA)(LPCSTR, DWORD, DWORD,
                                         LPSECURITY_ATTRIBUTES, DWORD, DWORD,
                                         HANDLE);
typedef HANDLE(WINAPI *PFUNC_CreateFileW)(LPCWSTR, DWORD, DWORD,
                                          LPSECURITY_ATTRIBUTES, DWORD, DWORD,
                                          HANDLE);
typedef BOOL(WINAPI *PFUNC_CreateDirectoryA)(LPCSTR, LPSECURITY_ATTRIBUTES);
typedef BOOL(WINAPI *PFUNC_CreateDirectoryW)(LPCWSTR, LPSECURITY_ATTRIBUTES);

PFUNC_MessageBoxA pMessageBoxA;
PFUNC_CreateWindowExA pCreateWindowExA;
PFUNC_StrcmpIA pStrcmpIA;
PFUNC_IsLeadByte pIsLeadByte;
PFUNC_CreateFileA pCreateFileA;
PFUNC_CreateFileW pCreateFileW;
PFUNC_CreateDirectoryA pCreateDirectoryA;
PFUNC_CreateDirectoryW pCreateDirectoryW;

DWORD PatchDoubleWord(PVOID pAddr, DWORD dwValue) {
  DWORD dwOldProtect, dwOldValue;

  VirtualProtect(pAddr, 4, PAGE_EXECUTE_READWRITE, &dwOldProtect);
  dwOldValue = *(PDWORD)pAddr;
  *(PDWORD)pAddr = dwValue;
  VirtualProtect(pAddr, 4, dwOldProtect, &dwOldProtect);
  return dwOldValue;
}

void InitGlobal() {
  HMODULE hUser32, hKernel32;
  hUser32 = GetModuleHandleW(L"USER32");
  hKernel32 = GetModuleHandleW(L"KERNEL32");
  pMessageBoxA = (PFUNC_MessageBoxA)GetProcAddress(hUser32, "MessageBoxA");
  pCreateWindowExA =
      (PFUNC_CreateWindowExA)GetProcAddress(hUser32, "CreateWindowExA");
  pStrcmpIA = (PFUNC_StrcmpIA)GetProcAddress(hKernel32, "lstrcmpiA");
  pIsLeadByte = (PFUNC_IsLeadByte)0x44844f;
  pCreateFileA = (PFUNC_CreateFileA)GetProcAddress(hKernel32, "CreateFileA");
  pCreateFileW = (PFUNC_CreateFileW)GetProcAddress(hKernel32, "CreateFileW");
  pCreateDirectoryA =
      (PFUNC_CreateDirectoryA)GetProcAddress(hKernel32, "CreateDirectoryA");
  pCreateDirectoryW =
      (PFUNC_CreateDirectoryW)GetProcAddress(hKernel32, "CreateDirectoryW");
  FromPtr(HEAPBLOCK::pReadFile, (PVOID)0x4344c0);
  FromPtr(HEAPBLOCK::pSetSize, (PVOID)0x434060);

  FromPtr(StringMap::pInflateInit, (PVOID)0x401cd0);
  FromPtr(StringMap::pInflate, (PVOID)0x401de0);
  FromPtr(StringMap::pInflateEnd, (PVOID)0x4033c0);
  FromPtr(FileSys::pOpen, (PVOID)0x4422d0);
  FromPtr(VMSTR::pSetString, (PVOID)0x433c60);
  FromPtr(VMENV::pGetIndex, (PVOID)0x4445e0);
  FromPtr(VMENV::pLoadText, (PVOID)0x445b50);
  FromPtr(SakuraApp::pRunStep, (PVOID)0x43f570);
  FromPtr(SakuraApp::pSysMovie, (PVOID)0x43b120);
  FromPtr(SakuraApp::pSysMovieStop, (PVOID)0x43b300);
  FromPtr(SakuraApp::pAudioLoad, (PVOID)0x439c80);
  FromPtr(SakuraApp::pAudioPlay, (PVOID)0x439ce0);
  FromPtr(SakuraApp::pAudioStop, (PVOID)0x439dc0);
  FromPtr(SakuraApp::pTextTest, (PVOID)0x437a60);
  return;
}

int WINAPI MessageBoxA_(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption,
                        UINT uType) {
  int size = MultiByteToWideChar(932, 0, lpText, -1, NULL, 0);
  if (!size) return (*pMessageBoxA)(hWnd, lpText, lpCaption, uType);

  std::vector<WCHAR> textW(size);
  size = MultiByteToWideChar(932, 0, lpText, -1, textW.data(), size);
  if (!size) return (*pMessageBoxA)(hWnd, lpText, lpCaption, uType);

  size = MultiByteToWideChar(932, 0, lpCaption, -1, NULL, 0);
  if (!size) return (*pMessageBoxA)(hWnd, lpText, lpCaption, uType);

  std::vector<WCHAR> captionW(size);
  size = MultiByteToWideChar(932, 0, lpCaption, -1, captionW.data(), size);
  if (!size) return (*pMessageBoxA)(hWnd, lpText, lpCaption, uType);

  return MessageBoxW(hWnd, textW.data(), captionW.data(), uType);
}

HWND WINAPI CreateWindowExA_(DWORD dwExStyle, LPCSTR lpClassName,
                             LPCSTR lpWindowName, DWORD dwStyle, int X, int Y,
                             int nWidth, int nHeight, HWND hWndParent,
                             HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam) {
  if (strstr(lpWindowName,
             "\x82\xb3\x82\xad\x82\xe7\x81\x41\x82\xe0\x82\xe4"
             "\x81\x42 -as the Night's, Reincarnation-")) {
    WCHAR lpWindowNameW[NAME_SIZE], lpClassNameW[NAME_SIZE];
    MultiByteToWideChar(932, 0, lpClassName, -1, lpClassNameW, NAME_SIZE);
    if (VMENV::pSubMap) {
      wsprintfW(lpWindowNameW,
                L"樱花、萌放 -as the Night's, Reincarnation- 文本版本-"
                L"%4hd/%02hd/%02hd-%02hd",
                VMENV::pSubMap->Time.wYear, VMENV::pSubMap->Time.wMonth,
                VMENV::pSubMap->Time.wDay, VMENV::pSubMap->Time.wHour);
    } else {
      wcscpy_s(lpWindowNameW, L"樱花、萌放 -as the Night's, Reincarnation-");
    }
    return CreateWindowExW(dwExStyle, lpClassNameW, lpWindowNameW, dwStyle, X,
                           Y, nWidth, nHeight, hWndParent, hMenu, hInstance,
                           lpParam);
  } else {
    return (*pCreateWindowExA)(dwExStyle, lpClassName, lpWindowName, dwStyle, X,
                               Y, nWidth, nHeight, hWndParent, hMenu, hInstance,
                               lpParam);
  }
}

INT WINAPI StrcmpIA_(LPCSTR lpString1, LPCSTR lpString2) {
  return CompareStringA(0x411, NORM_IGNORECASE, lpString1, -1, lpString2, -1) -
         2;
}

int IsLeadByte(UINT Char) {
  if (doTextPatch) {
    return (0x80 < Char && Char < 0xff);
  } else {
    return (0x80 < Char && Char < 0xa0) || (0xe0 <= Char && Char < 0xef);
  }
}

static BOOL IsSavePath(LPCWSTR lpPath) {
  if (!lpPath || !g_hasSaveDir) return FALSE;
  return wcsstr(lpPath,
      L"FAVORITE\\\u3055\u304F\u3089\u3001\u3082\u3086\u3002\\save") != NULL ||
      wcsstr(lpPath,
      L"FAVORITE/\u3055\u304F\u3089\u3001\u3082\u3086\u3002/save") != NULL;
}

static void RewriteSavePath(LPCWSTR lpSrc, LPWSTR lpDst, DWORD cchDst) {
  static const wchar_t patFile[] =
      L"FAVORITE\\\u3055\u304F\u3089\u3001\u3082\u3086\u3002\\save\\";
  static const wchar_t patDir[] =
      L"FAVORITE\\\u3055\u304F\u3089\u3001\u3082\u3086\u3002\\save";
  static const int patFileLen = (sizeof(patFile) / sizeof(wchar_t)) - 1;
  static const wchar_t patFile2[] =
      L"FAVORITE/\u3055\u304F\u3089\u3001\u3082\u3086\u3002/save/";
  static const wchar_t patDir2[] =
      L"FAVORITE/\u3055\u304F\u3089\u3001\u3082\u3086\u3002/save";

  LPCWSTR pos = wcsstr(lpSrc, patFile);
  if (!pos) pos = wcsstr(lpSrc, patFile2);
  if (pos) {
    wcscpy_s(lpDst, cchDst, L"save\\");
    wcscat_s(lpDst, cchDst, pos + patFileLen);
    return;
  }

  pos = wcsstr(lpSrc, patDir);
  if (!pos) pos = wcsstr(lpSrc, patDir2);
  if (pos) {
    wcscpy_s(lpDst, cchDst, L"save");
    return;
  }

  wcscpy_s(lpDst, cchDst, lpSrc);
}

static void EnsureParentDir(LPCWSTR lpPath) {
  WCHAR dir[NAME_SIZE];
  wcscpy_s(dir, lpPath);
  wchar_t *sep = wcsrchr(dir, L'\\');
  if (!sep) sep = wcsrchr(dir, L'/');
  if (!sep) return;
  *sep = L'\0';
  EnsureParentDir(dir);
  pCreateDirectoryW(dir, NULL);
}

HANDLE WINAPI CreateFileW_(LPCWSTR lpFileName, DWORD dwDesiredAccess,
                           DWORD dwShareMode,
                           LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                           DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes,
                           HANDLE hTemplateFile) {
  if (IsSavePath(lpFileName)) {
    WCHAR newPath[NAME_SIZE];
    RewriteSavePath(lpFileName, newPath, NAME_SIZE);
    if (dwCreationDisposition == CREATE_NEW ||
        dwCreationDisposition == CREATE_ALWAYS ||
        dwCreationDisposition == OPEN_ALWAYS) {
      EnsureParentDir(newPath);
    }
    return pCreateFileW(newPath, dwDesiredAccess, dwShareMode,
                        lpSecurityAttributes, dwCreationDisposition,
                        dwFlagsAndAttributes, hTemplateFile);
  }
  return pCreateFileW(lpFileName, dwDesiredAccess, dwShareMode,
                      lpSecurityAttributes, dwCreationDisposition,
                      dwFlagsAndAttributes, hTemplateFile);
}

BOOL WINAPI CreateDirectoryW_(LPCWSTR lpPathName,
                              LPSECURITY_ATTRIBUTES lpSecurityAttributes) {
  if (IsSavePath(lpPathName)) {
    WCHAR newPath[NAME_SIZE];
    RewriteSavePath(lpPathName, newPath, NAME_SIZE);
    EnsureParentDir(newPath);
    return pCreateDirectoryW(newPath, lpSecurityAttributes);
  }
  return pCreateDirectoryW(lpPathName, lpSecurityAttributes);
}

static BOOL ACPathToWide(LPCSTR lpSrc, LPWSTR lpDst, DWORD cchDst) {
  if (!lpSrc) return FALSE;
  static const UINT cpList[] = {CP_ACP, 932, 936};
  for (int i = 0; i < 3; i++) {
    if (MultiByteToWideChar(cpList[i], 0, lpSrc, -1, lpDst, (int)cchDst) > 0)
      return TRUE;
  }
  return FALSE;
}

HANDLE WINAPI CreateFileA_(LPCSTR lpFileName, DWORD dwDesiredAccess,
                           DWORD dwShareMode,
                           LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                           DWORD dwCreationDisposition,
                           DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
  WCHAR pathW[NAME_SIZE];
  if (ACPathToWide(lpFileName, pathW, NAME_SIZE) && IsSavePath(pathW)) {
    WCHAR newPath[NAME_SIZE];
    RewriteSavePath(pathW, newPath, NAME_SIZE);
    if (dwCreationDisposition == CREATE_NEW ||
        dwCreationDisposition == CREATE_ALWAYS ||
        dwCreationDisposition == OPEN_ALWAYS) {
      EnsureParentDir(newPath);
    }
    return pCreateFileW(newPath, dwDesiredAccess, dwShareMode,
                        lpSecurityAttributes, dwCreationDisposition,
                        dwFlagsAndAttributes, hTemplateFile);
  }
  return pCreateFileA(lpFileName, dwDesiredAccess, dwShareMode,
                      lpSecurityAttributes, dwCreationDisposition,
                      dwFlagsAndAttributes, hTemplateFile);
}

BOOL WINAPI CreateDirectoryA_(LPCSTR lpPathName,
                              LPSECURITY_ATTRIBUTES lpSecurityAttributes) {
  WCHAR pathW[NAME_SIZE];
  if (ACPathToWide(lpPathName, pathW, NAME_SIZE) && IsSavePath(pathW)) {
    WCHAR newPath[NAME_SIZE];
    RewriteSavePath(pathW, newPath, NAME_SIZE);
    EnsureParentDir(newPath);
    return pCreateDirectoryW(newPath, lpSecurityAttributes);
  }
  return pCreateDirectoryA(lpPathName, lpSecurityAttributes);
}

HookError AttachGlobal() {
  if (DetourAttach(&((PVOID &)pMessageBoxA), ToPtr(&MessageBoxA_)) != NO_ERROR)
    return HookError::ErrHookGlobal;
  if (DetourAttach(&((PVOID &)pCreateWindowExA), ToPtr(&CreateWindowExA_)) !=
      NO_ERROR)
    return HookError::ErrHookGlobal;
  if (DetourAttach(&((PVOID &)pStrcmpIA), ToPtr(&StrcmpIA_)) != NO_ERROR)
    return HookError::ErrHookGlobal;
  if (DetourAttach(&((PVOID &)pIsLeadByte), ToPtr(&IsLeadByte)) != NO_ERROR)
    return HookError::ErrHookGlobal;
  if (DetourAttach(&((PVOID &)HEAPBLOCK::pReadFile),
                   ToPtr(&HEAPBLOCK::ReadFile)) != NO_ERROR)
    return HookError::ErrHookGlobal;
  if (DetourAttach(&((PVOID &)pCreateFileA), ToPtr(&CreateFileA_)) != NO_ERROR)
    return HookError::ErrHookGlobal;
  if (DetourAttach(&((PVOID &)pCreateFileW), ToPtr(&CreateFileW_)) != NO_ERROR)
    return HookError::ErrHookGlobal;
  if (DetourAttach(&((PVOID &)pCreateDirectoryA), ToPtr(&CreateDirectoryA_)) !=
      NO_ERROR)
    return HookError::ErrHookGlobal;
  if (DetourAttach(&((PVOID &)pCreateDirectoryW), ToPtr(&CreateDirectoryW_)) !=
      NO_ERROR)
    return HookError::ErrHookGlobal;
  return HookError::Success;
}
void DetachGlobal() {
  DetourDetach(&((PVOID &)HEAPBLOCK::pReadFile), ToPtr(&HEAPBLOCK::ReadFile));
  DetourDetach(&((PVOID &)pIsLeadByte), ToPtr(&IsLeadByte));
  DetourDetach(&((PVOID &)pStrcmpIA), ToPtr(&StrcmpIA_));
  DetourDetach(&((PVOID &)pCreateWindowExA), ToPtr(&CreateWindowExA_));
  DetourDetach(&((PVOID &)pMessageBoxA), ToPtr(&MessageBoxA_));
  DetourDetach(&((PVOID &)pCreateFileA), ToPtr(&CreateFileA_));
  DetourDetach(&((PVOID &)pCreateFileW), ToPtr(&CreateFileW_));
  DetourDetach(&((PVOID &)pCreateDirectoryA), ToPtr(&CreateDirectoryA_));
  DetourDetach(&((PVOID &)pCreateDirectoryW), ToPtr(&CreateDirectoryW_));
}

static DWORD dwCharSet, pFaceName;
HookError AttachTextSub() {
  VMENV::pSubMap = new StringMap(L"" PATH_TEXT);
  if (!VMENV::pSubMap || !VMENV::pSubMap->Exist())
    return HookError::ErrTextMapLoadFailed;

  dwCharSet = PatchDoubleWord((PVOID)0x444364, GB2312_CHARSET);
  PatchDoubleWord((PVOID)0x42d22e, GB2312_CHARSET);
  pFaceName = PatchDoubleWord((PVOID)0x443b3a, (DWORD) "CHINESE_GB2312");
  if (DetourAttach(&((PVOID &)VMENV::pLoadText), ToPtr(&VMENV::LoadText)) !=
      NO_ERROR)
    return HookError::ErrHookText;
  if (DetourAttach(&((PVOID &)SakuraApp::pTextTest),
                   ToPtr(&SakuraApp::TextTest)) != NO_ERROR)
    return HookError::ErrHookText;
  return HookError::Success;
}
void DetachTextSub() {
  PatchDoubleWord((PVOID)0x443b3a, pFaceName);
  PatchDoubleWord((PVOID)0x42d22e, dwCharSet);
  PatchDoubleWord((PVOID)0x444364, dwCharSet);
  DetourDetach(&((PVOID &)SakuraApp::pTextTest), ToPtr(&SakuraApp::TextTest));
  DetourDetach(&((PVOID &)VMENV::pLoadText), ToPtr(&VMENV::LoadText));
  if (VMENV::pSubMap) {
    delete VMENV::pSubMap;
    VMENV::pSubMap = NULL;
  }
}

HookError AttachImage() {
  if (DetourAttach(&((PVOID &)FileSys::pOpen), ToPtr(&FileSys::Open)) !=
      NO_ERROR)
    return HookError::ErrHookImage;
  return HookError::Success;
}
void DetachImage() {
  DetourDetach(&((PVOID &)FileSys::pOpen), ToPtr(&FileSys::Open));
}

HookError AttachSub() {
  if (!pDirect3DCreate9) return HookError::ErrMissingD3d9;

  if (DetourAttach(&((PVOID &)pDirect3DCreate9), ToPtr(&OverlayD3DCreate)) !=
      NO_ERROR)
    return HookError::ErrHookSub;
  if (DetourAttach(&((PVOID &)SakuraApp::pSysMovie),
                   ToPtr(&SakuraApp::SysMovie)) != NO_ERROR)
    return HookError::ErrHookSub;
  if (DetourAttach(&((PVOID &)SakuraApp::pSysMovieStop),
                   ToPtr(&SakuraApp::SysMovieStop)) != NO_ERROR)
    return HookError::ErrHookSub;
  if (DetourAttach(&((PVOID &)SakuraApp::pAudioLoad),
                   ToPtr(&SakuraApp::AudioLoad)) != NO_ERROR)
    return HookError::ErrHookSub;
  if (DetourAttach(&((PVOID &)SakuraApp::pAudioPlay),
                   ToPtr(&SakuraApp::AudioPlay)) != NO_ERROR)
    return HookError::ErrHookSub;
  if (DetourAttach(&((PVOID &)SakuraApp::pAudioStop),
                   ToPtr(&SakuraApp::AudioStop)) != NO_ERROR)
    return HookError::ErrHookSub;
  return HookError::Success;
}
void DetachSub() {
  DetourDetach(&((PVOID &)SakuraApp::pAudioStop), ToPtr(&SakuraApp::AudioStop));
  DetourDetach(&((PVOID &)SakuraApp::pAudioPlay), ToPtr(&SakuraApp::AudioPlay));
  DetourDetach(&((PVOID &)SakuraApp::pAudioLoad), ToPtr(&SakuraApp::AudioLoad));
  DetourDetach(&((PVOID &)SakuraApp::pSysMovieStop),
               ToPtr(&SakuraApp::SysMovieStop));
  DetourDetach(&((PVOID &)SakuraApp::pSysMovie), ToPtr(&SakuraApp::SysMovie));
  DetourDetach(&((PVOID &)pDirect3DCreate9), ToPtr(&OverlayD3DCreate));
}

BOOL TestFile(LPCSTR lpPath) {
  HANDLE hTestFile;

  hTestFile = CreateFileA(lpPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hTestFile != INVALID_HANDLE_VALUE) {
    CloseHandle(hTestFile);
    return TRUE;
  }
  return FALSE;
}

static HookError DoAttachAll() {
  HookError err;

  err = AttachGlobal();
  if (err != HookError::Success) return err;

  if (doTextPatch) {
    err = AttachTextSub();
    if (err != HookError::Success) return err;
  }
  if (doImagePatch) {
    err = AttachImage();
    if (err != HookError::Success) return err;
  }
  if (doSubPatch) {
    err = AttachSub();
    if (err != HookError::Success) return err;
  }
  return HookError::Success;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ulReason, LPVOID lpReserved) {
  UNREFERENCED_PARAMETER(lpReserved);
  if (DetourIsHelperProcess()) {
    return TRUE;
  }
  hSakuraExe = GetModuleHandleW(L"" PATH_EXEC);
  if (hSakuraExe == NULL) {
    hSakuraExe = GetModuleHandleW(L"SakuraF.exe");
  }
  if (hSakuraExe == NULL) {
    GetModuleFileNameA(hModule, lpDllPath, NAME_SIZE);
    return TRUE;
  }
  switch (ulReason) {
    case DLL_PROCESS_ATTACH: {
      SetUnhandledExceptionFilter(MyUnhandledExceptionFilter);

      if (IsDebugEnabled()) {
        g_bDebugConsole = true;

        fopen_s(&g_pLogFile, "sakura_debug.log", "w");

        if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
          AllocConsole();
        }
        SetConsoleOutputCP(65001);
        SetConsoleCP(65001);

        FILE *fp;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        freopen_s(&fp, "CONIN$", "r", stdin);

        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);

        DebugLog("Console and File Log Initialized.\n");
      }

      DebugLog("Loading File List (patch.tsv)...\n");
      {
        HookError err = LoadFileList();
        if (err != HookError::Success) {
          WarnError(err);
          DebugLog("Warning: patch.tsv not loaded (%d)\n", (int)err);
        }
      }

      DebugLog("Validating patch resources...\n");
      {
        int validCount = 0;

        for (auto &entry : defaultEntryList) {
          entry.valid = false;
          char patchPath[NAME_SIZE];
          if (strcpy_s(patchPath, "patch/") ||
              strcat_s(patchPath, entry.patchName))
            continue;
          if (TestFile(patchPath)) {
            entry.valid = true;
            validCount++;
          }
        }

        FvpReader reader;
        if (TestFile(PATH_IMAGE)) {
          if (reader.open(PATH_IMAGE) == 0) {
            for (auto &entry : defaultEntryList) {
              if (!entry.valid && reader.hasFile(entry.patchName)) {
                entry.valid = true;
                validCount++;
              }
            }
            reader.close();
          }
        }

        for (const auto &entry : defaultEntryList) {
          if (!entry.valid) {
            DebugLog("Warning: patch resource missing for %s (patch/%s)\n",
                     entry.originalName, entry.patchName);
          }
        }

        DebugLog("Patch resource validation: %d/%d valid\n", validCount,
                 (int)defaultEntryList.size());
        doImagePatch = (validCount > 0);
      }

      doTextPatch = TestFile(PATH_TEXT);
      doSubPatch = TestFile(PATH_SUB VD1_NAME) && TestFile(PATH_SUB VD2_NAME) &&
                   TestFile(PATH_SUB ED1_NAME) && TestFile(PATH_SUB ED2_NAME);

      DebugLog("Configuration: Text %d, Image %d, Subtitle: %d\n", doTextPatch,
               doImagePatch, doSubPatch);

      if (doSubPatch) {
        DebugLog("Loading d3d9.dll...\n");
        hDirect3D9Library = LoadLibraryA("d3d9.dll");
        if (!hDirect3D9Library) {
          FatalError(HookError::ErrMissingD3d9);
          return FALSE;
        }
        pDirect3DCreate9 = (PFUNC_Direct3DCreate9)GetProcAddress(
            hDirect3D9Library, "Direct3DCreate9");
        DebugLog("Direct3DCreate9 ProcAddress: %p\n", pDirect3DCreate9);
      } else {
        hDirect3D9Library = NULL;
        pDirect3DCreate9 = NULL;
      }

      {
        DWORD attr = GetFileAttributesA("save");
        g_hasSaveDir =
            attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
        DebugLog("Save directory %s\n", g_hasSaveDir ? "found" : "not found");
      }

      DebugLog("Initializing Subtitles & Globals...\n");
      SubtitleInit();
      InitGlobal();

      DetourRestoreAfterWith();
      DetourTransactionBegin();
      DetourUpdateThread(GetCurrentThread());

      DebugLog("Attaching hooks...\n");
      {
        HookError err = DoAttachAll();
        if (err != HookError::Success) {
          DetourTransactionAbort();
          FatalError(err);
          return FALSE;
        }
      }

      DebugLog("Committing Detour transaction...\n");
      if (DetourTransactionCommit() != NO_ERROR) {
        FatalError(HookError::ErrDetourCommit);
        return FALSE;
      }
      DebugLog("Initialization Complete. Launching game...\n");
      break;
    }
    case DLL_PROCESS_DETACH:
      DetourTransactionBegin();
      DetourUpdateThread(GetCurrentThread());
      if (doSubPatch) {
        DetachSub();
      }
      if (doImagePatch) {
        DetachImage();
      }
      if (doTextPatch) {
        DetachTextSub();
      }

      DetachGlobal();
      DetourTransactionCommit();
      SubtitleFini();
      if (hDirect3D9Library) {
        FreeLibrary(hDirect3D9Library);
      }

      if (g_bDebugConsole) {
        DebugLog("Exiting process gracefully.\n");
        if (g_pLogFile) {
          fclose(g_pLogFile);
          g_pLogFile = nullptr;
        }
        FreeConsole();
      }
      break;
    default:
      break;
  }
  return TRUE;
}

EXTERN_C
HookError CDECL StartExecutable() {
  STARTUPINFOA siSakura;
  PROCESS_INFORMATION piSakura;

  ZeroMemory(&siSakura, sizeof(siSakura));
  ZeroMemory(&piSakura, sizeof(piSakura));
  siSakura.cb = sizeof(siSakura);
  siSakura.dwFlags = STARTF_USESHOWWINDOW;
  siSakura.wShowWindow = SW_SHOW;

  char szCmdLine[NAME_SIZE * 2];
  sprintf_s(szCmdLine, sizeof(szCmdLine), "\"%s\"", PATH_EXEC);

  if (IsDebugEnabled()) {
    strcat_s(szCmdLine, sizeof(szCmdLine), " --debug");
  }

  if (!DetourCreateProcessWithDllExA(PATH_EXEC, szCmdLine, NULL, NULL, FALSE,
                                     CREATE_DEFAULT_ERROR_MODE, NULL, NULL,
                                     &siSakura, &piSakura, lpDllPath, NULL)) {
    CloseHandle(&siSakura);
    CloseHandle(&piSakura);
    return HookError::ErrProcessCreate;
  }
  CloseHandle(&siSakura);
  CloseHandle(&piSakura);
  return HookError::Success;
}
