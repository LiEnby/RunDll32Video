#define WINVER _WIN32_WINNT_WIN7

#include <new>
#include <windows.h>
#include <windowsx.h>
#include <mfplay.h>
#include <mferror.h>
#include <strsafe.h>
#include <stdio.h>
#include <stdlib.h>
#include <tlhelp32.h>
#include "resource.h"


#pragma comment(linker, \
    "\"/manifestdependency:type='Win32' "\
    "name='Microsoft.Windows.Common-Controls' "\
    "version='6.0.0.0' "\
    "processorArchitecture='*' "\
    "publicKeyToken='6595b64144ccf1df' "\
    "language='*'\"")


HINSTANCE g_hinst = NULL;
HWND g_hwnd = NULL;
HINSTANCE g_dll_hinst = NULL;

BOOL WINAPI DllMain(
    HINSTANCE hinstDLL,  
    DWORD fdwReason,     
    LPVOID lpReserved)  
{
    
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        g_dll_hinst = hinstDLL;
        break;

    case DLL_THREAD_ATTACH:
        
        break;

    case DLL_THREAD_DETACH:
        
        break;

    case DLL_PROCESS_DETACH:
        
        break;
    }
    return TRUE;  
}
extern "C" __declspec (dllexport) void __cdecl fun(HWND hWnd, HINSTANCE hInst, LPWSTR  lpszCmdLine, int nCmdShow) {
    g_hwnd = hWnd;
    g_hinst = hInst;

    wWinMain(hInst, 0, lpszCmdLine, nCmdShow);
}

BOOL    InitializeWindow(HWND* pHwnd);
HRESULT PlayMediaFile(HWND hwnd, const WCHAR* sURL);
void    ShowErrorMessage(PCWSTR format, HRESULT hr);

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

void    OnClose(HWND hwnd);
void    OnPaint(HWND hwnd);
void    OnSize(HWND hwnd, UINT state, int cx, int cy);

void OnMediaItemCreated(MFP_MEDIAITEM_CREATED_EVENT* pEvent);
void OnMediaItemSet(MFP_MEDIAITEM_SET_EVENT* pEvent);


const WCHAR CLASS_NAME[] = L"Windows 7";
const WCHAR WINDOW_NAME[] = L"Windows 7";

#include <Shlwapi.h>
#include <fstream>

class MediaPlayerCallback : public IMFPMediaPlayerCallback
{
    long m_cRef; 

public:

    MediaPlayerCallback() : m_cRef(1)
    {
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        static const QITAB qit[] =
        {
            QITABENT(MediaPlayerCallback, IMFPMediaPlayerCallback),
            { 0 },
        };
        return QISearch(this, qit, riid, ppv);
    }
    STDMETHODIMP_(ULONG) AddRef()
    {
        return InterlockedIncrement(&m_cRef);
    }
    STDMETHODIMP_(ULONG) Release()
    {
        ULONG count = InterlockedDecrement(&m_cRef);
        if (count == 0)
        {
            delete this;
            return 0;
        }
        return count;
    }
    
    void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* pEventHeader);
};

IMFPMediaPlayer* g_pPlayer = NULL;      
MediaPlayerCallback* g_pPlayerCB = NULL;    
BOOL                    g_bHasVideo = FALSE;

INT WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, INT)
{
    (void)HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0);

    HWND hwnd = 0;
    MSG msg = { 0 };

    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
    {
        return 0;
    }

    if (!InitializeWindow(&hwnd))
    {
        return 0;
    }

    
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    DestroyWindow(hwnd);
    CoUninitialize();

    return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        HANDLE_MSG(hwnd, WM_CLOSE, OnClose);
        HANDLE_MSG(hwnd, WM_PAINT, OnPaint);
        HANDLE_MSG(hwnd, WM_SIZE, OnSize);

    case WM_ERASEBKGND:
        return 1;

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

void GetDesktopResolution(int& horizontal, int& vertical)
{
    RECT desktop;
    
    const HWND hDesktop = GetDesktopWindow();
    
    GetWindowRect(hDesktop, &desktop);    
    
    horizontal = desktop.right;
    vertical = desktop.bottom;
}
BOOL InitializeWindow(HWND* pHwnd)
{
    WNDCLASS wc = { 0 };

    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = CLASS_NAME;

    if (!RegisterClass(&wc))
    {
        return FALSE;
    }

    HWND hwnd = CreateWindow(
        CLASS_NAME,
        WINDOW_NAME,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        NULL,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );

    if (!hwnd)
    {
        hwnd = g_hwnd;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    *pHwnd = hwnd;
    
    WCHAR* tmpVar;
    size_t sz;

    LONG lStyle = GetWindowLong(hwnd, GWL_STYLE);
    lStyle &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
    SetWindowLong(hwnd, GWL_STYLE, lStyle);

    int width = 0;
    int height = 0;
    
    GetDesktopResolution(width, height);
    SetWindowPos(hwnd, 0, 0, 0, width, height, SWP_SHOWWINDOW);
  
    _wdupenv_s(&tmpVar, &sz,L"TEMP");
    WCHAR video_file_path[_MAX_PATH];
    _snwprintf_s(video_file_path, _MAX_PATH, L"%s/video.mp4", tmpVar);
    

    HRSRC hResource = FindResource(g_dll_hinst, MAKEINTRESOURCE(IDR_VIDEO1), L"VIDEO");
    HGLOBAL hLoadedResource = LoadResource(g_dll_hinst, hResource);
    void* pLockedResource = LockResource(hLoadedResource);
    DWORD dwResourceSize = SizeofResource(g_dll_hinst, hResource);
   
    FILE* mp4;
    _wfopen_s(&mp4, video_file_path, L"wb");
    fwrite(pLockedResource, dwResourceSize, 0x1, mp4);
    fclose(mp4);    
    
    PlayMediaFile(hwnd, video_file_path);

    return TRUE;
}


void OnClose(HWND /*hwnd*/)
{
    exit(0);
}


void OnPaint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc = 0;

    hdc = BeginPaint(hwnd, &ps);

    if (g_pPlayer && g_bHasVideo)
    {
        g_pPlayer->UpdateVideo();
    }
    else
    {
        FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW + 1));
    }

    EndPaint(hwnd, &ps);
}

void OnSize(HWND /*hwnd*/, UINT state, int /*cx*/, int /*cy*/)
{
    if (state == SIZE_RESTORED)
    {
        if (g_pPlayer)
        {
            
            g_pPlayer->UpdateVideo();
        }
    }
}

HRESULT PlayMediaFile(HWND hwnd, const WCHAR* sURL)
{
    HRESULT hr = S_OK;

    
    if (g_pPlayer == NULL)
    {
        g_pPlayerCB = new (std::nothrow) MediaPlayerCallback();

        if (g_pPlayerCB == NULL)
        {
            hr = E_OUTOFMEMORY;
            goto done;
        }

        hr = MFPCreateMediaPlayer(
            NULL,
            FALSE,          
            0,              
            g_pPlayerCB,    
            hwnd,           
            &g_pPlayer
        );

        if (FAILED(hr)) { goto done; }
    }

    
    hr = g_pPlayer->CreateMediaItemFromURL(sURL, FALSE, 0, NULL);

done:
    return hr;
}


void MediaPlayerCallback::OnMediaPlayerEvent(MFP_EVENT_HEADER* pEventHeader)
{
    if (FAILED(pEventHeader->hrEvent))
    {
        ShowErrorMessage(L"Playback error", pEventHeader->hrEvent);
        return;
    }

    switch (pEventHeader->eEventType)
    {
    case MFP_EVENT_TYPE_MEDIAITEM_CREATED:
        OnMediaItemCreated(MFP_GET_MEDIAITEM_CREATED_EVENT(pEventHeader));
        break;

    case MFP_EVENT_TYPE_MEDIAITEM_SET:
        OnMediaItemSet(MFP_GET_MEDIAITEM_SET_EVENT(pEventHeader));
        break;
    case MFP_EVENT_TYPE_PLAYBACK_ENDED:
        OnMediaItemSet(MFP_GET_MEDIAITEM_SET_EVENT(pEventHeader));
        g_pPlayer->SetPosition(MFP_POSITIONTYPE_100NS, 0);
        g_pPlayer->Play();
    case MFP_EVENT_TYPE_STOP:
        OnMediaItemSet(MFP_GET_MEDIAITEM_SET_EVENT(pEventHeader));
        g_pPlayer->SetPosition(MFP_POSITIONTYPE_100NS, 0);
        g_pPlayer->Play();
        break;
    }
}


void OnMediaItemCreated(MFP_MEDIAITEM_CREATED_EVENT* pEvent)
{
    HRESULT hr = S_OK;    

    if (g_pPlayer)
    {
        BOOL bHasVideo = FALSE, bIsSelected = FALSE;

        
        hr = pEvent->pMediaItem->HasVideo(&bHasVideo, &bIsSelected);

        if (FAILED(hr)) { goto done; }

        g_bHasVideo = bHasVideo && bIsSelected;

        
        hr = g_pPlayer->SetMediaItem(pEvent->pMediaItem);
    }

done:
    if (FAILED(hr))
    {
        ShowErrorMessage(L"Error playing this file.", hr);
    }
}

void OnMediaItemSet(MFP_MEDIAITEM_SET_EVENT* /*pEvent*/)
{
    HRESULT hr = S_OK;

    hr = g_pPlayer->Play();

    if (FAILED(hr))
    {
        ShowErrorMessage(L"IMFPMediaPlayer::Play failed.", hr);
    }
}

void ShowErrorMessage(PCWSTR format, HRESULT hrErr)
{
    HRESULT hr = S_OK;
    WCHAR msg[MAX_PATH];

    hr = StringCbPrintf(msg, sizeof(msg), L"%s (hr=0x%X)", format, hrErr);

    if (SUCCEEDED(hr))
    {
        MessageBox(NULL, msg, L"Error", MB_ICONERROR);
    }
}

