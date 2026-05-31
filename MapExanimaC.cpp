#include "framework.h"
#include "MapExanimaC.h"
#include "LoadPNG.h"
#include <thread>
#include <atomic>
#include <windowsx.h>
#include <d2d1.h>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cmath>
#include <psapi.h>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using namespace std;

#define MAX_LOADSTRING 100

// ── Global state ───────────────────────────────────────────────────────────

int monitor_width, monitor_height;
MONITORINFO info;
HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];
HWND hWnd;

int   winSizeWidth = 300, winSizeHeight = 300;
int   windowPos[]  = { 0, 0 };
DWORD mapState     = CORNERMAP;
bool  g_miniMap    = false;

std::atomic<bool>    b_readMemory  { true };
std::atomic<bool>    g_brushPaused { false };
std::atomic<uint8_t> mapLVL        { 0 };

float x_pos, y_pos;
float Map_rec[]    = { 0, 0 };
float PosFig_rec[] = { 140, 100 };
int   tmpmpos[]    = { 0, 0 };
float map_size[]   = { 300, 300 };
float x_maxSize    = 300.f;
float scale        = 1.0f;

constexpr float kMarkerW = 35.f * 0.85f;
constexpr float kMarkerH = 50.f * 0.85f;
constexpr float kMarkerFullMul = 1.8f;     // extra marker scale in the full-screen map view, aka I can't see shit
float g_markerAspect = kMarkerW / kMarkerH;

// Camera facing in degrees (written by the memory thread, read by the UI thread).
// It drives either the marker (static-map default) or the map (rotate_map mode).
float             g_mapAngle    = 0.f;      // map rotation    (rotate_map = 1)
float             g_markerAngle = 0.f;      // marker rotation (rotate_map = 0, the default)
std::atomic<bool> g_camRotOn    { false };  // camera rotation addresses resolved
bool              g_rotateMap   = false;    // config: rotate_map — turn the map vs the marker

// Runtime memory addresses — loaded from config.ini [MemoryAddresses] at startup.
LPVOID ADDR_MAP_LVL = nullptr;
LPVOID ADDR_X_POS   = nullptr;
LPVOID ADDR_Y_POS   = nullptr;
LPVOID ADDR_ROT_X   = nullptr;
LPVOID ADDR_ROT_Y   = nullptr;

// Direct2D objects
ID2D1Bitmap*           bmp_Map      = nullptr;
ID2D1Bitmap*           bmp_PosFig   = nullptr;
IWICImagingFactory*    wicFactory   = nullptr;
ID2D1Factory*          D2DFactory   = nullptr;
ID2D1HwndRenderTarget* renderTarget = nullptr;

BYTE  g_opacity   = 153;            // window opacity 0-255 (config: opacity, 0-100 percent)
bool  g_quickSave = false;          // config: quickSave
float g_brushR  = 0.667f;           // exploration trail colour (config: brush_color, hex RGB)
float g_brushG  = 0.067f;
float g_brushB  = 0.067f;
BYTE  g_cursorR = 0, g_cursorG = 0, g_cursorB = 0;  // marker tint (config: cursor_color)
bool  g_cursorTint = false;

// Exploration canvas — D2D is single-threaded; all canvas access must be on the UI thread.
ID2D1BitmapRenderTarget*               explorationCanvas = nullptr;
ID2D1SolidColorBrush*                  g_trailBrush      = nullptr;
unordered_map<BYTE, vector<pair<float,float>>> explorationPoints;
unordered_set<uint64_t>                g_visitedCells;
float prevWorldX = 0.f, prevWorldY = 0.f;
float brushRadius = 2.f;
float g_cellSize  = 25.f;
float g_canvasW   = 4096.f, g_canvasH = 4096.f;
float g_curOffsetX = 0.f,   g_curOffsetY = 0.f;
float g_curScale   = 0.0786f;

// Exanima.exe module base — resolved once on connect; config offsets are relative to this.
uintptr_t g_exeBase = 0;

HWND mainWindowH;

// ── Forward declarations ───────────────────────────────────────────────────

ATOM             MyRegisterClass(HINSTANCE);
BOOL             InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void             ReadMemoryOfExanima();
thread*          ptr_t_ReadMemoryOfExanima;
void             HideWindowBorders(HWND);
ID2D1Bitmap*     lbmpfromFile(const wchar_t*);
ID2D1Bitmap*     lbmpTintedFromFile(const wchar_t*, BYTE, BYTE, BYTE);
void             UpdateWindowProp();
void             LoadAddressesFromConfig();
void             CreateExplorationCanvas(BYTE level);
void             PaintVisit(float worldX, float worldY);
void             SaveExploration(BYTE level);
void             LoadExploration(BYTE level);
LRESULT CALLBACK keyboard_hook(int, WPARAM, LPARAM);

// ── Config ────────────────────────────────────────────────────────────────

void LoadAddressesFromConfig() {
    const wchar_t* ini = ConfigPath();
    // Config stores module-relative offsets (e.g. 0x48ACC8 = Exanima.exe+48ACC8).
    // g_exeBase is added so the final address is correct regardless of ASLR.
    auto readPtr = [&](const wchar_t* key) -> LPVOID {
        wchar_t buf[32] = {};
        GetPrivateProfileStringW(L"MemoryAddresses", key, L"0", buf, 32, ini);
        uintptr_t offset = wcstoull(buf, nullptr, 16);
        return offset ? (LPVOID)(g_exeBase + offset) : nullptr;
    };
    ADDR_X_POS   = readPtr(L"offset_x_ptr");
    ADDR_Y_POS   = readPtr(L"offset_y_ptr");
    ADDR_MAP_LVL = readPtr(L"offset_lvl_ptr");
    ADDR_ROT_X  = readPtr(L"rotationx_ptr");
    ADDR_ROT_Y  = readPtr(L"rotationy_ptr");
    g_camRotOn  = (ADDR_ROT_X != nullptr && ADDR_ROT_Y != nullptr);
    g_rotateMap = GetPrivateProfileIntW(L"AppSettings", L"rotate_map", 0, ini) != 0;
    brushRadius   = (float)GetPrivateProfileIntW(L"AppSettings", L"brush_radius", 2, ini);
    g_opacity     = (BYTE)(GetPrivateProfileIntW(L"AppSettings", L"opacity", 60, ini) * 255 / 100);
    g_brushPaused = GetPrivateProfileIntW(L"AppSettings", L"brush_enabled", 1, ini) == 0;
    g_quickSave   = GetPrivateProfileIntW(L"AppSettings", L"quickSave", 0, ini) != 0;

    wchar_t colorBuf[16] = {};
    GetPrivateProfileStringW(L"AppSettings", L"brush_color", L"AA1111", colorBuf, 16, ini);
    unsigned long rgb = wcstoul(colorBuf, nullptr, 16);
    g_brushR = ((rgb >> 16) & 0xFF) / 255.f;
    g_brushG = ((rgb >>  8) & 0xFF) / 255.f;
    g_brushB = ((rgb >>  0) & 0xFF) / 255.f;

    wchar_t cursorBuf[16] = {};
    GetPrivateProfileStringW(L"AppSettings", L"cursor_color", L"default", cursorBuf, 16, ini);
    g_cursorTint = (cursorBuf[0] != 0 && lstrcmpiW(cursorBuf, L"default") != 0);
    if (g_cursorTint) {
        unsigned long crgb = wcstoul(cursorBuf, nullptr, 16);
        g_cursorR = (BYTE)((crgb >> 16) & 0xFF);
        g_cursorG = (BYTE)((crgb >>  8) & 0xFF);
        g_cursorB = (BYTE)((crgb >>  0) & 0xFF);
    }

    ClearMapCache();
}

// ── Exploration canvas ────────────────────────────────────────────────────

void CreateExplorationCanvas(BYTE level) {
    if (g_trailBrush)      { g_trailBrush->Release();      g_trailBrush      = nullptr; }
    if (explorationCanvas) { explorationCanvas->Release(); explorationCanvas = nullptr; }
    if (!renderTarget) return;

    // Match canvas to the loaded PNG so stroke pixels align 1:1 with map pixels.
    g_canvasW = bmp_Map ? bmp_Map->GetSize().width  : 4096.f;
    g_canvasH = bmp_Map ? bmp_Map->GetSize().height : 4096.f;

    renderTarget->CreateCompatibleRenderTarget(D2D1::SizeF(g_canvasW, g_canvasH), &explorationCanvas);
    if (!explorationCanvas) return;

    explorationCanvas->BeginDraw();
    explorationCanvas->Clear(D2D1::ColorF(0, 0, 0, 0));
    explorationCanvas->EndDraw();

    // Create brush once and reuse — avoids a COM allocation on every stroke.
    explorationCanvas->CreateSolidColorBrush(
        D2D1::ColorF(g_brushR, g_brushG, g_brushB, 0.85f), &g_trailBrush);

    map_size[0] = g_canvasW * scale;
    map_size[1] = g_canvasH * scale;
    x_maxSize   = map_size[0];
    g_cellSize  = max(brushRadius / g_curScale, 1.f);
}

void PaintVisit(float worldX, float worldY) {
    if (!explorationCanvas || !g_trailBrush) return;
    float sx = (worldX + g_curOffsetX) * g_curScale;
    float sy = (worldY + g_curOffsetY) * g_curScale;
    if (sx < 0 || sx >= g_canvasW || sy < 0 || sy >= g_canvasH) return;
    explorationCanvas->BeginDraw();
    explorationCanvas->FillEllipse(
        D2D1::Ellipse(D2D1::Point2F(sx, sy), brushRadius, brushRadius), g_trailBrush);
    if (FAILED(explorationCanvas->EndDraw())) {
        explorationCanvas->Release(); explorationCanvas = nullptr;
        if (g_trailBrush) { g_trailBrush->Release(); g_trailBrush = nullptr; }
    }
}

// ── Exploration persistence ───────────────────────────────────────────────

static wstring ExplorationPath(BYTE level) {
    wchar_t buf[MAX_PATH];
    wsprintfW(buf, L"%s\\explored_lvl%d.dat", SavesDir(), (int)level);
    return buf;
}

static uint64_t visitCell(float wx, float wy) {
    int cx = (int)std::floor(wx / g_cellSize);
    int cy = (int)std::floor(wy / g_cellSize);
    return ((uint64_t)(uint32_t)cx << 32) | (uint32_t)cy;
}

void SaveExploration(BYTE level) {
    auto it = explorationPoints.find(level);
    if (it == explorationPoints.end() || it->second.empty()) return;
    CreateDirectoryW(SavesDir(), nullptr);
    ofstream f(ExplorationPath(level), ios::binary | ios::trunc);
    if (!f) return;
    uint32_t count = (uint32_t)it->second.size();
    f.write((char*)&count, sizeof(count));
    f.write((char*)it->second.data(), count * sizeof(pair<float,float>));
}

void LoadExploration(BYTE level) {
    ifstream f(ExplorationPath(level), ios::binary);
    if (!f) return;
    uint32_t count = 0;
    f.read((char*)&count, sizeof(count));
    if (count == 0 || count > 5000000) return;
    auto& pts = explorationPoints[level];
    pts.resize(count);
    f.read((char*)pts.data(), count * sizeof(pair<float,float>));
    if (!explorationCanvas || !g_trailBrush) return;
    explorationCanvas->BeginDraw();
    for (auto& [wx, wy] : pts) {
        g_visitedCells.insert(visitCell(wx, wy));
        float sx = (wx + g_curOffsetX) * g_curScale;
        float sy = (wy + g_curOffsetY) * g_curScale;
        if (sx >= 0 && sx < g_canvasW && sy >= 0 && sy < g_canvasH)
            explorationCanvas->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(sx, sy), brushRadius, brushRadius), g_trailBrush);
    }
    if (FAILED(explorationCanvas->EndDraw())) {
        explorationCanvas->Release(); explorationCanvas = nullptr;
        if (g_trailBrush) { g_trailBrush->Release(); g_trailBrush = nullptr; }
    }
}

// ── Entry point ───────────────────────────────────────────────────────────

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                      _In_opt_ HINSTANCE,
                      _In_ LPWSTR,
                      _In_ int nCmdShow)
{
    CoInitializeEx(nullptr, 0);
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                     IID_PPV_ARGS(&wicFactory));
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &D2DFactory);

    LoadStringW(hInstance, IDS_APP_TITLE,   szTitle,       MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_MAPEXANIMAC, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);
    LoadAddressesFromConfig();
    CreateDirectoryW(SavesDir(), nullptr);

    if (!InitInstance(hInstance, nCmdShow)) return FALSE;

    thread t_ReadMemoryOfExanima(ReadMemoryOfExanima);
    ptr_t_ReadMemoryOfExanima = &t_ReadMemoryOfExanima;

    HHOOK hHook = SetWindowsHookEx(WH_KEYBOARD_LL, keyboard_hook, hInstance, 0);
    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_MAPEXANIMAC));
    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    ptr_t_ReadMemoryOfExanima->join();
    UnhookWindowsHookEx(hHook);
    return (int)msg.wParam;
}

// ── Window class ──────────────────────────────────────────────────────────

ATOM MyRegisterClass(HINSTANCE hInstance) {
    WNDCLASSEXW wcex{};
    wcex.cbSize        = sizeof(WNDCLASSEX);
    wcex.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc   = WndProc;
    wcex.hInstance     = hInstance;
    wcex.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON));
    wcex.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm       = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON_SMALL));
    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow) {
    hInst = hInstance;
    hWnd  = CreateWindowExW(WS_EX_TOPMOST, szWindowClass, szTitle, WS_POPUP,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            winSizeWidth, winSizeHeight,
                            nullptr, nullptr, hInstance, nullptr);
    if (!hWnd) return FALSE;
    mainWindowH = hWnd;

    D2DFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(hWnd, D2D1::SizeU(winSizeWidth, winSizeHeight)),
        &renderTarget);

    bmp_PosFig = g_cursorTint
        ? lbmpTintedFromFile(L"assets\\playericon.png", g_cursorR, g_cursorG, g_cursorB)
        : lbmpfromFile(L"assets\\playericon.png");
    if (bmp_PosFig) {
        D2D1_SIZE_F msz = bmp_PosFig->GetSize();
        if (msz.height > 0.f) g_markerAspect = msz.width / msz.height;
    }
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    return TRUE;
}

// ── WndProc ───────────────────────────────────────────────────────────────

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case IDM_MY_MSG_UPDATE_UI:
        if (mapLVL > 1) {
            if (mapState & CORNERMAP) {
                // Anchor point on screen where the player sits.
                // Mini mode centres in the smaller 150px window; normal uses 145/140.
                float ax = g_miniMap ? 75.f : 145.f;
                float ay = g_miniMap ? 75.f : 140.f;
                Map_rec[0]    = x_pos * scale + ax;
                Map_rec[1]    = y_pos * scale + ay;
                PosFig_rec[0] = ax;
                PosFig_rec[1] = ay;
            } else if (mapState & FULLMAP) {
                // In full-screen mode Map_rec is panned by mouse drag;
                // the player indicator follows accordingly.
                PosFig_rec[0] = Map_rec[0] - x_pos * scale;
                PosFig_rec[1] = Map_rec[1] - y_pos * scale;
            }
        }
        InvalidateRect(hWnd, nullptr, FALSE);
        break;

    case IDM_LEVEL_CHANGED:
    {
        BYTE newLVL = (BYTE)wParam;
        BYTE oldLVL = (BYTE)lParam;
        if (oldLVL > 1) {
            SaveExploration(oldLVL);
            explorationPoints.erase(oldLVL);
        }
        if (bmp_Map) { bmp_Map->Release(); bmp_Map = nullptr; }
        bmp_Map      = lbmpfromFile(getMap(newLVL));
        g_curOffsetX = (float)getMapOffsetX(newLVL);
        g_curOffsetY = (float)getMapOffsetY(newLVL);
        g_curScale   = getMapScale(newLVL);
        g_visitedCells.clear();
        CreateExplorationCanvas(newLVL);
        LoadExploration(newLVL);
        break;
    }

    case IDM_PAINT_VISIT:
    {
        // Discard messages that arrived after a level transition.
        BYTE msgLVL = (BYTE)(wParam >> 32);
        if (msgLVL != mapLVL) break;
        WPARAM wxLow = wParam & 0xFFFFFFFF;
        float wx, wy;
        memcpy(&wx, &wxLow, 4);
        memcpy(&wy, &lParam, 4);
        if (!g_visitedCells.insert(visitCell(wx, wy)).second) break;
        explorationPoints[mapLVL].push_back({ wx, wy });
        PaintVisit(wx, wy);
        break;
    }

    case WM_CREATE:
        SetWindowLong(hWnd, GWL_EXSTYLE,
            GetWindowLong(hWnd, GWL_EXSTYLE) | WS_EX_LAYERED);
        SetLayeredWindowAttributes(hWnd, 0, g_opacity, LWA_ALPHA);
        SetTimer(hWnd, 1, 30000, nullptr);
        break;

    case WM_LBUTTONDOWN:
        tmpmpos[0] = GET_X_LPARAM(lParam);
        tmpmpos[1] = GET_Y_LPARAM(lParam);
        if (mapState & CORNERMAP) {
            ReleaseCapture();
            SendMessage(hWnd, 0xA1, 2, 0);  // WM_NCLBUTTONDOWN / HTCAPTION — starts OS drag
        }
        break;

    case WM_MOUSEMOVE:
        if ((mapState & FULLMAP) && (wParam & MK_LBUTTON)) {
            int dx = GET_X_LPARAM(lParam) - tmpmpos[0];
            int dy = GET_Y_LPARAM(lParam) - tmpmpos[1];
            Map_rec[0] += dx;
            Map_rec[1] += dy;
            tmpmpos[0]  = GET_X_LPARAM(lParam);
            tmpmpos[1]  = GET_Y_LPARAM(lParam);
        }
        break;

    case WM_LBUTTONDBLCLK:
        if (mapState & CORNERMAP) {
            RECT rec;
            GetWindowRect(hWnd, &rec);
            windowPos[0] = rec.left;
            windowPos[1] = rec.top;
        }
        g_miniMap  = false;
        mapState  ^= CORNERMAP;
        mapState  ^= FULLMAP;
        UpdateWindowProp();
        break;

    case WM_RBUTTONUP:
        if (mapState & CORNERMAP) {
            g_miniMap = !g_miniMap;
            UpdateWindowProp();
        }
        break;

    case WM_MOUSEWHEEL:
    {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);
        if (GET_WHEEL_DELTA_WPARAM(wParam) > 0 && map_size[0] <= x_maxSize + 3000) {
            scale       *= 1.1f;
            map_size[0] *= 1.1f;
            map_size[1] *= 1.1f;
            Map_rec[0]   = Map_rec[0] - (-Map_rec[0] + pt.x) * 0.1f;
            Map_rec[1]   = Map_rec[1] - (-Map_rec[1] + pt.y) * 0.1f;
        } else if (GET_WHEEL_DELTA_WPARAM(wParam) < 0 && map_size[0] > 600) {
            scale       /= 1.1f;
            map_size[0] /= 1.1f;
            map_size[1] /= 1.1f;
            Map_rec[0]   = Map_rec[0] + (-Map_rec[0] + pt.x) / 11.f;
            Map_rec[1]   = Map_rec[1] + (-Map_rec[1] + pt.y) / 11.f;
        }
        break;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        (void)BeginPaint(hWnd, &ps);
        if (renderTarget) {
            renderTarget->BeginDraw();
            renderTarget->Clear();

            D2D1_POINT_2F pivot = D2D1::Point2F(PosFig_rec[0], PosFig_rec[1]);
            bool mapRot = g_rotateMap && g_camRotOn && (mapState & CORNERMAP);
            renderTarget->SetTransform(mapRot
                ? D2D1::Matrix3x2F::Rotation(g_mapAngle, pivot)
                : D2D1::Matrix3x2F::Identity());

            if (bmp_Map)
                renderTarget->DrawBitmap(bmp_Map,
                    D2D1::RectF(Map_rec[0], Map_rec[1],
                                Map_rec[0] + map_size[0],
                                Map_rec[1] + map_size[1]));

            if (explorationCanvas) {
                ID2D1Bitmap* canvasBmp = nullptr;
                explorationCanvas->GetBitmap(&canvasBmp);
                if (canvasBmp) {
                    renderTarget->DrawBitmap(canvasBmp,
                        D2D1::RectF(Map_rec[0], Map_rec[1],
                                    Map_rec[0] + map_size[0],
                                    Map_rec[1] + map_size[1]));
                    canvasBmp->Release();
                }
            }

            if (bmp_PosFig) {
                float mz = sqrtf(scale);
                if (mz < 0.6f) mz = 0.6f; else if (mz > 1.6f) mz = 1.6f;
                if (mapState & FULLMAP) mz *= kMarkerFullMul;
                float hh = kMarkerH * mz * 0.5f;
                float hw = hh * g_markerAspect;
                bool markerRot = g_camRotOn && !mapRot;
                renderTarget->SetTransform(markerRot
                    ? D2D1::Matrix3x2F::Rotation(g_markerAngle, pivot)
                    : D2D1::Matrix3x2F::Identity());
                renderTarget->DrawBitmap(bmp_PosFig,
                    D2D1::RectF(pivot.x - hw, pivot.y - hh, pivot.x + hw, pivot.y + hh));
                renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
            }

            renderTarget->EndDraw();
        }
        EndPaint(hWnd, &ps);
        break;
    }

    case WM_TIMER:
        if (mapLVL > 1) SaveExploration(mapLVL);
        break;

    case WM_DESTROY:
        KillTimer(hWnd, 1);
        b_readMemory = false;
        if (mapLVL > 1) SaveExploration(mapLVL);
        if (bmp_Map)          { bmp_Map->Release();          bmp_Map          = nullptr; }
        if (g_trailBrush)     { g_trailBrush->Release();     g_trailBrush     = nullptr; }
        if (explorationCanvas){ explorationCanvas->Release(); explorationCanvas = nullptr; }
        PostQuitMessage(0);
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_CHAR:
        if (wParam == VK_ESCAPE) {
            b_readMemory = false;
            DestroyWindow(hWnd);
        }
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// ── Memory reading thread ─────────────────────────────────────────────────

void ReadMemoryOfExanima() {
    HWND hWindow = FindWindowEx(0, 0, L"Exanima", 0);
    DWORD processID;

    if (!hWindow) {
        int waited = 0;
        while (!hWindow && b_readMemory) {
            this_thread::sleep_for(chrono::seconds(1));
            hWindow = FindWindowEx(0, 0, L"Exanima", 0);
            if (++waited >= 300) {
                MessageBoxW(nullptr,
                    L"Exanima was not detected after 5 minutes.\n"
                    L"Start Exanima in windowed mode, then relaunch this tool.",
                    L"Exanima not found", MB_OK | MB_ICONERROR | MB_TOPMOST);
                PostMessage(mainWindowH, WM_CLOSE, 0, 0);
                return;
            }
        }
        if (!b_readMemory) return;
    }

    GetWindowThreadProcessId(hWindow, &processID);
    HANDLE hProcHandle = OpenProcess(
        PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, processID);

    // Resolve Exanima.exe base address so config offsets produce correct absolute addresses.
    {
        HMODULE hMods[512]; DWORD cbNeeded = 0;
        if (EnumProcessModules(hProcHandle, hMods, sizeof(hMods), &cbNeeded)) {
            for (DWORD i = 0; i < cbNeeded / sizeof(HMODULE); i++) {
                wchar_t name[MAX_PATH] = {};
                GetModuleFileNameExW(hProcHandle, hMods[i], name, MAX_PATH);
                if (wcsstr(name, L"Exanima") || wcsstr(name, L"exanima")) {
                    MODULEINFO mi = {};
                    GetModuleInformation(hProcHandle, hMods[i], &mi, sizeof(mi));
                    g_exeBase = (uintptr_t)mi.lpBaseOfDll;
                    break;
                }
            }
        }
        LoadAddressesFromConfig();
    }

    HMONITOR monitor = MonitorFromWindow(hWindow, MONITOR_DEFAULTTONEAREST);
    info.cbSize = sizeof(MONITORINFO);
    GetMonitorInfo(monitor, &info);
    monitor_width  = info.rcMonitor.right  - info.rcMonitor.left;
    monitor_height = info.rcMonitor.bottom - info.rcMonitor.top;

    if (isToFullscreen()) {
        HideWindowBorders(hWindow);
        SetWindowPos(hWindow, HWND_NOTOPMOST,
                     info.rcMonitor.left, 0, monitor_width, monitor_height,
                     SWP_NOSENDCHANGING);
        SetWindowPos(mainWindowH, HWND_TOPMOST,
                     info.rcMonitor.left, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SendMessage(hWindow, WM_EXITSIZEMOVE, 0, 0);
    }

    char chBufferMapLVL = 0;

    while (b_readMemory) {
        this_thread::sleep_for(chrono::milliseconds(20));

        if (!ADDR_X_POS || !ADDR_Y_POS) {
            PostMessage(mainWindowH, IDM_MY_MSG_UPDATE_UI, 0, 0);
            continue;
        }

        if (ADDR_MAP_LVL) {
            if (!ReadProcessMemory(hProcHandle, ADDR_MAP_LVL,
                                   &chBufferMapLVL, sizeof(chBufferMapLVL), 0)) {
                ADDR_MAP_LVL   = nullptr;
                chBufferMapLVL = 2;
            }
        } else {
            chBufferMapLVL = 2;
        }

        if (chBufferMapLVL > 1) {
            if (chBufferMapLVL != mapLVL) {
                PostMessage(mainWindowH, IDM_LEVEL_CHANGED,
                            (WPARAM)chBufferMapLVL, (LPARAM)mapLVL);
                mapLVL     = chBufferMapLVL;
                prevWorldX = prevWorldY = 0.f;
            }

            float rawX = 0.f, rawY = 0.f;
            ReadProcessMemory(hProcHandle, ADDR_X_POS, &rawX, sizeof(rawX), 0);
            ReadProcessMemory(hProcHandle, ADDR_Y_POS, &rawY, sizeof(rawY), 0);

            float scale_   = getMapScale(mapLVL);
            float originX  = -(float)getMapOffsetX(mapLVL);
            float originY  = -(float)getMapOffsetY(mapLVL);
            x_pos = (rawX - originX) * -scale_;
            y_pos = (rawY - originY) * -scale_;

            constexpr float kRad2Deg = 180.f / 3.14159265358979f;
            // Camera facing (2D vector, N=(0,1), E=(1,0)). The marker points along it
            // (+atan2, verified for 0.9.5g); the map turns the other way to bring that
            // facing to "up" (negated), used only in rotate_map mode.
            if (ADDR_ROT_X && ADDR_ROT_Y) {
                float cx = 0.f, cy = 0.f;
                if (ReadProcessMemory(hProcHandle, ADDR_ROT_X, &cx, sizeof(cx), 0) &&
                    ReadProcessMemory(hProcHandle, ADDR_ROT_Y, &cy, sizeof(cy), 0)) {
                    float a = atan2f(cx, cy) * kRad2Deg;
                    g_markerAngle =  a;
                    g_mapAngle    = -a;
                }
            }

            // Post a paint stroke when the player moves at least 1 world unit.
            // Skip if the delta is huge (>500 units) — indicates a menu transition
            // where X/Y snap to 0,0 while the level ID hasn't updated yet.
            float wdx = rawX - prevWorldX, wdy = rawY - prevWorldY;
            float dist2 = wdx * wdx + wdy * wdy;
            if (dist2 > 1.f) {
                prevWorldX = rawX; prevWorldY = rawY;
                if (!g_brushPaused && dist2 < 250000.f) {
                    WPARAM wx = 0; LPARAM wy = 0;
                    memcpy(&wx, &rawX, 4);
                    memcpy(&wy, &rawY, 4);
                    wx |= ((WPARAM)(uint8_t)chBufferMapLVL) << 32;
                    PostMessage(mainWindowH, IDM_PAINT_VISIT, wx, wy);
                }
            }
        }

        PostMessage(mainWindowH, IDM_MY_MSG_UPDATE_UI, 0, 0);
    }
}

// ── Utilities ─────────────────────────────────────────────────────────────

void HideWindowBorders(HWND hw) {
    SetWindowLong(hw, GWL_STYLE,
        GetWindowLong(hw, GWL_STYLE) & ~(WS_CAPTION | WS_SIZEBOX));
}

ID2D1Bitmap* lbmpfromFile(const wchar_t* file) {
    ID2D1Bitmap*           bmp          = nullptr;
    IWICBitmapDecoder*     wicDecoder   = nullptr;
    IWICFormatConverter*   wicConverter = nullptr;
    IWICBitmapFrameDecode* wicFrame     = nullptr;

    if (FAILED(wicFactory->CreateDecoderFromFilename(
            file, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &wicDecoder)))
        return nullptr;

    if (FAILED(wicDecoder->GetFrame(0, &wicFrame)))               goto cleanup;
    if (FAILED(wicFactory->CreateFormatConverter(&wicConverter)))  goto cleanup;
    if (FAILED(wicConverter->Initialize(wicFrame, GUID_WICPixelFormat32bppPBGRA,
                             WICBitmapDitherTypeNone, nullptr, 0.0,
                             WICBitmapPaletteTypeCustom)))         goto cleanup;
    if (renderTarget)
        renderTarget->CreateBitmapFromWicBitmap(wicConverter, nullptr, &bmp);

cleanup:
    if (wicDecoder)   wicDecoder->Release();
    if (wicConverter) wicConverter->Release();
    if (wicFrame)     wicFrame->Release();
    return bmp;
}

// Like lbmpfromFile, but recolours a solid (alpha-shaped) PNG to an RGB tint — used
// for the player marker so cursor_color works like brush_color. The PNG's own colour
// is discarded; each pixel's alpha is kept and premultiplied with the tint.
ID2D1Bitmap* lbmpTintedFromFile(const wchar_t* file, BYTE cr, BYTE cg, BYTE cb) {
    IWICBitmapDecoder*     dec  = nullptr;
    IWICFormatConverter*   conv = nullptr;
    IWICBitmapFrameDecode* frm  = nullptr;
    ID2D1Bitmap*           bmp  = nullptr;

    if (FAILED(wicFactory->CreateDecoderFromFilename(
            file, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec)))
        return nullptr;

    if (FAILED(dec->GetFrame(0, &frm)))                    goto cleanup;
    if (FAILED(wicFactory->CreateFormatConverter(&conv)))  goto cleanup;
    if (FAILED(conv->Initialize(frm, GUID_WICPixelFormat32bppPBGRA,
                       WICBitmapDitherTypeNone, nullptr, 0.0,
                       WICBitmapPaletteTypeCustom)))        goto cleanup;
    {
        UINT w = 0, h = 0;
        conv->GetSize(&w, &h);
        UINT stride = w * 4;
        std::vector<BYTE> px((size_t)stride * h);
        if (FAILED(conv->CopyPixels(nullptr, stride, (UINT)px.size(), px.data()))) goto cleanup;
        // 32bppPBGRA = premultiplied B,G,R,A. Replace the colour with the tint, scaled
        // by alpha to not fuck the anti aliased edges and make it blocky as hell.
        for (size_t i = 0; i + 3 < px.size(); i += 4) {
            BYTE a = px[i + 3];
            px[i + 0] = (BYTE)(cb * a / 255);
            px[i + 1] = (BYTE)(cg * a / 255);
            px[i + 2] = (BYTE)(cr * a / 255);
        }
        if (renderTarget) {
            D2D1_BITMAP_PROPERTIES bp = D2D1::BitmapProperties(
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            renderTarget->CreateBitmap(D2D1::SizeU(w, h), px.data(), stride, bp, &bmp);
        }
    }

cleanup:
    if (dec)  dec->Release();
    if (conv) conv->Release();
    if (frm)  frm->Release();
    return bmp;
}

void UpdateWindowProp() {
    SetWindowLong(hWnd, GWL_EXSTYLE,
        (GetWindowLong(hWnd, GWL_EXSTYLE) | WS_EX_LAYERED) & ~WS_EX_TRANSPARENT);
    SetLayeredWindowAttributes(hWnd, 0, g_opacity, LWA_ALPHA);

    if (mapState & CORNERMAP) {
        winSizeWidth = winSizeHeight = g_miniMap ? 150 : 300;
        SetWindowPos(mainWindowH, nullptr, windowPos[0], windowPos[1],
                     winSizeWidth, winSizeHeight, SWP_NOZORDER | SWP_NOACTIVATE);
        Map_rec[0] = Map_rec[1] = 0;
        PosFig_rec[0] = 140; PosFig_rec[1] = 100;
    } else {
        winSizeWidth  = monitor_width;
        winSizeHeight = monitor_height;
        SetWindowPos(mainWindowH, nullptr, 0, 0,
                     winSizeWidth, winSizeHeight, SWP_NOZORDER | SWP_NOACTIVATE);
        Map_rec[0] = (x_pos - windowPos[0] + winSizeWidth  / 2) * scale;
        Map_rec[1] = (y_pos - windowPos[1] + winSizeHeight / 2) * scale;
    }
    renderTarget->Resize(D2D1::SizeU(winSizeWidth, winSizeHeight));
}

// ── Keyboard hook ─────────────────────────────────────────────────────────

LRESULT CALLBACK keyboard_hook(int code, WPARAM wParam, LPARAM lParam) {
    if (wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* s = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

        if (s->vkCode == VK_F8)
            g_brushPaused = !g_brushPaused;

        if (g_quickSave) {

            if (s->vkCode == VK_F5) {
                if (MessageBox(nullptr,
                        L"Quick Backup? (Please pause or go to the menu first.)",
                        L"Confirm", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST) == IDYES) {
                    wstring dir = getPathExanima();
                    if (dir.length() < 2) {
                        wchar_t* pValue = nullptr; size_t len = 0;
                        _wdupenv_s(&pValue, &len, L"APPDATA");
                        if (pValue) { dir = wstring(pValue) + L"\\Exanima"; free(pValue); }
                    }
                    wstring backup = dir + L"\\backUP";
                    try {
                        if (!filesystem::exists(dir)) {
                            MessageBoxW(nullptr,
                                L"Exanima save folder not found.\n"
                                L"Set pathToExanimaSaves in assets\\config.ini.",
                                L"Exanima Backup", MB_OK | MB_ICONWARNING | MB_TOPMOST);
                        } else {
                            filesystem::create_directories(backup);
                            for (auto& entry : filesystem::directory_iterator(dir)) {
                                if (entry.path().extension() == L".rsg")
                                    filesystem::copy_file(entry.path(),
                                        backup + L"\\" + entry.path().filename().wstring(),
                                        filesystem::copy_options::overwrite_existing);
                            }
                            MessageBoxW(nullptr, L"Backup created.",
                                        L"Exanima Backup", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
                        }
                    } catch (const filesystem::filesystem_error&) {
                        MessageBoxW(nullptr,
                            L"Backup failed.\nCheck that pathToExanimaSaves in assets\\config.ini is correct.",
                            L"Exanima Backup", MB_OK | MB_ICONERROR | MB_TOPMOST);
                    }
                }
            }

            if (s->vkCode == VK_F6) {
                if (MessageBox(nullptr,
                        L"Load Backup? (This will overwrite your current saves.)",
                        L"Confirm", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST) == IDYES) {
                    wstring dir = getPathExanima();
                    if (dir.length() < 2) {
                        wchar_t* pValue = nullptr; size_t len = 0;
                        _wdupenv_s(&pValue, &len, L"APPDATA");
                        if (pValue) { dir = wstring(pValue) + L"\\Exanima"; free(pValue); }
                    }
                    wstring backup = dir + L"\\backUP";
                    try {
                        if (!filesystem::exists(backup)) {
                            MessageBoxW(nullptr, L"No backup found.",
                                        L"Exanima Backup", MB_OK | MB_ICONWARNING | MB_TOPMOST);
                        } else if (!filesystem::exists(dir)) {
                            MessageBoxW(nullptr,
                                L"Exanima save folder not found.\n"
                                L"Set pathToExanimaSaves in assets\\config.ini.",
                                L"Exanima Backup", MB_OK | MB_ICONWARNING | MB_TOPMOST);
                        } else {
                            for (auto& entry : filesystem::directory_iterator(backup)) {
                                if (entry.path().extension() == L".rsg")
                                    filesystem::copy_file(entry.path(),
                                        dir + L"\\" + entry.path().filename().wstring(),
                                        filesystem::copy_options::overwrite_existing);
                            }
                            MessageBoxW(nullptr, L"Backup loaded.",
                                        L"Exanima Backup", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
                        }
                    } catch (const filesystem::filesystem_error&) {
                        MessageBoxW(nullptr,
                            L"Restore failed.\nCheck that pathToExanimaSaves in assets\\config.ini is correct.",
                            L"Exanima Backup", MB_OK | MB_ICONERROR | MB_TOPMOST);
                    }
                }
            }
        }
    }
    return CallNextHookEx(0, code, wParam, lParam);
}
