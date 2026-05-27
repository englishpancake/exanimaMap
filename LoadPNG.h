#pragma once
#include <string>
#include <wincodec.h>

#pragma comment(lib, "windowscodecs.lib")

const wchar_t* ConfigPath();
const wchar_t* SavesDir();
const wchar_t* getMap(BYTE level);
int            getMapOffsetX(BYTE level);
int            getMapOffsetY(BYTE level);
float          getMapScale(BYTE level);
void           ClearMapCache();
std::wstring   getPathExanima();
BOOL           isToFullscreen();
