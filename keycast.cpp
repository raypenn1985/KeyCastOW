// Copyright © 2014 Brook Hong. All Rights Reserved.
//

// msbuild /p:platform=win32 /p:Configuration=Release
// msbuild keycastow.vcxproj /t:Clean
// rc keycastow.rc && cl -DUNICODE -D_UNICODE keycast.cpp keylog.cpp keycastow.res user32.lib shell32.lib gdi32.lib Comdlg32.lib comctl32.lib

#include <windows.h>
#include <Commctrl.h>
#include <stdio.h>

#include "resource.h"
#include "timer.h"
CTimer showTimer;
CTimer strokeTimer;

#define MAXCHARSINLINE 64
#define VISIBLECHARS 16
struct KeyLabel{
    RECT rect;
    WCHAR text[MAXCHARSINLINE];
    unsigned int time;
};

unsigned int keyStrokeDelay = 500;
unsigned int lingerTime = 1200;
unsigned int fadeDuration = 600;
unsigned int labelSpacing = 30;
COLORREF textColor = RGB(0,240, 33);
COLORREF bgColor = RGB(0x7f,0,0x8f);
LOGFONT labelFont;
float opacity = 0.78f;

unsigned int labelCount = 10;
KeyLabel keyLabels[10];

#include "keycast.h"
#include "keylog.h"

WCHAR *szWinName = L"KeyCast";
HWND hMainWnd;
HFONT hlabelFont;
HINSTANCE hInstance;
HDC hdcBuffer;

#define IDI_TRAY       100
#define WM_TRAYMSG     101
#define MENU_CONFIG    32
#define MENU_EXIT      33
#define MENU_RESTORE      34
void DrawAlphaBlend (HDC hdcwnd, int i)
{
    BLENDFUNCTION bf;      // structure for alpha blending
    HBITMAP hbitmap;       // bitmap handle
    BITMAPINFO bmi;        // bitmap header
    VOID *pvBits;          // pointer to DIB section
    ULONG   ulWindowWidth, ulWindowHeight;      // window width/height
    UINT32   x,y;          // stepping variables
    RECT &rt = keyLabels[i].rect;

    // calculate window width/height
    ulWindowWidth = rt.right - rt.left;
    ulWindowHeight = rt.bottom - rt.top;

    // make sure we have at least some window size
    if ((!ulWindowWidth) || (!ulWindowHeight))
        return;

    TextOut(hdcwnd, 8, keyLabels[i].rect.top, keyLabels[i].text, wcslen(keyLabels[i].text));

    // zero the memory for the bitmap info
    ZeroMemory(&bmi, sizeof(BITMAPINFO));

    // setup bitmap info
    // set the bitmap width and height to 60% of the width and height of each of the three horizontal areas. Later on, the blending will occur in the center of each of the three areas.
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = ulWindowWidth;
    bmi.bmiHeader.biHeight = ulWindowHeight;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;         // four 8-bit components
    bmi.bmiHeader.biCompression = BI_RGB;
    bmi.bmiHeader.biSizeImage = ulWindowWidth * ulWindowHeight * 4;

    // create our DIB section and select the bitmap into the dc
    hbitmap = CreateDIBSection(hdcBuffer, &bmi, DIB_RGB_COLORS, &pvBits, NULL, 0x0);
    SelectObject(hdcBuffer, hbitmap);

    for (y = 0; y < ulWindowHeight; y++)
        for (x = 0; x < ulWindowWidth; x++)
            ((UINT32 *)pvBits)[x + y * ulWindowWidth] = 0xffffffff;

    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    int alpha = 255-(int)(255.0*keyLabels[i].time/fadeDuration);
    alpha = (alpha < 0) ? 0: alpha;
    if(keyLabels[i].time == 0 && keyLabels[i].rect.right) {
        // remove the region for this label
        HRGN hRegion = CreateRectRgn(0,0,0,0);
        HRGN hRgnLabel = CreateRoundRectRgn (keyLabels[i].rect.left, keyLabels[i].rect.top, keyLabels[i].rect.right, keyLabels[i].rect.bottom, 15, 15);
        GetWindowRgn(hMainWnd, hRegion);
        CombineRgn(hRegion, hRegion, hRgnLabel, RGN_XOR);
        DeleteObject(hRgnLabel);
        SetWindowRgn(hMainWnd, hRegion, TRUE);
        InvalidateRect(hMainWnd, &keyLabels[i].rect, TRUE);
        keyLabels[i].rect.right = 0;
    }
    bf.SourceConstantAlpha = alpha;  // half of 0xff = 50% transparency
    bf.AlphaFormat = 0;             // ignore source alpha channel
    //bf.AlphaFormat = AC_SRC_ALPHA;  // use source alpha

    GdiAlphaBlend(hdcwnd, rt.left, rt.top,
                ulWindowWidth, ulWindowHeight,
                hdcBuffer, 0, 0, ulWindowWidth, ulWindowHeight, bf);
    DeleteObject(hbitmap);
}
void updateLabel(int i) {
    RECT box = {};
    DrawText(hdcBuffer, keyLabels[i].text, wcslen(keyLabels[i].text), &box, DT_CALCRECT);
    keyLabels[i].rect.right = box.right+18;
}
void drawLabels(HDC hdc) {
    SetTextColor(hdc, textColor);
    SetBkMode (hdc, TRANSPARENT);
    HFONT hFontOld = (HFONT)SelectObject(hdc, hlabelFont);
    for(unsigned int i = 0; i < labelCount; i ++) {
        DrawAlphaBlend(hdc, i);
    }
}

static void startFade() {
    unsigned int i = 0;
    for(i = 0; i < labelCount; i++) {
        if(keyLabels[i].time > fadeDuration) {
            keyLabels[i].time -= 100;
        } else if(keyLabels[i].time > 0) {
            keyLabels[i].time -= 100;
            InvalidateRect(hMainWnd, &keyLabels[i].rect, TRUE);
        }
    }
}

static bool newStroke = true;
static void startNewStroke() {
    newStroke = true;
}

void showText(LPCWSTR text, BOOL forceNewStroke = FALSE) {
    HRGN hRgnLabel;
    HRGN hRegion = CreateRectRgn(0,0,0,0);
    if(newStroke || forceNewStroke || wcslen(keyLabels[labelCount-1].text) > VISIBLECHARS-1) {
        unsigned int i;
        for (i = 1; i < labelCount; i++) {
            if(keyLabels[i].time > 0) {
                break;
            }
        }
        for (; i < labelCount; i++) {
            wcscpy_s(keyLabels[i-1].text, MAXCHARSINLINE, keyLabels[i].text);
            keyLabels[i-1].time = keyLabels[i].time;
            keyLabels[i-1].rect.right = keyLabels[i].rect.right;
        }
        wcscpy_s(keyLabels[labelCount-1].text, MAXCHARSINLINE, text);
        keyLabels[labelCount-1].time = lingerTime+fadeDuration;
        updateLabel(labelCount-1);

        for(i = 0; i < labelCount; i ++) {
            hRgnLabel = CreateRoundRectRgn (keyLabels[i].rect.left, keyLabels[i].rect.top, keyLabels[i].rect.right, keyLabels[i].rect.bottom, 15, 15);
            CombineRgn(hRegion, hRegion, hRgnLabel, RGN_OR);
            DeleteObject(hRgnLabel);
        }
        SetWindowRgn(hMainWnd, hRegion, TRUE);
        InvalidateRect(hMainWnd, NULL, TRUE);

        newStroke = false;
        strokeTimer.Start(keyStrokeDelay, false, true);
    } else {
        WCHAR tmp[MAXCHARSINLINE];
        wcscpy_s(tmp, MAXCHARSINLINE, keyLabels[labelCount-1].text);
        keyLabels[labelCount-1].time = lingerTime+fadeDuration;
        swprintf(keyLabels[labelCount-1].text, MAXCHARSINLINE, L"%s%s", tmp, text);
        strokeTimer.Stop();
        strokeTimer.Start(keyStrokeDelay, false, true);

        updateLabel(labelCount-1);
        hRgnLabel = CreateRoundRectRgn (keyLabels[labelCount-1].rect.left, keyLabels[labelCount-1].rect.top, keyLabels[labelCount-1].rect.right, keyLabels[labelCount-1].rect.bottom, 15, 15);
        hRegion = CreateRectRgn(0,0,0,0);
        GetWindowRgn(hMainWnd, hRegion);
        CombineRgn(hRegion, hRegion, hRgnLabel, RGN_OR);
        DeleteObject(hRgnLabel);
        SetWindowRgn(hMainWnd, hRegion, TRUE);
        InvalidateRect(hMainWnd, &keyLabels[labelCount-1].rect, TRUE);
    }
}

BOOL ColorDialog ( HWND hWnd, COLORREF &clr ) {
    DWORD dwCustClrs[16] = {
        RGB(0,0,0),
        RGB(0,0,255),
        RGB(0,255,0),
        RGB(128,255,255),
        RGB(255,0,0),
        RGB(255,0,255),
        RGB(255,255,0),
        RGB(192,192,192),
        RGB(127,127,127),
        RGB(0,0,128),
        RGB(0,128,0),
        RGB(0,255,255),
        RGB(128,0,0),
        RGB(255,0,128),
        RGB(128,128,64),
        RGB(255,255,255)
    };
    CHOOSECOLOR dlgColor;
    dlgColor.lStructSize = sizeof(CHOOSECOLOR);
    dlgColor.hwndOwner = hWnd;
    dlgColor.hInstance = NULL;
    dlgColor.lpTemplateName = NULL;
    dlgColor.rgbResult =  clr;
    dlgColor.lpCustColors =  dwCustClrs;
    dlgColor.Flags = CC_ANYCOLOR|CC_RGBINIT;
    dlgColor.lCustData = 0;
    dlgColor.lpfnHook = NULL;

    if(ChooseColor(&dlgColor)) {
        clr = dlgColor.rgbResult;
    }
    return TRUE;
}

void updateMainWindow() {
    HBRUSH brush = CreateSolidBrush(bgColor);
    SetClassLongPtr(hMainWnd, GCLP_HBRBACKGROUND, (LONG)brush);
    SetLayeredWindowAttributes(hMainWnd, 0, (BYTE)255*opacity, LWA_ALPHA);

    HFONT hFontOld = (HFONT)SelectObject(hdcBuffer, hlabelFont);
    RECT box = {};
    DrawText(hdcBuffer, L"A", 1, &box, DT_CALCRECT);
    int maxHeight = (box.bottom+4+labelSpacing)*labelCount;
    int maxWidth = box.right*(VISIBLECHARS+2)+18;
    RECT desktopRect;
    SystemParametersInfo(SPI_GETWORKAREA,NULL,&desktopRect,NULL);
    SetWindowPos(hMainWnd, HWND_TOPMOST, desktopRect.right - maxWidth, desktopRect.bottom - maxHeight, maxWidth, maxHeight, 0);
    UpdateWindow(hMainWnd);

    for(unsigned int i = 0; i < labelCount; i ++) {
        keyLabels[i].rect.top = (box.bottom+4)*i+labelSpacing*i;
        keyLabels[i].rect.bottom = (box.bottom+4)*(i+1)+labelSpacing*i;
    }
}
void initSettings() {
    keyStrokeDelay = 500;
    lingerTime = 1200;
    fadeDuration = 600;
    labelSpacing = 30;
    textColor = RGB(0,240, 33);
    bgColor = RGB(0x7f,0,0x8f);
    opacity = 0.78f;
    memset(&labelFont, 0, sizeof(labelFont));
    labelFont.lfCharSet = DEFAULT_CHARSET;
    labelFont.lfHeight = -36;
    labelFont.lfPitchAndFamily = DEFAULT_PITCH;
    labelFont.lfWeight  = FW_NORMAL;
    labelFont.lfOutPrecision = OUT_DEFAULT_PRECIS;
    labelFont.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    labelFont.lfQuality = ANTIALIASED_QUALITY;
    wcscpy_s(labelFont.lfFaceName, sizeof(labelFont.lfFaceName), TEXT("Arial"));
}
BOOL saveSettings() {
    WCHAR tmp[256];
    BOOL res = TRUE;

    HKEY hRootKey, hChildKey;
    if(RegOpenCurrentUser(KEY_WRITE, &hRootKey) != ERROR_SUCCESS)
        return FALSE;

    if(RegCreateKeyEx(hRootKey, L"Software\\keycastow", 0, NULL, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, NULL, &hChildKey, NULL) != ERROR_SUCCESS) {
        RegCloseKey(hRootKey);
        return FALSE;
    }

    if(RegSetKeyValue(hChildKey, NULL, L"keyStrokeDelay", REG_DWORD, (LPCVOID)&keyStrokeDelay, sizeof(keyStrokeDelay)) != ERROR_SUCCESS) {
        res = FALSE;
    }

    RegSetKeyValue(hChildKey, NULL, L"lingerTime", REG_DWORD, (LPCVOID)&lingerTime, sizeof(lingerTime));
    RegSetKeyValue(hChildKey, NULL, L"fadeDuration", REG_DWORD, (LPCVOID)&fadeDuration, sizeof(fadeDuration));
    RegSetKeyValue(hChildKey, NULL, L"labelSpacing", REG_DWORD, (LPCVOID)&labelSpacing, sizeof(labelSpacing));
    RegSetKeyValue(hChildKey, NULL, L"bgColor", REG_DWORD, (LPCVOID)&bgColor, sizeof(bgColor));
    RegSetKeyValue(hChildKey, NULL, L"textColor", REG_DWORD, (LPCVOID)&textColor, sizeof(textColor));
    RegSetKeyValue(hChildKey, NULL, L"labelFont", REG_BINARY, (LPCVOID)&labelFont, sizeof(labelFont));
    swprintf(tmp, 256, L"%f", opacity);
    RegSetKeyValue(hChildKey, NULL, L"opacity", REG_EXPAND_SZ, (LPCVOID)tmp, sizeof(tmp));

    RegCloseKey(hRootKey);
    RegCloseKey(hChildKey);
    return res;
}
BOOL loadSettings() {
    BOOL res = TRUE;
    HKEY hRootKey, hChildKey;
    DWORD disposition; // For checking if key was created or only opened
    initSettings();
    if(RegOpenCurrentUser(KEY_WRITE | KEY_READ, &hRootKey) != ERROR_SUCCESS)
        return FALSE;
    if(RegCreateKeyEx(hRootKey, TEXT("SOFTWARE\\keycastow"), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE,
                NULL, &hChildKey, &disposition) != ERROR_SUCCESS) {
        RegCloseKey(hRootKey);
        return FALSE;
    }

    DWORD size = sizeof(DWORD);
    if(disposition == REG_OPENED_EXISTING_KEY) {
        RegGetValue(hChildKey, NULL, L"keyStrokeDelay", RRF_RT_DWORD, NULL, &keyStrokeDelay, &size);
        RegGetValue(hChildKey, NULL, L"lingerTime", RRF_RT_DWORD, NULL, &lingerTime, &size);
        RegGetValue(hChildKey, NULL, L"fadeDuration", RRF_RT_DWORD, NULL, &fadeDuration, &size);
        RegGetValue(hChildKey, NULL, L"labelSpacing", RRF_RT_DWORD, NULL, &labelSpacing, &size);
        RegGetValue(hChildKey, NULL, L"bgColor", RRF_RT_DWORD, NULL, &bgColor, &size);
        RegGetValue(hChildKey, NULL, L"textColor", RRF_RT_DWORD, NULL, &textColor, &size);

        WCHAR tmp[256];
        size = sizeof(tmp);
        RegGetValue(hChildKey, NULL, L"opacity", RRF_RT_REG_EXPAND_SZ, NULL, tmp, &size);
        opacity = (float)_wtof(tmp);

        size = sizeof(labelFont);
        RegGetValue(hChildKey, NULL, L"labelFont", RRF_RT_REG_BINARY, NULL, &labelFont, &size);
    } else {
        saveSettings();
    }

    RegCloseKey(hRootKey);
    RegCloseKey(hChildKey);
    return res;
}
BOOL CALLBACK SettingsWndProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    WCHAR tmp[256];
    switch (msg)
    {
        case WM_INITDIALOG:
            swprintf(tmp, 256, L"%d", keyStrokeDelay);
            SetDlgItemText(hwndDlg, IDC_KEYSTROKEDELAY, tmp);
            swprintf(tmp, 256, L"%d", lingerTime);
            SetDlgItemText(hwndDlg, IDC_LINGERTIME, tmp);
            swprintf(tmp, 256, L"%d", fadeDuration);
            SetDlgItemText(hwndDlg, IDC_FADEDURATION, tmp);
            swprintf(tmp, 256, L"%d", labelSpacing);
            SetDlgItemText(hwndDlg, IDC_LABELSPACING, tmp);
            swprintf(tmp, 256, L"%f", opacity);
            SetDlgItemText(hwndDlg, IDC_OPACITY, tmp);
            return TRUE;
        case WM_NOTIFY:
            switch (((LPNMHDR)lParam)->code)
            {

                case NM_CLICK:          // Fall through to the next case.
                case NM_RETURN:
                    {
                        PNMLINK pNMLink = (PNMLINK)lParam;
                        LITEM   item    = pNMLink->item;
                        ShellExecute(NULL, L"open", item.szUrl, NULL, NULL, SW_SHOW);
                        break;
                    }
            }

            break;
        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDC_TEXTFONT:
                    {
                        CHOOSEFONT cf ;
                        cf.lStructSize    = sizeof (CHOOSEFONT) ;
                        cf.hwndOwner      = hwndDlg ;
                        cf.hDC            = NULL ;
                        cf.lpLogFont      = &labelFont ;
                        cf.iPointSize     = 0 ;
                        cf.Flags          = CF_INITTOLOGFONTSTRUCT | CF_SCREENFONTS | CF_EFFECTS ;
                        cf.rgbColors      = 0 ;
                        cf.lCustData      = 0 ;
                        cf.lpfnHook       = NULL ;
                        cf.lpTemplateName = NULL ;
                        cf.hInstance      = NULL ;
                        cf.lpszStyle      = NULL ;
                        cf.nFontType      = 0 ;               // Returned from ChooseFont
                        cf.nSizeMin       = 0 ;
                        cf.nSizeMax       = 0 ;

                        if(ChooseFont (&cf)) {
                            hlabelFont = CreateFontIndirect(&labelFont);
                        }
                    }
                    return TRUE;
                case IDC_TEXTCOLOR:
                    return ColorDialog(hwndDlg, textColor);
                case IDC_BGCOLOR:
                    return ColorDialog(hwndDlg, bgColor);
                case IDOK:
                    GetDlgItemText(hwndDlg, IDC_KEYSTROKEDELAY, tmp, 256);
                    keyStrokeDelay = _wtoi(tmp);
                    GetDlgItemText(hwndDlg, IDC_LINGERTIME, tmp, 256);
                    lingerTime = _wtoi(tmp);
                    GetDlgItemText(hwndDlg, IDC_FADEDURATION, tmp, 256);
                    fadeDuration = _wtoi(tmp);
                    GetDlgItemText(hwndDlg, IDC_LABELSPACING, tmp, 256);
                    labelSpacing = _wtoi(tmp);
                    GetDlgItemText(hwndDlg, IDC_OPACITY, tmp, 256);
                    opacity = (float)_wtof(tmp);
                    updateMainWindow();
                    InvalidateRect(hMainWnd, NULL, TRUE);
                    saveSettings();
                case IDCANCEL:
                    EndDialog(hwndDlg, wParam);
                    return TRUE;
            }
    }
    return FALSE;
}
LRESULT CALLBACK WindowFunc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static POINT s_last_mouse;
    static HMENU hPopMenu;
    static NOTIFYICONDATA nid;

    PAINTSTRUCT ps;
    HDC hdc;

    switch(message)
    {
        case WM_PAINT:
            hdc = BeginPaint(hWnd, &ps);
            drawLabels(hdc);
            EndPaint(hWnd, &ps);
            break;

        // trayicon
        case WM_CREATE:
            {
                memset( &nid, 0, sizeof( nid ) );

                nid.cbSize              = sizeof( nid );
                nid.hWnd                = hWnd;
                nid.uID                 = IDI_TRAY;
                nid.uFlags              = NIF_ICON | NIF_MESSAGE | NIF_TIP;
                nid.uCallbackMessage    = WM_TRAYMSG;
                nid.hIcon = LoadIcon( hInstance, MAKEINTRESOURCE(IDI_ICON1));
                lstrcpy( nid.szTip, L"KeyCast On Windows by brook hong" );
                Shell_NotifyIcon( NIM_ADD, &nid );

                hPopMenu = CreatePopupMenu();
                AppendMenu( hPopMenu, MF_STRING, MENU_CONFIG,  L"&Settings..." );
                AppendMenu( hPopMenu, MF_STRING, MENU_RESTORE,  L"&Restore default settings" );
                AppendMenu( hPopMenu, MF_STRING, MENU_EXIT,    L"E&xit" );
                SetMenuDefaultItem( hPopMenu, MENU_CONFIG, FALSE );
            }
            break;
        case WM_TRAYMSG:
            {
                switch ( lParam )
                {
                    case WM_RBUTTONUP:
                        {
                            POINT pnt;
                            GetCursorPos( &pnt );
                            SetForegroundWindow( hWnd ); // needed to get keyboard focus
                            TrackPopupMenu( hPopMenu, TPM_LEFTALIGN, pnt.x, pnt.y, 0, hWnd, NULL );
                        }
                        break;
                    case WM_LBUTTONDBLCLK:
                        SendMessage( hWnd, WM_COMMAND, MENU_CONFIG, 0 );
                        return 0;
                }
            }
            break;
        case WM_COMMAND:
            {
                switch ( LOWORD( wParam ) )
                {
                    case MENU_CONFIG:
                        if (DialogBox(NULL,
                                    MAKEINTRESOURCE(IDD_DLGSETTINGS),
                                    hWnd,
                                    (DLGPROC)SettingsWndProc)==IDOK) {
                            // Complete the command; szItemName contains the
                            // name of the item to delete.
                        } else {
                            // Cancel the command.
                        }
                        break;
                    case MENU_RESTORE:
                        initSettings();
                        saveSettings();
                        updateMainWindow();
                        InvalidateRect(hMainWnd, NULL, TRUE);
                        break;
                    case MENU_EXIT:
                        Shell_NotifyIcon( NIM_DELETE, &nid );
                        ExitProcess(0);
                        break;
                    default:
                        break;
                }
            }
            break;

        // hold mouse to move
        case WM_LBUTTONDOWN:
            SetCapture(hWnd);
            GetCursorPos(&s_last_mouse);
            showTimer.Stop();
            break;
        case WM_MOUSEMOVE:
            if (GetCapture()==hWnd)
            {
                POINT p;
                GetCursorPos(&p);
                int dx= p.x - s_last_mouse.x;
                int dy= p.y - s_last_mouse.y;
                if (dx||dy)
                {
                    s_last_mouse=p;
                    RECT r;
                    GetWindowRect(hWnd,&r);
                    SetWindowPos(hWnd,NULL,r.left+dx,r.top+dy,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
                }
            }
            break;
        case WM_LBUTTONUP:
            ReleaseCapture();
            showTimer.Start(100);
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hThisInst, HINSTANCE hPrevInst,
        LPSTR lpszArgs, int nWinMode)
{
    WNDCLASSEX wcl;
    MSG        msg;

    hInstance = hThisInst;

    wcl.cbSize = sizeof(WNDCLASSEX);
    wcl.hInstance = hThisInst;
    wcl.lpszClassName = szWinName;
    wcl.lpfnWndProc = WindowFunc;
    wcl.style = CS_DBLCLKS;
    wcl.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcl.hIconSm = LoadIcon(NULL, IDI_WINLOGO);
    wcl.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcl.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wcl.lpszMenuName = NULL;
    wcl.cbWndExtra = 0;
    wcl.cbClsExtra = 0;

    if(!RegisterClassEx(&wcl) )    {
        MessageBox(NULL, L"Could not register window class", L"Error", MB_OK);
        return 0;
    }

    //INITCOMMONCONTROLSEX ics;
    //ZeroMemory(&ics, sizeof(ics));
    //ics.dwSize = sizeof(ics);
    //ics.dwICC = ICC_LINK_CLASS;
    //InitCommonControlsEx(&ics);
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_LINK_CLASS|ICC_LISTVIEW_CLASSES|ICC_PAGESCROLLER_CLASS
        |ICC_PROGRESS_CLASS|ICC_STANDARD_CLASSES|ICC_TAB_CLASSES|ICC_TREEVIEW_CLASSES
        |ICC_UPDOWN_CLASS|ICC_USEREX_CLASSES|ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    hMainWnd = CreateWindowEx(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
            szWinName,
            szWinName,
            WS_POPUP,
            0, 0,            //X and Y position of window
            0, 0,            //Width and height of window
            NULL,
            NULL,
            hThisInst,
            NULL
            );
    if( !hMainWnd)    {
        MessageBox(NULL, L"Could not create window", L"Error", MB_OK);
        return 0;
    }

    loadSettings();
    hlabelFont = CreateFontIndirect(&labelFont);

    HDC hdc = GetDC(hMainWnd);
    hdcBuffer = CreateCompatibleDC(hdc);
    ReleaseDC(hMainWnd, hdc);

    for(unsigned int i = 0; i < labelCount; i ++) {
        keyLabels[i].time = 0;
        keyLabels[i].rect.left = 0;
        keyLabels[i].rect.right = 0;
    }
    updateMainWindow();
    HRGN hRegion = CreateRectRgn(0,0,0,0);
    SetWindowRgn(hMainWnd, hRegion, TRUE);
    ShowWindow(hMainWnd, SW_SHOW);

    showTimer.OnTimedEvent = startFade;
    showTimer.Start(100);
    strokeTimer.OnTimedEvent = startNewStroke;

    kbdhook = SetWindowsHookEx(WH_KEYBOARD_LL, LLKeyboardProc, hThisInst, NULL);

    while( GetMessage(&msg, NULL, 0, 0) )    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return msg.wParam;
}
