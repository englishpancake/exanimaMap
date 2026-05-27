#include "LoadPNG.h"
#include <windows.h>

static wchar_t szMapBuffer[128];

// Returns the exe's directory with trailing backslash, cached after first call.
static const wchar_t* ExeDir() {
    static wchar_t dir[MAX_PATH] = {};
    if (dir[0]) return dir;
    GetModuleFileNameW(nullptr, dir, MAX_PATH);
    wchar_t* last = wcsrchr(dir, L'\\');
    if (last) *(last + 1) = L'\0';
    return dir;
}

const wchar_t* ConfigPath() {
    static wchar_t path[MAX_PATH] = {};
    if (path[0]) return path;
    wcsncpy_s(path, MAX_PATH, ExeDir(), _TRUNCATE);
    wcsncat_s(path, MAX_PATH, L"assets\\config.ini", _TRUNCATE);
    return path;
}

// Returns the saves\ directory path (no trailing backslash), exe-relative, cached.
const wchar_t* SavesDir() {
    static wchar_t path[MAX_PATH] = {};
    if (path[0]) return path;
    wcsncpy_s(path, MAX_PATH, ExeDir(), _TRUNCATE);
    wcsncat_s(path, MAX_PATH, L"routes", _TRUNCATE);
    return path;
}

// ── Per-level cache — avoids hitting the INI file on every frame ─────────────

struct LevelMapData { int offsetX, offsetY; float scale; };
static LevelMapData s_cache[32] = {};
static bool         s_valid[32] = {};

static const LevelMapData& levelData(BYTE level) {
    if (level < 32 && !s_valid[level]) {
        const wchar_t* ini = ConfigPath();
        wchar_t key[32];
        auto makeKey = [&](const wchar_t* suffix) {
            wsprintfW(key, L"mapid_%d_%s", (int)level, suffix);
            return key;
        };
        s_cache[level].offsetX = GetPrivateProfileIntW(L"Offsets", makeKey(L"offsetx"), 0, ini);
        s_cache[level].offsetY = GetPrivateProfileIntW(L"Offsets", makeKey(L"offsety"), 0, ini);
        wchar_t buf[32] = {};
        GetPrivateProfileStringW(L"Offsets", makeKey(L"scalexy"), L"0.0786", buf, 32, ini);
        s_cache[level].scale = wcstof(buf, nullptr);
        if (s_cache[level].scale <= 0.f) s_cache[level].scale = 0.0786f;
        s_valid[level] = true;
    }
    return s_cache[level < 32 ? level : 0];
}

void ClearMapCache() {
    memset(s_valid, 0, sizeof(s_valid));
}

// ── Public API ────────────────────────────────────────────────────────────────

const wchar_t* getMap(BYTE level) {
    if (level < 2) return L"assets\\title.png";
    wchar_t key[32];
    wsprintfW(key, L"mapid_%d_location", (int)level);
    GetPrivateProfileStringW(L"Maps", key, nullptr, szMapBuffer, 128, ConfigPath());
    return szMapBuffer;
}

int getMapOffsetX(BYTE level) { return level < 2 ? 0 : levelData(level).offsetX; }
int getMapOffsetY(BYTE level) { return level < 2 ? 0 : levelData(level).offsetY; }
float getMapScale(BYTE level) { return level < 2 ? 0.0786f : levelData(level).scale; }

BOOL isToFullscreen() {
    return GetPrivateProfileIntW(L"AppSettings", L"full_window_screen", 0, ConfigPath()) == 1;
}

// Returns the Exanima saves path from config, or empty if not set.
std::wstring getPathExanima() {
    wchar_t buf[512] = {};
    GetPrivateProfileStringW(L"AppSettings", L"pathToExanimaSaves", L"", buf, 512, ConfigPath());
    return buf;
}
