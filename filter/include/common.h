#ifndef FILTER_COMMON_H
#define FILTER_COMMON_H

/* External Files */
#define PATH_EXEC "Sakura.exe"
#define PATH_TEXT "patch.dat"
#define PATH_IMAGE "patch.bin"
#define PATH_SUB "subtitle/"
#define PATH_FONT "subtitle/fonts"
#define VD1_NAME "vd1.ass"
#define VD2_NAME "vd2.ass"
#define ED1_NAME "ed1.ass"
#define ED2_NAME "ed2.ass"

enum class HookError {
    Success,
    ErrMissingPatchTsv,
    ErrMissingD3d9,
    ErrTextMapLoadFailed,
    ErrHookGlobal,
    ErrHookText,
    ErrHookImage,
    ErrHookSub,
    ErrDetourCommit,
    ErrProcessCreate,
};

inline const char* HookErrorToString(HookError err) {
    switch (err) {
        case HookError::Success: return "Success";
        case HookError::ErrMissingPatchTsv: return "Missing patch.tsv";
        case HookError::ErrMissingD3d9: return "Missing d3d9.dll";
        case HookError::ErrTextMapLoadFailed: return "Text map load failed";
        case HookError::ErrHookGlobal: return "Global hook attach failed";
        case HookError::ErrHookText: return "Text hook attach failed";
        case HookError::ErrHookImage: return "Image hook attach failed";
        case HookError::ErrHookSub: return "Subtitle hook attach failed";
        case HookError::ErrDetourCommit: return "Detour transaction commit failed";
        case HookError::ErrProcessCreate: return "Process creation failed";
    }
    return "Unknown error";
}

#endif  // FILTER_COMMON_H
