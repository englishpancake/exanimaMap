#pragma once

#include "resource.h"

#define IDM_MY_MSG_UPDATE_UI  (WM_APP + 4)
#define IDM_LEVEL_CHANGED     (WM_APP + 6)  // WPARAM=newLevel, LPARAM=oldLevel — bg thread → UI thread
#define IDM_PAINT_VISIT       (WM_APP + 7)  // WPARAM=level<<32|worldXbits, LPARAM=worldYbits

#define CORNERMAP  0x0010
#define FULLMAP    0x0100

// Runtime memory addresses — set from config.ini at startup.
// Defined in MapExanimaC.cpp.
extern LPVOID ADDR_MAP_LVL;
extern LPVOID ADDR_X_POS;
extern LPVOID ADDR_Y_POS;
