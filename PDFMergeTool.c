/*
 * PDFMergeTool.c  — Quick PDF Tool
 * Win32 GUI application (MinGW / GCC)
 *
 * Compile:
 *   windres resources.rc -o resources.o
 *   gcc PDFMergeTool.c pdf_merge.c miniz.c resources.o -o PDFMergeTool.exe ^
 *       -lcomctl32 -lgdi32 -lcomdlg32 -lshell32 -lshlwapi -lole32 ^
 *       -ldwmapi -ladvapi32 -mwindows -static-libgcc -O2
 */

#define WIN32_LEAN_AND_MEAN
#define _WIN32_IE 0x0600
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "miniz.h"
#include "pdf_merge.h"

/* DWM dark-mode attribute (Windows 10 20H1+) */
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

/* ============================================================
 * CONSTANTS
 * ============================================================ */

#define APP_CLASS   "QuickPDFTool"
#define APP_TITLE   "Quick PDF Tool"
#define WIN_W       900
#define WIN_H       660
#define MIN_WIN_W   700
#define MIN_WIN_H   540

#define SCR_WELCOME  0
#define SCR_MERGE    1
#define SCR_SPLIT    2
#define SCR_ARRANGE  3

#define ID_BTN_MERGE        101
#define ID_BTN_SPLIT        102
#define ID_BTN_ARRANGE      103
#define ID_BTN_PLACEHOLDER  104
#define ID_BTN_BACK         110
#define ID_DROP_AREA        111
#define ID_EDIT_OUTNAME     112
#define ID_CHK_DELZIP       113
#define ID_CHK_OPENDIR      114
#define ID_BTN_RUN_MERGE    115
#define ID_LST_MERGE_PDFS   116
#define ID_BTN_MERGE_UP     117
#define ID_BTN_MERGE_DN     118
#define ID_DROP_SPLIT       120
#define ID_EDIT_SPLIT_DIR   121
#define ID_BTN_SPLIT_BROWSE 122
#define ID_BTN_RUN_SPLIT    123
#define ID_BTN_BACK_SPLIT   124
#define ID_EDIT_SPLIT_NAME  125
#define ID_LST_PDFS         130
#define ID_BTN_ADD_PDFS     131
#define ID_BTN_MOVE_UP      132
#define ID_BTN_MOVE_DOWN    133
#define ID_BTN_REMOVE_PDF   134
#define ID_EDIT_ARR_OUT     135
#define ID_CHK_ARR_OPEN     136
#define ID_BTN_RUN_ARR      137
#define ID_BTN_BACK_ARR     138
#define ID_BTN_ARR_UP       141
#define ID_BTN_ARR_DN       142
#define ID_BTN_ARR_X        143
#define ID_BTN_ARR_CLEAR    144

#define MAX_ARR_FILES 512   /* max PDFs in the Arrange list */

#define IDI_APPICON   1
#define IDR_LOGO_PNG  101

/* ============================================================
 * DYNAMIC COLOUR PALETTE  (set by setup_theme, dark or light)
 * ============================================================ */

static COLORREF C_BG;          /* window / welcome background      */
static COLORREF C_CARD;        /* card face                         */
static COLORREF C_CARD_HOV;    /* card face while hovered           */
static COLORREF C_SUB_BG;      /* sub-screen background             */
static COLORREF C_EDIT_BG;     /* edit control interior             */
static COLORREF C_ACCENT;      /* brand blue                        */
static COLORREF C_ACCENT_DIM;  /* pressed-state accent              */
static COLORREF C_BORDER;      /* subtle border                     */
static COLORREF C_BORDER_HOV;  /* highlighted border (hover)        */
static COLORREF C_SHADOW;      /* card drop-shadow                  */
static COLORREF C_TEXT;        /* primary text                      */
static COLORREF C_SUBTEXT;     /* secondary / description text      */
static COLORREF C_FOOTER;      /* footer signature text             */
static COLORREF C_DROP_FILL;   /* drag-zone interior fill           */
static COLORREF C_DROP_OUTER;  /* drag-zone outer solid border      */
static COLORREF C_DROP_DASH;   /* drag-zone inner dashed border     */
static COLORREF C_DROP_TEXT;   /* drop-hint text                    */
static COLORREF C_BTN_FG;      /* action-button label (always white)*/

static int g_dark = 0;

/* Cached background brushes — rebuilt whenever theme changes */
static HBRUSH g_hBrBg    = NULL;
static HBRUSH g_hBrSubBg = NULL;
static HBRUSH g_hBrEdit  = NULL;

/* ============================================================
 * GLOBALS
 * ============================================================ */

static HWND  g_hwnd;
static int   g_screen     = SCR_WELCOME;
static int   g_hover_card = -1;

static HFONT g_fntTitle;
static HFONT g_fntHeading;
static HFONT g_fntBody;
static HFONT g_fntBtnBig;
static HFONT g_fntBtnCard;
static HFONT g_fntSmall;

static HBITMAP g_hLogo  = NULL;
static int     g_logoW  = 0;
static int     g_logoH  = 0;

static HICON   g_hIconBig = NULL;
static HICON   g_hIconSm  = NULL;

/* Merge screen */
static HWND  g_hDropArea, g_hDropLabel, g_hEditOutName;
static HWND  g_hChkDelZip, g_hChkOpenDir, g_hBtnRunMerge, g_hBtnBack;
static char  g_zipPath[MAX_PATH] = "";

/* Merge screen — file-order list */
static HWND       g_hListMergePDFs = NULL;
static HWND       g_hBtnMergeUp    = NULL;
static HWND       g_hBtnMergeDn    = NULL;
static HIMAGELIST g_hMergeImgList  = NULL;
static int        g_merge_sort_dir = -1;  /* -1=custom, 1=A-Z, 0=Z-A */

/* Split screen */
static HWND  g_hDropSplit, g_hDropLabelSplit, g_hEditSplitDir;
static HWND  g_hBtnSplitBrowse, g_hBtnRunSplit, g_hBtnBackSplit;
static HWND  g_hEditSplitName;   /* optional custom base name for split pages */
static char  g_splitPdfPath[MAX_PATH] = "";

/* Arrange screen */
static HWND  g_hListPDFs;
static HWND  g_hBtnAddPDFs;
static HWND  g_hBtnArrUp    = NULL;   /* single ↑ / ↓ / ✕ for selected row */
static HWND  g_hBtnArrDn    = NULL;
static HWND  g_hBtnArrX     = NULL;
static HWND  g_hBtnArrClear = NULL;   /* clears all files from the list */
static HWND  g_hEditArrOut, g_hChkArrOpen, g_hBtnRunArr, g_hBtnBackArr;
static HIMAGELIST g_hArrImgList = NULL;

/* ============================================================
 * THEME
 * ============================================================ */

static void rebuild_brushes(void)
{
    if (g_hBrBg)    { DeleteObject(g_hBrBg);    g_hBrBg    = NULL; }
    if (g_hBrSubBg) { DeleteObject(g_hBrSubBg); g_hBrSubBg = NULL; }
    if (g_hBrEdit)  { DeleteObject(g_hBrEdit);  g_hBrEdit  = NULL; }
    g_hBrBg    = CreateSolidBrush(C_BG);
    g_hBrSubBg = CreateSolidBrush(C_SUB_BG);
    g_hBrEdit  = CreateSolidBrush(C_EDIT_BG);
}

static void setup_theme(void)
{
    /* Read AppsUseLightTheme from registry (0 = dark, 1 = light, missing = light) */
    DWORD val = 1, sz = sizeof(val);
    RegGetValueA(HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        "AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &val, &sz);
    g_dark = (val == 0);

    if (g_dark) {
        C_BG         = RGB( 15,  17,  26);
        C_CARD       = RGB( 26,  29,  45);
        C_CARD_HOV   = RGB( 33,  37,  58);
        C_SUB_BG     = RGB( 20,  23,  36);
        C_EDIT_BG    = RGB( 30,  34,  52);
        C_ACCENT     = RGB( 82, 145, 255);
        C_ACCENT_DIM = RGB( 55, 105, 200);
        C_BORDER     = RGB( 48,  54,  82);
        C_BORDER_HOV = RGB( 82, 145, 255);
        C_SHADOW     = RGB(  6,   7,  14);
        C_TEXT       = RGB(222, 228, 245);
        C_SUBTEXT    = RGB(148, 160, 192);
        C_FOOTER     = RGB( 95, 110, 148);
        C_DROP_FILL  = RGB( 18,  38,  72);   /* visibly blue-tinted panel */
        C_DROP_OUTER = RGB( 58,  80, 145);
        C_DROP_DASH  = RGB( 90, 120, 200);
        C_DROP_TEXT  = RGB(130, 148, 188);
        C_BTN_FG     = RGB(255, 255, 255);
    } else {
        C_BG         = RGB(238, 242, 252);
        C_CARD       = RGB(255, 255, 255);
        C_CARD_HOV   = RGB(246, 250, 255);
        C_SUB_BG     = RGB(251, 252, 255);
        C_EDIT_BG    = RGB(255, 255, 255);
        C_ACCENT     = RGB( 41,  98, 220);
        C_ACCENT_DIM = RGB( 28,  70, 170);
        C_BORDER     = RGB(208, 218, 242);
        C_BORDER_HOV = RGB( 41,  98, 220);
        C_SHADOW     = RGB(190, 202, 228);
        C_TEXT       = RGB( 13,  20,  42);
        C_SUBTEXT    = RGB( 86, 100, 134);
        C_FOOTER     = RGB(125, 140, 172);
        C_DROP_FILL  = RGB(228, 234, 248);   /* visible light grey-blue panel */
        C_DROP_OUTER = RGB(175, 190, 225);
        C_DROP_DASH  = RGB(120, 140, 195);
        C_DROP_TEXT  = RGB(120, 138, 180);
        C_BTN_FG     = RGB(255, 255, 255);
    }

    rebuild_brushes();
}

static void apply_dark_titlebar(HWND hwnd)
{
    BOOL dark = (BOOL)g_dark;
    /* Try attribute 20 (Win10 20H1+), fall back to 19 */
    if (FAILED(DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE,
                                     &dark, sizeof(dark))))
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
}

static void apply_listview_theme(void)
{
    if (g_hListPDFs) {
        ListView_SetBkColor(g_hListPDFs,     C_SUB_BG);
        ListView_SetTextBkColor(g_hListPDFs, C_SUB_BG);
        ListView_SetTextColor(g_hListPDFs,   C_TEXT);
        InvalidateRect(g_hListPDFs, NULL, TRUE);
    }
    if (g_hListMergePDFs) {
        ListView_SetBkColor(g_hListMergePDFs,     C_SUB_BG);
        ListView_SetTextBkColor(g_hListMergePDFs, C_SUB_BG);
        ListView_SetTextColor(g_hListMergePDFs,   C_TEXT);
        InvalidateRect(g_hListMergePDFs, NULL, TRUE);
    }
}

/* ============================================================
 * UTILITY
 * ============================================================ */

static void set_font_all(HWND hwnd, HFONT fnt)
{
    SendMessage(hwnd, WM_SETFONT, (WPARAM)fnt, TRUE);
}

static HFONT make_font(int pt, int bold, int italic)
{
    return CreateFont(
        -MulDiv(pt, GetDeviceCaps(GetDC(NULL), LOGPIXELSY), 72),
        0, 0, 0, bold ? FW_BOLD : FW_NORMAL, italic, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
}

static RECT control_rect(HWND hw)
{
    RECT r;
    GetWindowRect(hw, &r);
    MapWindowPoints(HWND_DESKTOP, g_hwnd, (POINT *)&r, 2);
    return r;
}

/* Darken a COLORREF by a percentage (0-100) */
static COLORREF darken(COLORREF c, int pct)
{
    int r = MulDiv(GetRValue(c), 100 - pct, 100);
    int g = MulDiv(GetGValue(c), 100 - pct, 100);
    int b = MulDiv(GetBValue(c), 100 - pct, 100);
    return RGB(r, g, b);
}

/* ============================================================
 * EMBEDDED RESOURCE HELPERS
 * ============================================================ */

static HBITMAP load_png_resource_bitmap(HINSTANCE hInst, int res_id,
                                        int *w_out, int *h_out)
{
    HRSRC   hRes  = FindResourceA(hInst, MAKEINTRESOURCE(res_id), RT_RCDATA);
    if (!hRes) return NULL;
    HGLOBAL hGlob = LoadResource(hInst, hRes);
    if (!hGlob) return NULL;
    const void *data = LockResource(hGlob);
    DWORD       sz   = SizeofResource(hInst, hRes);
    if (!data || !sz) return NULL;

    int w, h, ch;
    unsigned char *rgba = stbi_load_from_memory(
        (const stbi_uc *)data, (int)sz, &w, &h, &ch, 4);
    if (!rgba) return NULL;

    for (int i = 0; i < w * h; i++) {
        unsigned char r = rgba[i*4];
        rgba[i*4]   = rgba[i*4+2];
        rgba[i*4+2] = r;
    }

    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       =  w;
    bmi.bmiHeader.biHeight      = -h;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC     hdc = GetDC(NULL);
    HBITMAP hb  = CreateDIBitmap(hdc, &bmi.bmiHeader, CBM_INIT,
                                  rgba, &bmi, DIB_RGB_COLORS);
    ReleaseDC(NULL, hdc);
    stbi_image_free(rgba);
    if (w_out) *w_out = w;
    if (h_out) *h_out = h;
    return hb;
}

static void load_logo(HINSTANCE hInst)
{
    g_hLogo = load_png_resource_bitmap(hInst, IDR_LOGO_PNG, &g_logoW, &g_logoH);
}

/* ============================================================
 * LAYOUT — card geometry
 * ============================================================ */

#define WELCOME_HEADER_H  118
#define WELCOME_FOOTER_H   36
#define CARD_GAP           22
#define CARD_MARGIN        40

static void get_card_rects(RECT rects[4])
{
    RECT client;
    GetClientRect(g_hwnd, &client);
    int cw = client.right, ch = client.bottom;

    int card_w = (cw - 2 * CARD_MARGIN - CARD_GAP) / 2;
    int avail_h = ch - WELCOME_HEADER_H - WELCOME_FOOTER_H - CARD_GAP - 8;
    int card_h  = avail_h / 2;
    if (card_w < 250) card_w = 250;
    if (card_h < 150) card_h = 150;

    int start_x = (cw - (2 * card_w + CARD_GAP)) / 2;
    int start_y = WELCOME_HEADER_H;

    for (int i = 0; i < 4; i++) {
        int col = i % 2, row = i / 2;
        rects[i].left   = start_x + col * (card_w + CARD_GAP);
        rects[i].top    = start_y + row * (card_h + CARD_GAP);
        rects[i].right  = rects[i].left + card_w;
        rects[i].bottom = rects[i].top  + card_h;
    }
}

/* ============================================================
 * REPOSITION CHILD CONTROLS
 * ============================================================ */

static void reposition_controls(int cw, int ch)
{
    int mx = 60, drop_w = cw - mx * 2;

    /* Merge — compact ZIP bar at top, sortable file list below */
    {
        int drop_y  = 120, drop_h = 100;          /* compact ZIP source bar      */
        int list_y  = drop_y + drop_h + 8;        /* file-order ListView         */
        int list_h  = ch - list_y - 196;
        if (list_h > 220) list_h = 220;
        if (list_h < 80)  list_h = 80;
        int add_y   = list_y + list_h + 6;        /* row with ↑/↓ buttons        */
        int out_y   = add_y  + 36;                /* Output PDF Name             */
        int chk_y   = out_y  + 32;                /* checkboxes                  */
        int run_y   = chk_y  + 56;                /* MERGE button                */
        int edit_x  = mx + 152, edit_w = cw - edit_x - mx;
        int chk_w   = 280, chk_x = (cw - chk_w) / 2;
        int btn3_r  = mx + drop_w;               /* right edge of the list area  */

        MoveWindow(g_hDropArea,      mx, drop_y, drop_w, drop_h, TRUE);
        MoveWindow(g_hDropLabel,     mx, drop_y, drop_w, drop_h, TRUE);
        MoveWindow(g_hListMergePDFs, mx, list_y, drop_w, list_h, TRUE);
        ListView_SetColumnWidth(g_hListMergePDFs, 0, drop_w - 4);
        MoveWindow(g_hBtnMergeDn,    btn3_r-36,    add_y, 32, 28, TRUE);  /* ↓ */
        MoveWindow(g_hBtnMergeUp,    btn3_r-36-38, add_y, 32, 28, TRUE);  /* ↑ */
        MoveWindow(g_hEditOutName,   edit_x, out_y, edit_w, 26, TRUE);
        MoveWindow(g_hChkDelZip,     chk_x, chk_y,      chk_w, 22, TRUE);
        MoveWindow(g_hChkOpenDir,    chk_x, chk_y + 28, chk_w, 22, TRUE);
        MoveWindow(g_hBtnRunMerge,   (cw-200)/2, run_y, 200, 44, TRUE);
        MoveWindow(g_hBtnBack,       14, 14, 80, 30, TRUE);
    }

    /* Split */
    {
        int drop_y  = 80, drop_h = 110;
        int dir_y   = drop_y + drop_h + 14;          /* Output Folder row */
        int name_y  = dir_y + 34;                    /* Page Name row     */
        int btn_y   = name_y + 40;                   /* SPLIT button      */
        int edit_x  = mx + 130, edit_w = cw - edit_x - mx - 96;
        MoveWindow(g_hDropSplit,      mx, drop_y, drop_w, drop_h, TRUE);
        MoveWindow(g_hDropLabelSplit, mx, drop_y, drop_w, drop_h, TRUE);
        MoveWindow(g_hEditSplitDir,   edit_x, dir_y,  edit_w, 26, TRUE);
        MoveWindow(g_hBtnSplitBrowse, edit_x+edit_w+6, dir_y, 84, 26, TRUE);
        MoveWindow(g_hEditSplitName,  edit_x, name_y, edit_w+90, 26, TRUE);
        MoveWindow(g_hBtnRunSplit,    (cw-200)/2, btn_y, 200, 44, TRUE);
        MoveWindow(g_hBtnBackSplit,   14, 14, 80, 30, TRUE);
    }

    /* Arrange */
    {
        int list_y = 92;  /* below heading + subtitle */
        /* Cap list so MERGE button is always visible */
        int list_h = ch - 250; if (list_h > 290) list_h = 290;
                                if (list_h < 100) list_h = 100;
        int add_y   = list_y + list_h + 8;
        int out_y   = add_y  + 40;
        int chk_y   = out_y  + 32;
        int run_y   = chk_y  + 30;
        int edit_x  = mx + 152, edit_w = cw - edit_x - mx;
        int chk_w   = 280,      chk_x  = (cw - chk_w) / 2;
        MoveWindow(g_hListPDFs,   mx, list_y, drop_w, list_h, TRUE);
        ListView_SetColumnWidth(g_hListPDFs, 0, drop_w - 4);
        /* Add Files on the left, ↑ ↓ ✕ grouped on the right of that row */
        MoveWindow(g_hBtnAddPDFs,  mx,      add_y, 130, 32, TRUE);
        MoveWindow(g_hBtnArrClear, mx+136,  add_y, 100, 32, TRUE);
        int btn3_r = mx + drop_w;  /* right edge of the list */
        MoveWindow(g_hBtnArrX,    btn3_r-36,       add_y, 32, 32, TRUE);
        MoveWindow(g_hBtnArrDn,   btn3_r-36-38,    add_y, 32, 32, TRUE);
        MoveWindow(g_hBtnArrUp,   btn3_r-36-38-38, add_y, 32, 32, TRUE);
        MoveWindow(g_hEditArrOut, edit_x, out_y, edit_w, 26, TRUE);
        MoveWindow(g_hChkArrOpen, chk_x, chk_y, chk_w, 22, TRUE);
        MoveWindow(g_hBtnRunArr,  (cw-200)/2, run_y, 200, 44, TRUE);
        MoveWindow(g_hBtnBackArr, 14, 14, 80, 30, TRUE);
    }

    InvalidateRect(g_hwnd, NULL, TRUE);
}

/* ============================================================
 * SHOW / HIDE SCREENS
 * ============================================================ */

static void hide_all_screens(void)
{
    HWND ctrls[] = {
        g_hDropArea, g_hDropLabel, g_hEditOutName, g_hChkDelZip,
        g_hChkOpenDir, g_hBtnRunMerge, g_hBtnBack,
        g_hListMergePDFs, g_hBtnMergeUp, g_hBtnMergeDn,
        g_hDropSplit, g_hDropLabelSplit, g_hEditSplitDir,
        g_hBtnSplitBrowse, g_hBtnRunSplit, g_hBtnBackSplit, g_hEditSplitName,
        g_hListPDFs, g_hBtnAddPDFs, g_hBtnArrClear,
        g_hBtnArrUp, g_hBtnArrDn, g_hBtnArrX,
        g_hEditArrOut, g_hChkArrOpen,
        g_hBtnRunArr, g_hBtnBackArr, NULL
    };
    for (int i = 0; ctrls[i]; i++) ShowWindow(ctrls[i], SW_HIDE);
    InvalidateRect(g_hwnd, NULL, TRUE);
}

static void show_screen(int scr)
{
    hide_all_screens();
    g_screen = scr;
    switch (scr) {
    case SCR_MERGE:
        ShowWindow(g_hDropArea,      SW_SHOW); ShowWindow(g_hDropLabel,      SW_SHOW);
        ShowWindow(g_hListMergePDFs, SW_SHOW); ShowWindow(g_hBtnMergeUp, SW_SHOW);
        ShowWindow(g_hBtnMergeDn,    SW_SHOW);
        ShowWindow(g_hEditOutName,   SW_SHOW); ShowWindow(g_hChkDelZip,      SW_SHOW);
        ShowWindow(g_hChkOpenDir,    SW_SHOW); ShowWindow(g_hBtnRunMerge,    SW_SHOW);
        ShowWindow(g_hBtnBack,       SW_SHOW);
        apply_listview_theme();
        break;
    case SCR_SPLIT:
        ShowWindow(g_hDropSplit,      SW_SHOW); ShowWindow(g_hDropLabelSplit, SW_SHOW);
        ShowWindow(g_hEditSplitDir,   SW_SHOW); ShowWindow(g_hBtnSplitBrowse, SW_SHOW);
        ShowWindow(g_hEditSplitName,  SW_SHOW);
        ShowWindow(g_hBtnRunSplit,    SW_SHOW); ShowWindow(g_hBtnBackSplit,   SW_SHOW);
        break;
    case SCR_ARRANGE:
        ShowWindow(g_hListPDFs,    SW_SHOW); ShowWindow(g_hBtnAddPDFs,   SW_SHOW);
        ShowWindow(g_hBtnArrClear, SW_SHOW);
        ShowWindow(g_hBtnArrUp,    SW_SHOW); ShowWindow(g_hBtnArrDn,     SW_SHOW);
        ShowWindow(g_hBtnArrX,     SW_SHOW);
        ShowWindow(g_hEditArrOut, SW_SHOW); ShowWindow(g_hChkArrOpen, SW_SHOW);
        ShowWindow(g_hBtnRunArr,  SW_SHOW); ShowWindow(g_hBtnBackArr, SW_SHOW);
        apply_listview_theme();
        break;
    default: break;
    }
    RECT r; GetClientRect(g_hwnd, &r);
    reposition_controls(r.right, r.bottom);
}

/* ============================================================
 * PAINT HELPERS
 * ============================================================ */

static void fill_rounded(HDC hdc, RECT *r, int rad, COLORREF fill)
{
    HBRUSH hbr = CreateSolidBrush(fill);
    HPEN   pnull = CreatePen(PS_NULL, 0, 0);
    HGDIOBJ ob = SelectObject(hdc, hbr);
    HGDIOBJ op = SelectObject(hdc, pnull);
    RoundRect(hdc, r->left, r->top, r->right, r->bottom, rad, rad);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(hbr); DeleteObject(pnull);
}

static void stroke_rounded(HDC hdc, RECT *r, int rad, COLORREF stroke, int w)
{
    HPEN   hpn = CreatePen(PS_SOLID, w, stroke);
    HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    HGDIOBJ op = SelectObject(hdc, hpn);
    RoundRect(hdc, r->left, r->top, r->right, r->bottom, rad, rad);
    SelectObject(hdc, ob); SelectObject(hdc, op);
    DeleteObject(hpn);
}

/* Draw the drag-and-drop zone — layered inset panel with depth */
static void draw_drop_zone(HDC hdc, RECT *r)
{
    int rad = 12;

    /* ── Layer 1: outer drop shadow (slightly offset darker rounded rect) ── */
    RECT sh = {r->left+3, r->top+3, r->right+3, r->bottom+3};
    fill_rounded(hdc, &sh, rad, C_SHADOW);

    /* ── Layer 2: main fill ── */
    fill_rounded(hdc, r, rad, C_DROP_FILL);

    /* ── Layer 3: 2px solid outer border (clearly visible) ── */
    stroke_rounded(hdc, r, rad, C_DROP_OUTER, 2);

    /* ── Layer 4: inner bevel highlight (1px, just inside outer border) ── */
    {
        COLORREF bevel = g_dark ? RGB(38, 75, 145) : RGB(190, 212, 250);
        RECT bev = {r->left+3, r->top+3, r->right-3, r->bottom-3};
        stroke_rounded(hdc, &bev, rad-2, bevel, 1);
    }

    /* ── Layer 5: inner dashed border (inset 10px) ── */
    {
        LOGBRUSH lb = {BS_SOLID, C_DROP_DASH, 0};
        HPEN dsh = ExtCreatePen(PS_GEOMETRIC|PS_DASH|PS_ENDCAP_FLAT, 1, &lb, 0, NULL);
        HGDIOBJ op = SelectObject(hdc, dsh);
        HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        RoundRect(hdc, r->left+10, r->top+10, r->right-10, r->bottom-10, rad-2, rad-2);
        SelectObject(hdc, op); SelectObject(hdc, ob);
        DeleteObject(dsh);
    }

    /* ── Layer 6: upload arrow icon (shifted up so icon+text centre in box) ── */
    {
        int cx = (r->left + r->right)  / 2;
        int cy = (r->top  + r->bottom) / 2 - 26; /* -26 instead of -16 */
        HPEN ip = CreatePen(PS_SOLID, 2, C_DROP_DASH);
        HGDIOBJ op = SelectObject(hdc, ip);
        MoveToEx(hdc, cx,    cy+13, NULL); LineTo(hdc, cx,   cy);       /* shaft   */
        MoveToEx(hdc, cx-8,  cy+8,  NULL); LineTo(hdc, cx,   cy);       /* left arm*/
        MoveToEx(hdc, cx,    cy,    NULL); LineTo(hdc, cx+8, cy+8);     /* right arm*/
        MoveToEx(hdc, cx-10, cy+16, NULL); LineTo(hdc, cx+10, cy+16);   /* base    */
        SelectObject(hdc, op);
        DeleteObject(ip);
    }
}

/* Draw one welcome card */
static void draw_card(HDC hdc, RECT *r, const char *label, const char *sub,
                      COLORREF accent, int hovered)
{
    int rad = 14;

    /* Drop shadow */
    RECT sh = { r->left+4, r->top+5, r->right+4, r->bottom+5 };
    fill_rounded(hdc, &sh, rad, C_SHADOW);

    /* Card body */
    fill_rounded(hdc, r, rad, hovered ? C_CARD_HOV : C_CARD);

    /* Hover / focus border */
    if (hovered)
        stroke_rounded(hdc, r, rad, C_ACCENT, 2);
    else
        stroke_rounded(hdc, r, rad, C_BORDER, 1);

    /* Accent pill at top-left */
    RECT pill = { r->left+16, r->top+18, r->left+52, r->top+23 };
    fill_rounded(hdc, &pill, 3, accent);

    /* Filled circle indicator */
    {
        int cx = r->left + 26, cy = r->top + 50, cr = 9;
        HBRUSH hcb = CreateSolidBrush(accent);
        HPEN   pn0 = CreatePen(PS_NULL, 0, 0);
        HGDIOBJ oh = SelectObject(hdc, hcb);
        HGDIOBJ op = SelectObject(hdc, pn0);
        Ellipse(hdc, cx-cr, cy-cr, cx+cr, cy+cr);
        SelectObject(hdc, oh); SelectObject(hdc, op);
        DeleteObject(hcb); DeleteObject(pn0);
    }

    SetBkMode(hdc, TRANSPARENT);

    /* Title */
    HGDIOBJ oldf = SelectObject(hdc, g_fntBtnCard);
    SetTextColor(hdc, C_TEXT);
    RECT tr = { r->left+44, r->top+38, r->right-12, r->top+66 };
    DrawTextA(hdc, label, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    /* Description */
    SelectObject(hdc, g_fntBody);
    SetTextColor(hdc, C_SUBTEXT);
    RECT dr = { r->left+18, r->top+74, r->right-14, r->bottom-16 };
    DrawTextA(hdc, sub, -1, &dr, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

    SelectObject(hdc, oldf);
}

/* ============================================================
 * OWNER-DRAW HELPERS  (called from WM_DRAWITEM)
 * ============================================================ */

static void draw_action_btn(DRAWITEMSTRUCT *dis, const char *label)
{
    HDC  hdc  = dis->hDC;
    RECT r    = dis->rcItem;
    BOOL down = (dis->itemState & ODS_SELECTED) != 0;
    BOOL focus= (dis->itemState & ODS_FOCUS)    != 0;

    COLORREF bg = down ? C_ACCENT_DIM : C_ACCENT;

    /* Pill shape — radius = half height */
    int rad = (r.bottom - r.top);
    fill_rounded(hdc, &r, rad, bg);

    /* Subtle top highlight (1px lighter line on top half) for depth */
    if (!down) {
        HPEN hpn = CreatePen(PS_SOLID, 1,
            RGB(min(255, GetRValue(bg)+40),
                min(255, GetGValue(bg)+30),
                min(255, GetBValue(bg)+20)));
        HGDIOBJ op = SelectObject(hdc, hpn);
        HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        RoundRect(hdc, r.left+1, r.top+1, r.right-1, r.bottom-1, rad, rad);
        SelectObject(hdc, op); SelectObject(hdc, ob);
        DeleteObject(hpn);
    }

    /* Label */
    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, g_fntBtnBig);
    SetTextColor(hdc, C_BTN_FG);
    RECT tr = r;
    if (down) { tr.top += 1; tr.left += 1; } /* press offset */
    DrawTextA(hdc, label, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    /* Focus ring */
    if (focus) {
        RECT fr = { r.left+3, r.top+3, r.right-3, r.bottom-3 };
        DrawFocusRect(hdc, &fr);
    }
}

static void draw_back_btn(DRAWITEMSTRUCT *dis)
{
    HDC  hdc  = dis->hDC;
    RECT r    = dis->rcItem;
    BOOL down = (dis->itemState & ODS_SELECTED) != 0;

    /* Flat background — matches screen bg */
    HBRUSH hbr = CreateSolidBrush(down ? darken(C_SUB_BG, 6) : C_SUB_BG);
    FillRect(hdc, &r, hbr);
    DeleteObject(hbr);

    /* Thin rounded border */
    stroke_rounded(hdc, &r, 6, C_BORDER, 1);

    SetBkMode(hdc, TRANSPARENT);

    /* Draw left-arrow chevron */
    int cy  = (r.top + r.bottom) / 2;
    int ax  = r.left + 14;
    int ahs = 5; /* arrowhead size */

    HPEN hpn = CreatePen(PS_SOLID, 2, C_ACCENT);
    HGDIOBJ op = SelectObject(hdc, hpn);
    MoveToEx(hdc, ax + ahs, cy - ahs, NULL);
    LineTo  (hdc, ax,       cy);
    LineTo  (hdc, ax + ahs, cy + ahs);
    MoveToEx(hdc, ax,       cy, NULL);
    LineTo  (hdc, ax + 16,  cy);
    SelectObject(hdc, op);
    DeleteObject(hpn);

    /* "Back" text */
    SelectObject(hdc, g_fntBody);
    SetTextColor(hdc, C_ACCENT);
    RECT tr = { r.left + 32, r.top, r.right - 4, r.bottom };
    DrawTextA(hdc, "Back", -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

/* ============================================================
 * PAINT — WELCOME SCREEN
 * ============================================================ */

static void on_paint_welcome(HDC hdc, RECT *client)
{
    int cw = client->right, ch = client->bottom;

    HBRUSH hbg = CreateSolidBrush(C_BG);
    FillRect(hdc, client, hbg);
    DeleteObject(hbg);

    SetBkMode(hdc, TRANSPARENT);

    /* Title */
    HGDIOBJ oldf = SelectObject(hdc, g_fntTitle);
    SetTextColor(hdc, C_TEXT);
    RECT tr = { 0, 18, cw, 64 };
    DrawTextA(hdc, APP_TITLE, -1, &tr, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    SelectObject(hdc, oldf);

    /* Accent divider */
    {
        int lw = (cw < 520) ? cw - 80 : 440;
        HPEN hpn = CreatePen(PS_SOLID, 2, C_ACCENT);
        HGDIOBJ opn = SelectObject(hdc, hpn);
        MoveToEx(hdc, (cw-lw)/2, 72, NULL);
        LineTo  (hdc, (cw+lw)/2, 72);
        SelectObject(hdc, opn);
        DeleteObject(hpn);
    }

    /* Subtitle */
    oldf = SelectObject(hdc, g_fntBody);
    SetTextColor(hdc, C_SUBTEXT);
    RECT sub = { 0, 78, cw, 108 };
    DrawTextA(hdc, "Select a tool to get started", -1, &sub,
              DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    SelectObject(hdc, oldf);

    /* Cards */
    struct { const char *label; const char *sub; COLORREF accent; } cards[4] = {
        { "Merge PDF",      "Combine all PDFs inside a ZIP\nfile into one document.",
          RGB( 34, 148,  83) },
        { "Split PDF",      "Extract each page of a PDF\ninto its own separate file.",
          RGB( 41,  98, 220) },
        { "Arrange & Merge","Add individual PDFs, set the\norder, then merge.",
          RGB(148,  48, 185) },
        { "Coming Soon",    "Additional tools will be\nadded in a future update.",
          RGB(148, 158, 175) },
    };

    RECT rects[4];
    get_card_rects(rects);
    for (int i = 0; i < 4; i++)
        draw_card(hdc, &rects[i], cards[i].label, cards[i].sub,
                  cards[i].accent, g_hover_card == i);

    /* Footer separator */
    {
        HPEN hpn = CreatePen(PS_SOLID, 1, C_BORDER);
        HGDIOBJ opn = SelectObject(hdc, hpn);
        MoveToEx(hdc, 40, ch - WELCOME_FOOTER_H, NULL);
        LineTo  (hdc, cw-40, ch - WELCOME_FOOTER_H);
        SelectObject(hdc, opn); DeleteObject(hpn);
    }

    oldf = SelectObject(hdc, g_fntSmall);
    SetTextColor(hdc, C_FOOTER);
    RECT fr = { 0, ch - WELCOME_FOOTER_H, cw, ch };
    DrawTextA(hdc, "Created by Ravi Patel", -1, &fr,
              DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    SelectObject(hdc, oldf);
}

/* ============================================================
 * PAINT — MERGE SCREEN
 * ============================================================ */

static void on_paint_merge(HDC hdc, RECT *client)
{
    int cw = client->right;

    HBRUSH hbg = CreateSolidBrush(C_SUB_BG);
    FillRect(hdc, client, hbg);
    DeleteObject(hbg);

    SetBkMode(hdc, TRANSPARENT);

    /* Logo — white rounded panel behind it */
    {
        int max_h = 68, max_w = 300, pad_x = 24, pad_y = 8;
        int dh = g_hLogo ? g_logoH : 0, dw = g_hLogo ? g_logoW : 0;
        if (dh > max_h) { dw = dw*max_h/dh; dh = max_h; }
        if (dw > max_w) { dh = dh*max_w/dw; dw = max_w; }
        int lx = (cw-dw)/2, ly = 10;

        RECT bg = { lx-pad_x, ly-pad_y,
                    lx+dw+pad_x, ly+(dh>0?dh:max_h)+pad_y };
        fill_rounded(hdc, &bg, 10, RGB(255,255,255));
        stroke_rounded(hdc, &bg, 10, C_BORDER, 1);

        if (g_hLogo) {
            HDC mdc = CreateCompatibleDC(hdc);
            HGDIOBJ old = SelectObject(mdc, g_hLogo);
            SetStretchBltMode(hdc, HALFTONE);
            StretchBlt(hdc, lx, ly, dw, dh, mdc, 0, 0, g_logoW, g_logoH, SRCCOPY);
            SelectObject(mdc, old); DeleteDC(mdc);
        }
    }

    /* Heading */
    HGDIOBJ oldf = SelectObject(hdc, g_fntHeading);
    SetTextColor(hdc, C_TEXT);
    RECT hr = { 0, 88, cw, 118 };
    DrawTextA(hdc, "Merge PDFs Inside a Zip File", -1, &hr,
              DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    SelectObject(hdc, oldf);

    /* Compact ZIP source bar — border + single-line text, no icon */
    RECT dr = control_rect(g_hDropArea);
    draw_drop_zone(hdc, &dr);
    {
        /* Text drawn in the lower portion of the compact bar (below icon area) */
        RECT tr = {dr.left+16, dr.top+18, dr.right-16, dr.bottom-4};
        SetBkMode(hdc, TRANSPARENT);
        SelectObject(hdc, g_fntBody);
        if (g_zipPath[0]) {
            char label[MAX_PATH + 8];
            snprintf(label, sizeof(label), "ZIP: %s", PathFindFileNameA(g_zipPath));
            SetTextColor(hdc, C_TEXT);
            DrawTextA(hdc, label, -1, &tr,
                DT_CENTER|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
        } else {
            SetTextColor(hdc, C_DROP_TEXT);
            DrawTextA(hdc, "Drop ZIP file here  \xb7\xb7\xb7  or click to browse",
                -1, &tr, DT_CENTER|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
        }
    }

    /* "Output PDF Name:" label — positioned relative to the edit control */
    RECT er = control_rect(g_hEditOutName);
    oldf = SelectObject(hdc, g_fntBody);
    SetTextColor(hdc, C_TEXT);
    RECT lbl = { 60, er.top, er.left-4, er.bottom };
    DrawTextA(hdc, "Output PDF Name:", -1, &lbl, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldf);
}

/* ============================================================
 * PAINT — SPLIT SCREEN
 * ============================================================ */

static void on_paint_split(HDC hdc, RECT *client)
{
    int cw = client->right;

    HBRUSH hbg = CreateSolidBrush(C_SUB_BG);
    FillRect(hdc, client, hbg);
    DeleteObject(hbg);

    SetBkMode(hdc, TRANSPARENT);

    HGDIOBJ oldf = SelectObject(hdc, g_fntHeading);
    SetTextColor(hdc, C_TEXT);
    RECT hr = { 0, 28, cw, 64 };
    DrawTextA(hdc, "Split PDF into Individual Pages", -1, &hr,
              DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    SelectObject(hdc, oldf);

    RECT dr = control_rect(g_hDropSplit);
    draw_drop_zone(hdc, &dr);

    /* Hint / selected-file text — centred inside drop zone */
    {
        int zone_cy = (dr.top + dr.bottom) / 2;
        int text_top = zone_cy - 2;
        RECT tr = {dr.left+20, text_top, dr.right-20, dr.bottom-8};
        SetBkMode(hdc, TRANSPARENT);
        SelectObject(hdc, g_fntBody);
        if (g_splitPdfPath[0]) {
            SetTextColor(hdc, C_TEXT);
            DrawTextA(hdc, PathFindFileNameA(g_splitPdfPath), -1, &tr,
                DT_CENTER|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
        } else {
            SetTextColor(hdc, C_DROP_TEXT);
            DrawTextA(hdc, "Drag and drop PDF file here\nor click to browse...", -1, &tr,
                DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);
        }
    }

    oldf = SelectObject(hdc, g_fntBody);
    SetTextColor(hdc, C_TEXT);

    RECT er = control_rect(g_hEditSplitDir);
    RECT lbl = { 60, er.top, er.left-4, er.bottom };
    DrawTextA(hdc, "Output Folder:", -1, &lbl, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    RECT nr = control_rect(g_hEditSplitName);
    RECT nlbl = { 60, nr.top, nr.left-4, nr.bottom };
    DrawTextA(hdc, "File Name:", -1, &nlbl, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, oldf);
}

/* ============================================================
 * PAINT — ARRANGE SCREEN
 * ============================================================ */

static void on_paint_arrange(HDC hdc, RECT *client)
{
    int cw = client->right;

    HBRUSH hbg = CreateSolidBrush(C_SUB_BG);
    FillRect(hdc, client, hbg);
    DeleteObject(hbg);

    SetBkMode(hdc, TRANSPARENT);

    HGDIOBJ oldf = SelectObject(hdc, g_fntHeading);
    SetTextColor(hdc, C_TEXT);
    RECT hr = { 0, 18, cw, 56 };
    DrawTextA(hdc, "Arrange & Merge PDFs", -1, &hr,
              DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    oldf = SelectObject(hdc, g_fntBody);
    SetTextColor(hdc, C_SUBTEXT);
    RECT sr = { 0, 56, cw, 78 };
    DrawTextA(hdc, "Add PDF files, set the order, then merge.", -1, &sr,
              DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);

    RECT er = control_rect(g_hEditArrOut);
    SetTextColor(hdc, C_TEXT);
    RECT lbl = { 60, er.top, er.left-4, er.bottom };
    DrawTextA(hdc, "Output PDF Name:", -1, &lbl,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, oldf);
}

/* ============================================================
 * CREATE CHILD CONTROLS
 * ============================================================ */

static HWND mk_btn(HWND parent, int id, const char *text,
                   int x, int y, int w, int h, HFONT fnt)
{
    HWND hw = CreateWindowA("BUTTON", text,
        WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, NULL, NULL);
    set_font_all(hw, fnt);
    return hw;
}

/* Owner-drawn action button (MERGE / SPLIT) */
static HWND mk_action_btn(HWND parent, int id, const char *text,
                           int x, int y, int w, int h)
{
    HWND hw = CreateWindowA("BUTTON", text,
        WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, NULL, NULL);
    set_font_all(hw, g_fntBtnBig);
    return hw;
}

/* Owner-drawn back button */
static HWND mk_back_btn(HWND parent, int id, int x, int y, int w, int h)
{
    HWND hw = CreateWindowA("BUTTON", "",
        WS_CHILD | WS_TABSTOP | BS_OWNERDRAW,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, NULL, NULL);
    set_font_all(hw, g_fntBody);
    return hw;
}

static HWND mk_check(HWND parent, int id, const char *text,
                     int x, int y, int w, int h)
{
    HWND hw = CreateWindowA("BUTTON", text,
        WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, NULL, NULL);
    set_font_all(hw, g_fntBody);
    SendMessage(hw, BM_SETCHECK, BST_UNCHECKED, 0);
    return hw;
}

static HWND mk_edit(HWND parent, int id, int x, int y, int w, int h)
{
    HWND hw = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
        WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, NULL, NULL);
    set_font_all(hw, g_fntBody);
    return hw;
}

static HWND mk_drop_area(HWND parent, int id, int x, int y, int w, int h)
{
    /* Empty text — hint text is drawn in on_paint_merge / on_paint_split
     * so it can be properly vertically centred inside the painted drop zone. */
    HWND hw = CreateWindowExA(
        WS_EX_TRANSPARENT, "STATIC", "",
        WS_CHILD | WS_TABSTOP | SS_CENTER | SS_NOTIFY,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, NULL, NULL);
    set_font_all(hw, g_fntBody);
    return hw;
}

static void create_all_controls(HWND hwnd)
{
    /* Merge */
    g_hDropArea    = mk_drop_area(hwnd, ID_DROP_AREA,    80,130,740,128);
    g_hDropLabel   = CreateWindowA("STATIC","",WS_CHILD|SS_CENTER,
                                   80,130,740,128,hwnd,NULL,NULL,NULL);
    set_font_all(g_hDropLabel, g_fntBody);
    g_hEditOutName = mk_edit(hwnd, ID_EDIT_OUTNAME, 232,272,488,26);
    g_hChkDelZip   = mk_check(hwnd,ID_CHK_DELZIP,  "Delete ZIP file after merge",  80,310,300,22);
    g_hChkOpenDir  = mk_check(hwnd,ID_CHK_OPENDIR, "Open output folder when done", 80,338,310,22);
    g_hBtnRunMerge = mk_action_btn(hwnd,ID_BTN_RUN_MERGE,"MERGE",350,376,200,44);
    g_hBtnBack     = mk_back_btn(hwnd,ID_BTN_BACK,14,14,80,30);

    /* Merge — file-order ListView */
    g_hListMergePDFs = CreateWindowExA(WS_EX_STATICEDGE, WC_LISTVIEWA, "",
        WS_CHILD|WS_TABSTOP|WS_VSCROLL|LVS_REPORT|LVS_SHOWSELALWAYS|LVS_SINGLESEL,
        80,186,740,200, hwnd, (HMENU)(INT_PTR)ID_LST_MERGE_PDFS, NULL, NULL);
    set_font_all(g_hListMergePDFs, g_fntBody);
    ListView_SetExtendedListViewStyle(g_hListMergePDFs, LVS_EX_FULLROWSELECT);
    g_hMergeImgList = ImageList_Create(1, 26, ILC_COLOR32, 0, 1);
    ListView_SetImageList(g_hListMergePDFs, g_hMergeImgList, LVSIL_SMALL);
    {
        LVCOLUMNA col; memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT|LVCF_WIDTH|LVCF_SUBITEM;
        col.cx   = 736; col.pszText = "File   (click header to sort  |  drag or use arrows to reorder)"; col.iSubItem = 0;
        ListView_InsertColumn(g_hListMergePDFs, 0, &col);
    }
    g_hBtnMergeUp = mk_action_btn(hwnd, ID_BTN_MERGE_UP, "", 80,  394, 32, 28);
    g_hBtnMergeDn = mk_action_btn(hwnd, ID_BTN_MERGE_DN, "", 118, 394, 32, 28);

    /* Split */
    g_hDropSplit      = mk_drop_area(hwnd,ID_DROP_SPLIT,80,80,740,110);
    g_hDropLabelSplit = CreateWindowA("STATIC","",WS_CHILD|SS_CENTER,
                                      80,80,740,110,hwnd,NULL,NULL,NULL);
    set_font_all(g_hDropLabelSplit, g_fntBody);
    g_hEditSplitDir   = mk_edit(hwnd,ID_EDIT_SPLIT_DIR, 210,204,474,26);
    g_hBtnSplitBrowse = mk_btn(hwnd,ID_BTN_SPLIT_BROWSE,"Browse...",690,204,84,26,g_fntBody);
    g_hEditSplitName  = mk_edit(hwnd,ID_EDIT_SPLIT_NAME,210,238,558,26);
    /* Placeholder text shown when the field is empty */
    SendMessageW(g_hEditSplitName, EM_SETCUEBANNER, TRUE,
                 (LPARAM)L"leave blank to use original PDF filename");
    g_hBtnRunSplit    = mk_action_btn(hwnd,ID_BTN_RUN_SPLIT,"SPLIT",350,296,200,44);
    g_hBtnBackSplit   = mk_back_btn(hwnd,ID_BTN_BACK_SPLIT,14,14,80,30);

    /* Arrange */
    g_hListPDFs = CreateWindowExA(WS_EX_STATICEDGE, WC_LISTVIEWA, "",
        WS_CHILD|WS_TABSTOP|WS_VSCROLL|LVS_REPORT|LVS_SHOWSELALWAYS|LVS_SINGLESEL,
        80,92,740,280,hwnd,(HMENU)(INT_PTR)ID_LST_PDFS,NULL,NULL);
    set_font_all(g_hListPDFs, g_fntBody);
    /* Full-row select only — NO grid lines */
    ListView_SetExtendedListViewStyle(g_hListPDFs, LVS_EX_FULLROWSELECT);
    /* Force row height to 28 px via a 1×26 image list */
    g_hArrImgList = ImageList_Create(1, 26, ILC_COLOR32, 0, 1);
    ListView_SetImageList(g_hListPDFs, g_hArrImgList, LVSIL_SMALL);
    /* Single column — header shows sort direction */
    {
        LVCOLUMNA col; memset(&col,0,sizeof(col));
        col.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_SUBITEM;
        col.cx=736; col.pszText="File   (click header to sort  |  drag or use arrows to reorder)"; col.iSubItem=0;
        ListView_InsertColumn(g_hListPDFs,0,&col);
    }

    /* Single ↑ ↓ ✕ set (owner-drawn icon buttons) */
    g_hBtnArrUp = mk_action_btn(hwnd,ID_BTN_ARR_UP,"",80,  380,32,32);
    g_hBtnArrDn = mk_action_btn(hwnd,ID_BTN_ARR_DN,"",118, 380,32,32);
    g_hBtnArrX  = mk_action_btn(hwnd,ID_BTN_ARR_X, "",156, 380,32,32);

    g_hBtnAddPDFs   = mk_btn(hwnd,ID_BTN_ADD_PDFS,  "+ Add Files",  80,380,130,28,g_fntBody);
    g_hBtnArrClear  = mk_btn(hwnd,ID_BTN_ARR_CLEAR, "Clear All",   216,380,100,28,g_fntBody);
    g_hEditArrOut = mk_edit(hwnd,ID_EDIT_ARR_OUT,232,418,488,26);
    g_hChkArrOpen = mk_check(hwnd,ID_CHK_ARR_OPEN,"Open output folder when done",80,452,310,22);
    g_hBtnRunArr  = mk_action_btn(hwnd,ID_BTN_RUN_ARR,"MERGE",350,484,200,44);
    g_hBtnBackArr = mk_back_btn(hwnd,ID_BTN_BACK_ARR,14,14,80,30);
}

/* ============================================================
 * ACTIONS  (business logic — unchanged)
 * ============================================================ */

/* Create a uniquely-named subfolder inside base_dir.
 * Tries:  base_dir\name
 *         base_dir\name_2
 *         base_dir\name_3  ...
 * Strips any trailing .pdf extension from name first.
 * Returns TRUE and fills out_path on success. */
static BOOL make_unique_folder(const char *base_dir, const char *name,
                                char *out_path, int out_size)
{
    /* Strip .pdf extension if present */
    char clean[MAX_PATH];
    strncpy(clean, name, MAX_PATH-1); clean[MAX_PATH-1] = 0;
    int len = (int)strlen(clean);
    if (len > 4 && _stricmp(clean+len-4, ".pdf")==0) clean[len-4] = 0;

    /* Try base name first */
    snprintf(out_path, out_size, "%s\\%s", base_dir, clean);
    if (GetFileAttributesA(out_path) == INVALID_FILE_ATTRIBUTES)
        return CreateDirectoryA(out_path, NULL);

    /* Try numbered suffixes _2, _3, … */
    for (int n = 2; n < 10000; n++) {
        snprintf(out_path, out_size, "%s\\%s_%d", base_dir, clean, n);
        if (GetFileAttributesA(out_path) == INVALID_FILE_ATTRIBUTES)
            return CreateDirectoryA(out_path, NULL);
    }
    return FALSE;
}

/* ============================================================
 * MERGE SCREEN — FILE ORDER DATA MODEL
 * ============================================================ */

#define MAX_MERGE_FILES 512

static char g_merge_names[MAX_MERGE_FILES][MAX_PATH]; /* PDF basenames from ZIP, user-ordered */
static int  g_merge_count    = 0;
static int  g_merge_dragging = -1;
static int  g_merge_drag_tgt = -1;

static int cmp_merge_name(const void *a, const void *b)
{ return _stricmp((const char *)a, (const char *)b); }

static void merge_swap(int a, int b)
{
    char tmp[MAX_PATH];
    memcpy(tmp,               g_merge_names[a], MAX_PATH);
    memcpy(g_merge_names[a],  g_merge_names[b], MAX_PATH);
    memcpy(g_merge_names[b],  tmp,               MAX_PATH);
}

static void merge_refresh_list(int sel_idx)
{
    ListView_DeleteAllItems(g_hListMergePDFs);
    for (int i = 0; i < g_merge_count; i++) {
        LVITEMA lvi; memset(&lvi, 0, sizeof(lvi));
        lvi.mask = LVIF_TEXT; lvi.iItem = i;
        lvi.pszText = g_merge_names[i];
        ListView_InsertItem(g_hListMergePDFs, &lvi);
    }
    if (sel_idx >= 0 && sel_idx < g_merge_count) {
        ListView_SetItemState(g_hListMergePDFs, sel_idx,
            LVIS_SELECTED|LVIS_FOCUSED, LVIS_SELECTED|LVIS_FOCUSED);
        ListView_EnsureVisible(g_hListMergePDFs, sel_idx, FALSE);
    }
    InvalidateRect(g_hListMergePDFs, NULL, TRUE);
}

/* Sort g_merge_names per current g_merge_sort_dir (1=A-Z, 0=Z-A). */
static void merge_sort(void)
{
    qsort(g_merge_names, g_merge_count, MAX_PATH, cmp_merge_name);
    if (g_merge_sort_dir == 0) {
        /* Reverse in-place for Z-A */
        for (int i = 0, j = g_merge_count - 1; i < j; i++, j--) {
            char tmp[MAX_PATH];
            memcpy(tmp,               g_merge_names[i], MAX_PATH);
            memcpy(g_merge_names[i],  g_merge_names[j], MAX_PATH);
            memcpy(g_merge_names[j],  tmp,               MAX_PATH);
        }
    }
}

/* Update the merge ListView header text + native sort arrow to match g_merge_sort_dir. */
static void merge_update_header(void)
{
    HWND hHdr = ListView_GetHeader(g_hListMergePDFs);
    if (!hHdr) return;

    char text[128];
    DWORD fmt = HDF_LEFT | HDF_STRING;
    if (g_merge_sort_dir == 1) {
        strcpy(text, "A-Z   |   File");
        fmt |= HDF_SORTUP;
    } else if (g_merge_sort_dir == 0) {
        strcpy(text, "Z-A   |   File");
        fmt |= HDF_SORTDOWN;
    } else {
        strcpy(text, "File   (click header to sort  |  drag or use arrows to reorder)");
    }

    HDITEM hdi; memset(&hdi, 0, sizeof(hdi));
    hdi.mask    = HDI_TEXT | HDI_FORMAT;
    hdi.pszText = text;
    hdi.cchTextMax = (int)strlen(text) + 1;
    hdi.fmt     = (int)fmt;
    Header_SetItem(hHdr, 0, &hdi);
}

/* Read PDF filenames from a ZIP (no extraction) and populate g_merge_names sorted A-Z. */
static void load_zip_pdf_names(const char *zip_path)
{
    g_merge_count = 0;
    mz_zip_archive zip; memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, zip_path, 0)) return;
    int total = (int)mz_zip_reader_get_num_files(&zip);
    for (int i = 0; i < total && g_merge_count < MAX_MERGE_FILES; i++) {
        char name[MAX_PATH];
        mz_zip_reader_get_filename(&zip, i, name, MAX_PATH);
        int nlen = (int)strlen(name);
        if (nlen < 4 || _stricmp(name + nlen - 4, ".pdf") != 0) continue;
        /* Strip any directory prefix inside the ZIP */
        const char *fname = name;
        for (int j = nlen - 1; j >= 0; j--)
            if (name[j] == '/' || name[j] == '\\') { fname = name + j + 1; break; }
        if (!fname[0]) continue;
        strncpy(g_merge_names[g_merge_count++], fname, MAX_PATH - 1);
    }
    mz_zip_reader_end(&zip);
    /* Default sort: A-Z */
    qsort(g_merge_names, g_merge_count, MAX_PATH, cmp_merge_name);
    g_merge_sort_dir = 1;   /* reflect in header */
}

/* NM_CUSTOMDRAW for the merge file-order list */
static LRESULT on_merge_customdraw(LPNMLVCUSTOMDRAW cd)
{
    switch (cd->nmcd.dwDrawStage) {

    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;

    case CDDS_POSTPAINT:
        if (g_merge_count == 0) {
            HDC  hdc = cd->nmcd.hdc;
            RECT cr;  GetClientRect(g_hListMergePDFs, &cr);
            HWND hHdr = ListView_GetHeader(g_hListMergePDFs);
            if (hHdr) { RECT hr; GetClientRect(hHdr, &hr); cr.top += (hr.bottom - hr.top); }
            SetBkMode(hdc, TRANSPARENT);
            SelectObject(hdc, g_fntBody);
            SetTextColor(hdc, C_DROP_TEXT);
            DrawTextA(hdc, "Load a ZIP file above to list its PDFs here",
                -1, &cr, DT_CENTER|DT_VCENTER|DT_WORDBREAK|DT_NOPREFIX);
        }
        return CDRF_DODEFAULT;

    case CDDS_ITEMPREPAINT: {
        int  idx     = (int)cd->nmcd.dwItemSpec;
        BOOL sel     = (cd->nmcd.uItemState & CDIS_SELECTED) != 0;
        BOOL is_drag = (g_merge_dragging >= 0 && idx == g_merge_dragging);

        if (is_drag) {
            HDC  hdc = cd->nmcd.hdc;
            RECT r;   ListView_GetItemRect(g_hListMergePDFs, idx, &r, LVIR_BOUNDS);
            COLORREF bg = g_dark ? RGB(20,44,95) : RGB(210,228,255);
            HBRUSH hbr = CreateSolidBrush(bg); FillRect(hdc, &r, hbr); DeleteObject(hbr);
            LOGBRUSH lb = {BS_SOLID, C_ACCENT, 0};
            HPEN dp = ExtCreatePen(PS_GEOMETRIC|PS_DASH|PS_ENDCAP_FLAT, 1, &lb, 0, NULL);
            HGDIOBJ op = SelectObject(hdc, dp);
            HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, r.left+1, r.top+1, r.right-1, r.bottom-2);
            SelectObject(hdc, op); SelectObject(hdc, ob); DeleteObject(dp);
            if (idx < g_merge_count) {
                RECT tr = {r.left+10, r.top, r.right-8, r.bottom};
                SetBkMode(hdc, TRANSPARENT); SelectObject(hdc, g_fntBody);
                SetTextColor(hdc, C_ACCENT);
                DrawTextA(hdc, g_merge_names[idx], -1, &tr,
                    DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            }
            return CDRF_SKIPDEFAULT;
        }
        if (sel)        { cd->clrText = C_BTN_FG; cd->clrTextBk = C_ACCENT; }
        else if (idx&1) { cd->clrText = C_TEXT;   cd->clrTextBk = g_dark ? RGB(24,28,44) : RGB(246,248,254); }
        else            { cd->clrText = C_TEXT;   cd->clrTextBk = C_SUB_BG; }
        return CDRF_NEWFONT;
    }}
    return CDRF_DODEFAULT;
}

static int cmp_str(const void *a, const void *b)
{ return _stricmp(*(const char**)a, *(const char**)b); }

static char **extract_zip_pdfs(const char *zip_path, const char *temp_dir,
                                int *out_count, char *err, int err_size)
{
    mz_zip_archive zip; memset(&zip,0,sizeof(zip));
    if (!mz_zip_reader_init_file(&zip,zip_path,0)) {
        snprintf(err,err_size,"Cannot open ZIP file."); return NULL; }
    int total=(int)mz_zip_reader_get_num_files(&zip);
    char **paths=NULL; int count=0;
    for (int i=0;i<total;i++) {
        char name[MAX_PATH];
        mz_zip_reader_get_filename(&zip,i,name,MAX_PATH);
        int nlen=(int)strlen(name);
        if (nlen<4||_stricmp(name+nlen-4,".pdf")!=0) continue;
        const char *fname=name;
        for (int j=nlen-1;j>=0;j--)
            if (name[j]=='/'||name[j]=='\\') { fname=name+j+1; break; }
        char op[MAX_PATH]; snprintf(op,MAX_PATH,"%s\\%s",temp_dir,fname);
        if (!mz_zip_reader_extract_to_file(&zip,i,op,0)) continue;
        char **tmp=(char**)realloc(paths,(count+1)*sizeof(char*));
        if (!tmp) break; paths=tmp; paths[count++]=_strdup(op);
    }
    mz_zip_reader_end(&zip);
    if (count==0) { snprintf(err,err_size,"No PDF files found inside the ZIP.");
                    free(paths); return NULL; }
    qsort(paths,count,sizeof(char*),cmp_str);
    *out_count=count; return paths;
}

static void do_browse_zip(void)
{
    OPENFILENAMEA ofn={0}; char buf[MAX_PATH]="";
    ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=g_hwnd;
    ofn.lpstrFilter="ZIP Files\0*.zip\0All Files\0*.*\0";
    ofn.lpstrFile=buf; ofn.nMaxFile=MAX_PATH;
    ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
    if (GetOpenFileNameA(&ofn)) {
        strncpy(g_zipPath,buf,MAX_PATH-1);
        load_zip_pdf_names(g_zipPath);
        merge_refresh_list(0);
        merge_update_header();
        InvalidateRect(g_hwnd, NULL, FALSE); }
}

static void do_browse_pdf_split(void)
{
    OPENFILENAMEA ofn={0}; char buf[MAX_PATH]="";
    ofn.lStructSize=sizeof(ofn); ofn.hwndOwner=g_hwnd;
    ofn.lpstrFilter="PDF Files\0*.pdf\0All Files\0*.*\0";
    ofn.lpstrFile=buf; ofn.nMaxFile=MAX_PATH;
    ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
    if (GetOpenFileNameA(&ofn)) {
        strncpy(g_splitPdfPath,buf,MAX_PATH-1);
        InvalidateRect(g_hwnd, NULL, FALSE); }
}

static void do_browse_split_dir(void)
{
    BROWSEINFOA bi={0}; bi.hwndOwner=g_hwnd;
    bi.lpszTitle="Select output folder for split pages";
    bi.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl=SHBrowseForFolderA(&bi);
    if (pidl) { char buf[MAX_PATH];
        SHGetPathFromIDListA(pidl,buf);
        SetWindowTextA(g_hEditSplitDir,buf);
        CoTaskMemFree(pidl); }
}

static void do_run_merge(void)
{
    if (!g_zipPath[0]) {
        MessageBoxA(g_hwnd,"Please select a ZIP file first.","No File",MB_ICONWARNING|MB_OK);
        return; }

    char out_name[MAX_PATH]; GetWindowTextA(g_hEditOutName,out_name,MAX_PATH);
    if (!out_name[0]) strcpy(out_name,"Merged");

    /* Strip any .pdf extension for use as folder name */
    char base_name[MAX_PATH]; strncpy(base_name,out_name,MAX_PATH-1);
    int blen=(int)strlen(base_name);
    if (blen>4 && _stricmp(base_name+blen-4,".pdf")==0) base_name[blen-4]=0;

    /* Determine output folder location (same directory as the ZIP) */
    char zip_dir[MAX_PATH]; strncpy(zip_dir,g_zipPath,MAX_PATH-1);
    PathRemoveFileSpecA(zip_dir);

    /* Create a uniquely-named output folder */
    char out_folder[MAX_PATH];
    if (!make_unique_folder(zip_dir,base_name,out_folder,MAX_PATH)) {
        MessageBoxA(g_hwnd,"Could not create output folder.","Error",MB_ICONERROR|MB_OK);
        return; }

    /* Output PDF lives inside the new folder */
    char out_path[MAX_PATH];
    snprintf(out_path,MAX_PATH,"%s\\%s.pdf",out_folder,base_name);

    /* Extract ZIPped PDFs to a temp directory */
    char temp_base[MAX_PATH]; GetTempPathA(MAX_PATH,temp_base);
    char temp_dir[MAX_PATH];
    snprintf(temp_dir,MAX_PATH,"%sPDFMerge_%lu",temp_base,GetCurrentProcessId());
    CreateDirectoryA(temp_dir,NULL);

    char err[256]; int count;
    char **pdfs=extract_zip_pdfs(g_zipPath,temp_dir,&count,err,sizeof(err));
    if (!pdfs) {
        RemoveDirectoryA(out_folder);
        MessageBoxA(g_hwnd,err,"Error",MB_ICONERROR|MB_OK);
        RemoveDirectoryA(temp_dir); return; }

    /* Reorder extracted paths to match g_merge_names user order (if list was loaded).
     * For each slot in g_merge_names find the matching extracted path by basename. */
    if (g_merge_count > 0) {
        char **ordered = (char**)calloc(count, sizeof(char*));
        int   n_ord    = 0;
        if (ordered) {
            /* Walk g_merge_names order; for each entry find the extracted file */
            for (int i = 0; i < g_merge_count && n_ord < count; i++) {
                for (int j = 0; j < count; j++) {
                    if (!pdfs[j]) continue;
                    const char *bname = PathFindFileNameA(pdfs[j]);
                    if (_stricmp(bname, g_merge_names[i]) == 0) {
                        ordered[n_ord++] = pdfs[j];
                        pdfs[j] = NULL; /* mark as consumed */
                        break;
                    }
                }
            }
            /* Append any extracted files not matched in g_merge_names */
            for (int j = 0; j < count; j++)
                if (pdfs[j]) ordered[n_ord++] = pdfs[j];
            /* Replace pdfs array with the ordered one */
            for (int j = 0; j < count; j++) pdfs[j] = ordered[j];
            free(ordered);
        }
    }

    int rc=pdf_merge_files((const char**)pdfs,count,out_path,err,sizeof(err));

    for (int i=0;i<count;i++) { DeleteFileA(pdfs[i]); free(pdfs[i]); }
    free(pdfs);
    RemoveDirectoryA(temp_dir);

    if (rc!=0) {
        RemoveDirectoryA(out_folder);
        MessageBoxA(g_hwnd,err,"Merge Failed",MB_ICONERROR|MB_OK); return; }

    if (SendMessage(g_hChkDelZip, BM_GETCHECK,0,0)==BST_CHECKED) DeleteFileA(g_zipPath);
    if (SendMessage(g_hChkOpenDir,BM_GETCHECK,0,0)==BST_CHECKED)
        ShellExecuteA(g_hwnd,"open","explorer.exe",out_folder,NULL,SW_SHOWNORMAL);

    char msg[MAX_PATH+64]; snprintf(msg,sizeof(msg),"Done!\n\nSaved to:\n%s",out_path);
    MessageBoxA(g_hwnd,msg,"Merge Complete",MB_ICONINFORMATION|MB_OK);
    g_zipPath[0]=0;
    InvalidateRect(g_hwnd, NULL, FALSE);
}

static void do_run_split(void)
{
    if (!g_splitPdfPath[0]) {
        MessageBoxA(g_hwnd,"Please select a PDF file first.","No File",MB_ICONWARNING|MB_OK);
        return; }

    /* Base directory — user-chosen or same folder as the source PDF */
    char base_dir[MAX_PATH]; GetWindowTextA(g_hEditSplitDir,base_dir,MAX_PATH);
    if (!base_dir[0]) { strncpy(base_dir,g_splitPdfPath,MAX_PATH-1);
                        PathRemoveFileSpecA(base_dir); }

    /* Folder name = source PDF filename without .pdf extension */
    char folder_name[MAX_PATH];
    strncpy(folder_name, PathFindFileNameA(g_splitPdfPath), MAX_PATH-1);
    int flen=(int)strlen(folder_name);
    if (flen>4 && _stricmp(folder_name+flen-4,".pdf")==0) folder_name[flen-4]=0;

    /* Create a uniquely-named subfolder */
    char out_folder[MAX_PATH];
    if (!make_unique_folder(base_dir,folder_name,out_folder,MAX_PATH)) {
        MessageBoxA(g_hwnd,"Could not create output folder.","Error",MB_ICONERROR|MB_OK);
        return; }

    /* Custom page base name — empty means use source filename */
    char split_name[MAX_PATH];
    GetWindowTextA(g_hEditSplitName, split_name, MAX_PATH);
    /* Trim leading/trailing whitespace */
    { char *s=split_name; while(*s==' '||*s=='\t') memmove(s,s+1,strlen(s));
      int tl=(int)strlen(split_name);
      while(tl>0&&(split_name[tl-1]==' '||split_name[tl-1]=='\t')) split_name[--tl]=0; }

    char err[256];
    int n=pdf_split_file(g_splitPdfPath, out_folder,
                         split_name[0] ? split_name : NULL,
                         err, sizeof(err));
    if (n<0) {
        RemoveDirectoryA(out_folder);
        MessageBoxA(g_hwnd,err,"Split Failed",MB_ICONERROR|MB_OK); }
    else {
        char msg[MAX_PATH+64];
        snprintf(msg,sizeof(msg),"Done! Split into %d page(s).\n\nSaved to:\n%s",n,out_folder);
        MessageBoxA(g_hwnd,msg,"Split Complete",MB_ICONINFORMATION|MB_OK);
        ShellExecuteA(g_hwnd,"open","explorer.exe",out_folder,NULL,SW_SHOWNORMAL); }
}

/* ============================================================
 * ARRANGE SCREEN — DATA MODEL
 * ============================================================ */

static char g_arr_paths[MAX_ARR_FILES][MAX_PATH];
static int  g_arr_count    = 0;
static int  g_arr_sort_dir = -1;  /* -1=custom, 1=A-Z, 0=Z-A */
static int  g_arr_dragging = -1;  /* row being dragged; -1 = none */
static int  g_arr_drag_tgt = -1;  /* insertion target row          */

static void arr_swap(int a, int b)
{
    char tmp[MAX_PATH];
    memcpy(tmp,             g_arr_paths[a], MAX_PATH);
    memcpy(g_arr_paths[a], g_arr_paths[b], MAX_PATH);
    memcpy(g_arr_paths[b], tmp,             MAX_PATH);
}

static void arr_remove(int idx)
{
    if (idx < 0 || idx >= g_arr_count) return;
    for (int i = idx; i < g_arr_count - 1; i++)
        memcpy(g_arr_paths[i], g_arr_paths[i+1], MAX_PATH);
    g_arr_count--;
}

static int arr_cmp_az(const void *a, const void *b)
{ return _stricmp(PathFindFileNameA((const char*)a),
                  PathFindFileNameA((const char*)b)); }
static int arr_cmp_za(const void *a, const void *b)
{ return -_stricmp(PathFindFileNameA((const char*)a),
                   PathFindFileNameA((const char*)b)); }

static void arr_sort(void)
{
    qsort(g_arr_paths, g_arr_count, MAX_PATH,
          g_arr_sort_dir ? arr_cmp_az : arr_cmp_za);
}

static void arr_update_header(void)
{
    HWND hHdr = ListView_GetHeader(g_hListPDFs);
    if (!hHdr) return;
    char text[128];
    DWORD fmt = HDF_LEFT | HDF_STRING;
    if (g_arr_sort_dir == 1) {
        strcpy(text, "A-Z   |   File");
        fmt |= HDF_SORTUP;
    } else if (g_arr_sort_dir == 0) {
        strcpy(text, "Z-A   |   File");
        fmt |= HDF_SORTDOWN;
    } else {
        strcpy(text, "File   (click header to sort  |  drag or use arrows to reorder)");
    }
    HDITEM hdi; memset(&hdi, 0, sizeof(hdi));
    hdi.mask       = HDI_TEXT | HDI_FORMAT;
    hdi.pszText    = text;
    hdi.cchTextMax = (int)strlen(text) + 1;
    hdi.fmt        = (int)fmt;
    Header_SetItem(hHdr, 0, &hdi);
}

static void arr_refresh_list(int sel_idx)
{
    ListView_DeleteAllItems(g_hListPDFs);
    for (int i = 0; i < g_arr_count; i++) {
        LVITEMA lvi; memset(&lvi, 0, sizeof(lvi));
        lvi.mask    = LVIF_TEXT;
        lvi.iItem   = i;
        lvi.pszText = (char*)PathFindFileNameA(g_arr_paths[i]); /* filename only */
        ListView_InsertItem(g_hListPDFs, &lvi);
    }
    if (sel_idx >= 0 && sel_idx < g_arr_count) {
        ListView_SetItemState(g_hListPDFs, sel_idx,
            LVIS_SELECTED|LVIS_FOCUSED, LVIS_SELECTED|LVIS_FOCUSED);
        ListView_EnsureVisible(g_hListPDFs, sel_idx, FALSE);
    }
    InvalidateRect(g_hListPDFs, NULL, TRUE);
}

static void arr_add_files(void)
{
    char buf[32768] = "";
    OPENFILENAMEA ofn; memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = "PDF Files\0*.pdf\0All Files\0*.*\0";
    ofn.lpstrFile = buf; ofn.nMaxFile = sizeof(buf);
    ofn.Flags = OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_ALLOWMULTISELECT|OFN_EXPLORER;
    if (!GetOpenFileNameA(&ofn)) return;
    char dir[MAX_PATH]; strncpy(dir, buf, MAX_PATH-1);
    char *p = buf + strlen(buf) + 1;
    if (!*p) {
        if (g_arr_count < MAX_ARR_FILES)
            strncpy(g_arr_paths[g_arr_count++], buf, MAX_PATH-1);
    } else {
        while (*p && g_arr_count < MAX_ARR_FILES) {
            char full[MAX_PATH];
            snprintf(full, MAX_PATH, "%s\\%s", dir, p);
            strncpy(g_arr_paths[g_arr_count++], full, MAX_PATH-1);
            p += strlen(p) + 1;
        }
    }
    arr_refresh_list(g_arr_count - 1);
}

/* Owner-drawn icon button for the single ↑ ↓ ✕ set (type 0/1/2) */
static void draw_arr_icon_btn(DRAWITEMSTRUCT *dis, int type)
{
    HDC  hdc  = dis->hDC;
    RECT r    = dis->rcItem;
    BOOL down = (dis->itemState & ODS_SELECTED) != 0;

    COLORREF bg     = (type == 2)
        ? (down ? RGB(160,30,30) : (g_dark ? RGB(72,18,18) : RGB(255,228,228)))
        : (down ? darken(C_SUB_BG,14) : C_SUB_BG);
    COLORREF border = (type == 2) ? RGB(200,50,50) : C_BORDER;

    fill_rounded  (hdc, &r, 6, bg);
    stroke_rounded(hdc, &r, 6, border, 1);

    int cx = (r.left + r.right)  / 2;
    int cy = (r.top  + r.bottom) / 2;

    if (type == 0) {
        HPEN hp = CreatePen(PS_SOLID, 2, C_ACCENT);
        HGDIOBJ op = SelectObject(hdc, hp);
        MoveToEx(hdc, cx-6, cy+4, NULL); LineTo(hdc, cx,   cy-4);
        MoveToEx(hdc, cx,   cy-4, NULL); LineTo(hdc, cx+6, cy+4);
        SelectObject(hdc, op); DeleteObject(hp);
    } else if (type == 1) {
        HPEN hp = CreatePen(PS_SOLID, 2, C_ACCENT);
        HGDIOBJ op = SelectObject(hdc, hp);
        MoveToEx(hdc, cx-6, cy-4, NULL); LineTo(hdc, cx,   cy+4);
        MoveToEx(hdc, cx,   cy+4, NULL); LineTo(hdc, cx+6, cy-4);
        SelectObject(hdc, op); DeleteObject(hp);
    } else {
        HPEN hp = CreatePen(PS_SOLID, 2, RGB(210,48,48));
        HGDIOBJ op = SelectObject(hdc, hp);
        MoveToEx(hdc, cx-5, cy-5, NULL); LineTo(hdc, cx+5, cy+5);
        MoveToEx(hdc, cx+5, cy-5, NULL); LineTo(hdc, cx-5, cy+5);
        SelectObject(hdc, op); DeleteObject(hp);
    }
}

/* NM_CUSTOMDRAW handler — returns the CDRF_* value for WM_NOTIFY */
static LRESULT on_arrange_customdraw(LPNMLVCUSTOMDRAW cd)
{
    switch (cd->nmcd.dwDrawStage) {

    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;

    case CDDS_POSTPAINT:
        /* Empty-state placeholder when no files added yet */
        if (g_arr_count == 0) {
            HDC  hdc = cd->nmcd.hdc;
            RECT cr;  GetClientRect(g_hListPDFs, &cr);
            HWND hHdr = ListView_GetHeader(g_hListPDFs);
            if (hHdr) {
                RECT hr; GetClientRect(hHdr, &hr);
                cr.top += (hr.bottom - hr.top);
            }
            SetBkMode(hdc, TRANSPARENT);
            SelectObject(hdc, g_fntBody);
            SetTextColor(hdc, C_DROP_TEXT);
            DrawTextA(hdc,
                "Drag and drop PDF files here\nor click  \"+  Add Files\"  below",
                -1, &cr, DT_CENTER|DT_VCENTER|DT_WORDBREAK|DT_NOPREFIX);
        }
        return CDRF_DODEFAULT;

    case CDDS_ITEMPREPAINT: {
        int  idx     = (int)cd->nmcd.dwItemSpec;
        BOOL sel     = (cd->nmcd.uItemState & CDIS_SELECTED) != 0;
        BOOL is_drag = (g_arr_dragging >= 0 && idx == g_arr_dragging);

        if (is_drag) {
            /* Draw the dragged "lifted" item entirely ourselves.
             * Use ListView_GetItemRect — guaranteed ListView-local coordinates,
             * avoiding any HDC-origin mismatch with cd->nmcd.rc. */
            HDC  hdc = cd->nmcd.hdc;
            RECT r;
            ListView_GetItemRect(g_hListPDFs, idx, &r, LVIR_BOUNDS);

            /* Lifted ghost background */
            COLORREF bg = g_dark ? RGB(20,44,95) : RGB(210,228,255);
            HBRUSH hbr = CreateSolidBrush(bg);
            FillRect(hdc, &r, hbr);
            DeleteObject(hbr);

            /* Dashed accent border — "floating" look */
            {
                LOGBRUSH lb = {BS_SOLID, C_ACCENT, 0};
                HPEN dp = ExtCreatePen(PS_GEOMETRIC|PS_DASH|PS_ENDCAP_FLAT,
                                       1, &lb, 0, NULL);
                HGDIOBJ op = SelectObject(hdc, dp);
                HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
                Rectangle(hdc, r.left+1, r.top+1, r.right-1, r.bottom-2);
                SelectObject(hdc, op); SelectObject(hdc, ob);
                DeleteObject(dp);
            }

            /* Filename text in accent colour */
            if (idx < g_arr_count) {
                const char *fname = PathFindFileNameA(g_arr_paths[idx]);
                RECT tr = {r.left+10, r.top, r.right-8, r.bottom};
                SetBkMode(hdc, TRANSPARENT);
                SelectObject(hdc, g_fntBody);
                SetTextColor(hdc, C_ACCENT);
                DrawTextA(hdc, fname, -1, &tr,
                    DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            }
            return CDRF_SKIPDEFAULT;
        }

        /* Normal item: set colors and let ListView draw the text natively.
         * CDRF_NEWFONT tells the ListView to re-select font/colors and redraw —
         * text is placed by the ListView itself so coordinates are always correct. */
        if (sel) {
            cd->clrText   = C_BTN_FG;
            cd->clrTextBk = C_ACCENT;
        } else if (idx & 1) {
            cd->clrText   = C_TEXT;
            cd->clrTextBk = g_dark ? RGB(24,28,44) : RGB(246,248,254);
        } else {
            cd->clrText   = C_TEXT;
            cd->clrTextBk = C_SUB_BG;
        }
        return CDRF_NEWFONT;
    }
    }
    return CDRF_DODEFAULT;
}

static void do_run_arrange(void)
{
    if (g_arr_count == 0) {
        MessageBoxA(g_hwnd,"Please add PDF files first.","No Files",
                    MB_ICONWARNING|MB_OK);
        return;
    }

    char out_name[MAX_PATH]; GetWindowTextA(g_hEditArrOut, out_name, MAX_PATH);
    if (!out_name[0]) strcpy(out_name, "Merged");

    /* Strip .pdf extension for use as folder name */
    char base_name[MAX_PATH]; strncpy(base_name, out_name, MAX_PATH-1);
    int blen = (int)strlen(base_name);
    if (blen>4 && _stricmp(base_name+blen-4,".pdf")==0) base_name[blen-4]=0;

    /* Output folder sits next to the first file in the list */
    char first_dir[MAX_PATH] = "";
    strncpy(first_dir, g_arr_paths[0], MAX_PATH-1);
    PathRemoveFileSpecA(first_dir);

    /* Create a uniquely-named output folder */
    char out_folder[MAX_PATH];
    if (!make_unique_folder(first_dir, base_name, out_folder, MAX_PATH)) {
        MessageBoxA(g_hwnd,"Could not create output folder.","Error",MB_ICONERROR|MB_OK);
        return; }

    /* Output PDF lives inside the new folder */
    char out_path[MAX_PATH];
    snprintf(out_path, MAX_PATH, "%s\\%s.pdf", out_folder, base_name);

    const char **paths = (const char**)malloc(g_arr_count * sizeof(char*));
    if (!paths) { RemoveDirectoryA(out_folder); return; }
    for (int i = 0; i < g_arr_count; i++) paths[i] = g_arr_paths[i];

    char err[256];
    int rc = pdf_merge_files(paths, g_arr_count, out_path, err, sizeof(err));
    free(paths);

    if (rc != 0) {
        RemoveDirectoryA(out_folder);
        MessageBoxA(g_hwnd,err,"Merge Failed",MB_ICONERROR|MB_OK); return; }

    if (SendMessage(g_hChkArrOpen,BM_GETCHECK,0,0)==BST_CHECKED)
        ShellExecuteA(g_hwnd,"open","explorer.exe",out_folder,NULL,SW_SHOWNORMAL);

    char msg[MAX_PATH+64]; snprintf(msg,sizeof(msg),"Done!\n\nSaved to:\n%s",out_path);
    MessageBoxA(g_hwnd,msg,"Merge Complete",MB_ICONINFORMATION|MB_OK);
}

/* ============================================================
 * WINDOW PROCEDURE
 * ============================================================ */

/* EnumChildWindows callback — invalidates each child for theme repaint */
static BOOL CALLBACK invalidate_child_proc(HWND h, LPARAM l)
{
    (void)l;
    InvalidateRect(h, NULL, TRUE);
    return TRUE;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {

    case WM_CREATE:
        g_hwnd = hwnd;
        create_all_controls(hwnd);
        hide_all_screens();
        DragAcceptFiles(hwnd, TRUE);
        return 0;

    case WM_SIZE: {
        int cw=LOWORD(lp), ch=HIWORD(lp);
        if (cw>0 && ch>0) reposition_controls(cw, ch);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi=(MINMAXINFO*)lp;
        RECT wr={0,0,MIN_WIN_W,MIN_WIN_H};
        AdjustWindowRect(&wr,WS_OVERLAPPEDWINDOW,FALSE);
        mmi->ptMinTrackSize.x=wr.right-wr.left;
        mmi->ptMinTrackSize.y=wr.bottom-wr.top;
        return 0;
    }

    case WM_SETTINGCHANGE: {
        /* Re-detect theme when Windows broadcasts a settings change */
        if (lp && lstrcmpiA((LPCSTR)lp, "ImmersiveColorSet")==0) {
            setup_theme();
            apply_dark_titlebar(hwnd);
            apply_listview_theme();
            InvalidateRect(hwnd, NULL, TRUE);
            /* Force all child controls to repaint with new colours */
            EnumChildWindows(hwnd, invalidate_child_proc, 0);
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc=BeginPaint(hwnd,&ps);
        RECT client; GetClientRect(hwnd,&client);
        switch (g_screen) {
        case SCR_WELCOME: on_paint_welcome(hdc,&client); break;
        case SCR_MERGE:   on_paint_merge(hdc,&client);   break;
        case SCR_SPLIT:   on_paint_split(hdc,&client);   break;
        case SCR_ARRANGE: on_paint_arrange(hdc,&client); break;
        }
        EndPaint(hwnd,&ps);
        return 0;
    }

    /* Owner-draw buttons */
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *dis=(DRAWITEMSTRUCT*)lp;
        switch (dis->CtlID) {
        case ID_BTN_RUN_MERGE:
        case ID_BTN_RUN_SPLIT:
        case ID_BTN_RUN_ARR: {
            char txt[64]="";
            GetWindowTextA(dis->hwndItem, txt, sizeof(txt));
            draw_action_btn(dis, txt);
            return TRUE;
        }
        case ID_BTN_BACK:
        case ID_BTN_BACK_SPLIT:
        case ID_BTN_BACK_ARR:
            draw_back_btn(dis);
            return TRUE;
        case ID_BTN_MERGE_UP: draw_arr_icon_btn(dis, 0); return TRUE;
        case ID_BTN_MERGE_DN: draw_arr_icon_btn(dis, 1); return TRUE;
        case ID_BTN_ARR_UP:   draw_arr_icon_btn(dis, 0); return TRUE;
        case ID_BTN_ARR_DN:   draw_arr_icon_btn(dis, 1); return TRUE;
        case ID_BTN_ARR_X:    draw_arr_icon_btn(dis, 2); return TRUE;
        }
        break;
    }

    /* Background colour for static / checkbox / edit controls */
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC  hdc  = (HDC)wp;
        HWND hCtl = (HWND)lp;
        /* Drop-zone overlay labels must be transparent so our GDI depth layers show */
        if (hCtl==g_hDropArea || hCtl==g_hDropLabel ||
            hCtl==g_hDropSplit || hCtl==g_hDropLabelSplit) {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, C_DROP_TEXT);
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        COLORREF bg=(g_screen==SCR_WELCOME) ? C_BG : C_SUB_BG;
        SetBkColor(hdc, bg);
        SetTextColor(hdc, C_TEXT);
        return (LRESULT)((g_screen==SCR_WELCOME) ? g_hBrBg : g_hBrSubBg);
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc=(HDC)wp;
        SetBkColor(hdc, C_EDIT_BG);
        SetTextColor(hdc, C_TEXT);
        return (LRESULT)g_hBrEdit;
    }

    case WM_NOTIFY: {
        NMHDR *hdr = (NMHDR*)lp;

        /* ── Merge file-order list ── */
        if (hdr->hwndFrom == g_hListMergePDFs) {
            switch (hdr->code) {
            case NM_CUSTOMDRAW:
                return on_merge_customdraw((LPNMLVCUSTOMDRAW)lp);
            case LVN_COLUMNCLICK:
                /* Toggle sort direction; re-sort and reflect in header */
                g_merge_sort_dir = (g_merge_sort_dir == 1) ? 0 : 1;
                merge_sort();
                merge_refresh_list(-1);
                merge_update_header();
                break;
            case LVN_BEGINDRAG: {
                NMLISTVIEW *nmlv = (NMLISTVIEW*)lp;
                g_merge_dragging = nmlv->iItem;
                g_merge_drag_tgt = nmlv->iItem;
                SetCapture(hwnd);
                SetCursor(LoadCursor(NULL, IDC_SIZENS));
                break;
            }
            }
        }

        /* ── Arrange list ── */
        if (hdr->hwndFrom == g_hListPDFs) {
            switch (hdr->code) {

            case NM_CUSTOMDRAW:
                return on_arrange_customdraw((LPNMLVCUSTOMDRAW)lp);

            case LVN_COLUMNCLICK: {
                g_arr_sort_dir = (g_arr_sort_dir == 1) ? 0 : 1;
                arr_sort();
                arr_refresh_list(-1);
                arr_update_header();
                break;
            }

            case LVN_BEGINDRAG: {
                NMLISTVIEW *nmlv = (NMLISTVIEW*)lp;
                if (nmlv->ptAction.x >= 80) {
                    g_arr_dragging = nmlv->iItem;
                    g_arr_drag_tgt = nmlv->iItem;
                    SetCapture(hwnd);
                    SetCursor(LoadCursor(NULL, IDC_SIZENS));
                }
                break;
            }

            }  /* switch hdr->code */
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (g_screen==SCR_WELCOME) {
            POINT pt={(short)LOWORD(lp),(short)HIWORD(lp)};
            RECT rects[4]; get_card_rects(rects);
            int prev=g_hover_card; g_hover_card=-1;
            for (int i=0;i<4;i++)
                if (PtInRect(&rects[i],pt)) { g_hover_card=i; break; }
            if (g_hover_card!=prev) InvalidateRect(hwnd,NULL,FALSE);
            TRACKMOUSEEVENT tme={sizeof(tme),TME_LEAVE,hwnd,0};
            TrackMouseEvent(&tme);
        }
        /* Drag-reorder in Merge list */
        if (g_merge_dragging >= 0 && g_screen == SCR_MERGE) {
            POINT pt={(short)LOWORD(lp),(short)HIWORD(lp)};
            MapWindowPoints(hwnd, g_hListMergePDFs, &pt, 1);
            LVHITTESTINFO hti; memset(&hti,0,sizeof(hti)); hti.pt = pt;
            int tgt = ListView_HitTest(g_hListMergePDFs, &hti);
            if (tgt >= 0 && tgt < g_merge_count && tgt != g_merge_drag_tgt) {
                int step = (tgt > g_merge_dragging) ? 1 : -1;
                int next = g_merge_dragging + step;
                if (next >= 0 && next < g_merge_count) {
                    int old_pos = g_merge_dragging;
                    merge_swap(old_pos, next);
                    ListView_SetItemText(g_hListMergePDFs, old_pos, 0, g_merge_names[old_pos]);
                    ListView_SetItemText(g_hListMergePDFs, next,    0, g_merge_names[next]);
                    g_merge_dragging = next;
                    g_merge_drag_tgt = next;
                    ListView_SetItemState(g_hListMergePDFs, g_merge_dragging,
                        LVIS_SELECTED|LVIS_FOCUSED, LVIS_SELECTED|LVIS_FOCUSED);
                    ListView_EnsureVisible(g_hListMergePDFs, g_merge_dragging, FALSE);
                    InvalidateRect(g_hListMergePDFs, NULL, TRUE);
                    UpdateWindow(g_hListMergePDFs);
                }
            }
        }
        /* Drag-reorder in Arrange list */
        if (g_arr_dragging >= 0 && g_screen == SCR_ARRANGE) {
            POINT pt={(short)LOWORD(lp),(short)HIWORD(lp)};
            MapWindowPoints(hwnd, g_hListPDFs, &pt, 1);
            LVHITTESTINFO hti; memset(&hti,0,sizeof(hti)); hti.pt = pt;
            int tgt = ListView_HitTest(g_hListPDFs, &hti);
            if (tgt >= 0 && tgt < g_arr_count && tgt != g_arr_drag_tgt) {
                int step = (tgt > g_arr_dragging) ? 1 : -1;
                int next = g_arr_dragging + step;
                if (next >= 0 && next < g_arr_count) {
                    int old_pos = g_arr_dragging;
                    arr_swap(old_pos, next);
                    ListView_SetItemText(g_hListPDFs, old_pos, 0,
                        (char*)PathFindFileNameA(g_arr_paths[old_pos]));
                    ListView_SetItemText(g_hListPDFs, next, 0,
                        (char*)PathFindFileNameA(g_arr_paths[next]));
                    g_arr_dragging = next;
                    g_arr_drag_tgt = next;
                    ListView_SetItemState(g_hListPDFs, g_arr_dragging,
                        LVIS_SELECTED|LVIS_FOCUSED, LVIS_SELECTED|LVIS_FOCUSED);
                    ListView_EnsureVisible(g_hListPDFs, g_arr_dragging, FALSE);
                    InvalidateRect(g_hListPDFs, NULL, TRUE);
                    UpdateWindow(g_hListPDFs);
                }
            }
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        /* End merge-list drag */
        if (g_merge_dragging >= 0) {
            int final_pos = g_merge_dragging;
            ReleaseCapture();
            g_merge_dragging = -1;
            g_merge_drag_tgt = -1;
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            /* User manually reordered — clear sort indicator */
            g_merge_sort_dir = -1;
            merge_update_header();
            ListView_SetItemState(g_hListMergePDFs, final_pos,
                LVIS_SELECTED|LVIS_FOCUSED, LVIS_SELECTED|LVIS_FOCUSED);
            ListView_EnsureVisible(g_hListMergePDFs, final_pos, FALSE);
            InvalidateRect(g_hListMergePDFs, NULL, TRUE);
            UpdateWindow(g_hListMergePDFs);
        }
        /* End arrange-list drag */
        if (g_arr_dragging >= 0) {
            /* Item is already at its final position (live-shifted in WM_MOUSEMOVE) */
            int final_pos = g_arr_dragging;
            ReleaseCapture();
            g_arr_dragging = -1;
            g_arr_drag_tgt = -1;
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            /* User manually reordered — clear sort indicator */
            g_arr_sort_dir = -1;
            arr_update_header();
            /* Keep the dropped item selected, clear the drag highlight */
            ListView_SetItemState(g_hListPDFs, final_pos,
                LVIS_SELECTED|LVIS_FOCUSED, LVIS_SELECTED|LVIS_FOCUSED);
            ListView_EnsureVisible(g_hListPDFs, final_pos, FALSE);
            InvalidateRect(g_hListPDFs, NULL, TRUE);
            UpdateWindow(g_hListPDFs);
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        if (g_hover_card!=-1) { g_hover_card=-1; InvalidateRect(hwnd,NULL,FALSE); }
        return 0;

    case WM_COMMAND: {
        int id=LOWORD(wp);
        switch(id) {
        case ID_BTN_MERGE:       show_screen(SCR_MERGE);   break;
        case ID_BTN_SPLIT:       show_screen(SCR_SPLIT);   break;
        case ID_BTN_ARRANGE:     show_screen(SCR_ARRANGE); break;
        case ID_BTN_PLACEHOLDER:
            MessageBoxA(hwnd,"This feature is coming soon!","Coming Soon",
                        MB_ICONINFORMATION|MB_OK); break;
        case ID_BTN_BACK:
        case ID_BTN_BACK_SPLIT:
        case ID_BTN_BACK_ARR:    show_screen(SCR_WELCOME); break;
        case ID_DROP_AREA:       do_browse_zip();           break;
        case ID_DROP_SPLIT:      do_browse_pdf_split();     break;
        case ID_BTN_SPLIT_BROWSE:do_browse_split_dir();     break;
        case ID_BTN_MERGE_UP: {
            int sel=ListView_GetNextItem(g_hListMergePDFs,-1,LVNI_SELECTED);
            if (sel>0) {
                merge_swap(sel, sel-1);
                g_merge_sort_dir = -1;
                merge_refresh_list(sel-1);
                merge_update_header();
            }
            break; }
        case ID_BTN_MERGE_DN: {
            int sel=ListView_GetNextItem(g_hListMergePDFs,-1,LVNI_SELECTED);
            if (sel>=0 && sel<g_merge_count-1) {
                merge_swap(sel, sel+1);
                g_merge_sort_dir = -1;
                merge_refresh_list(sel+1);
                merge_update_header();
            }
            break; }
        case ID_BTN_RUN_MERGE:   do_run_merge();            break;
        case ID_BTN_RUN_SPLIT:   do_run_split();            break;
        case ID_BTN_ADD_PDFS:    arr_add_files(); break;
        case ID_BTN_ARR_CLEAR:
            g_arr_count = 0;
            arr_refresh_list(-1);
            break;
        case ID_BTN_ARR_UP: {
            int sel=ListView_GetNextItem(g_hListPDFs,-1,LVNI_SELECTED);
            if (sel>0) {
                arr_swap(sel,sel-1); g_arr_sort_dir=-1;
                arr_refresh_list(sel-1); arr_update_header();
            }
            break; }
        case ID_BTN_ARR_DN: {
            int sel=ListView_GetNextItem(g_hListPDFs,-1,LVNI_SELECTED);
            if (sel>=0 && sel<g_arr_count-1) {
                arr_swap(sel,sel+1); g_arr_sort_dir=-1;
                arr_refresh_list(sel+1); arr_update_header();
            }
            break; }
        case ID_BTN_ARR_X: {
            int sel=ListView_GetNextItem(g_hListPDFs,-1,LVNI_SELECTED);
            if (sel>=0) {
                arr_remove(sel); g_arr_sort_dir=-1;
                arr_refresh_list(sel<g_arr_count?sel:g_arr_count-1);
                arr_update_header();
            }
            break; }
        case ID_BTN_RUN_ARR:     do_run_arrange();          break;
        }
        return 0;
    }

    case WM_LBUTTONDOWN: {
        if (g_screen!=SCR_WELCOME) break;
        POINT pt={(short)LOWORD(lp),(short)HIWORD(lp)};
        RECT rects[4]; get_card_rects(rects);
        int ids[4]={ID_BTN_MERGE,ID_BTN_SPLIT,ID_BTN_ARRANGE,ID_BTN_PLACEHOLDER};
        for (int i=0;i<4;i++)
            if (PtInRect(&rects[i],pt)) {
                SendMessage(hwnd,WM_COMMAND,MAKEWPARAM(ids[i],0),0); break; }
        return 0;
    }

    case WM_SETCURSOR: {
        if (g_screen==SCR_WELCOME) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd,&pt);
            RECT rects[4]; get_card_rects(rects);
            for (int i=0;i<4;i++)
                if (PtInRect(&rects[i],pt)) {
                    SetCursor(LoadCursor(NULL,IDC_HAND)); return TRUE; }
        }
        return DefWindowProcA(hwnd,msg,wp,lp);
    }

    case WM_DROPFILES: {
        HDROP hd=(HDROP)wp;
        if (g_screen==SCR_MERGE) {
            char path[MAX_PATH]; DragQueryFileA(hd,0,path,MAX_PATH);
            int nlen=(int)strlen(path);
            if (nlen>4 && _stricmp(path+nlen-4,".zip")==0) {
                strncpy(g_zipPath,path,MAX_PATH-1);
                load_zip_pdf_names(g_zipPath);
                merge_refresh_list(0);
                merge_update_header();
                InvalidateRect(hwnd, NULL, FALSE);
            } else MessageBoxA(hwnd,"Please drop a .zip file.","Wrong File Type",MB_ICONWARNING|MB_OK);
        } else if (g_screen==SCR_SPLIT) {
            char path[MAX_PATH]; DragQueryFileA(hd,0,path,MAX_PATH);
            int nlen=(int)strlen(path);
            if (nlen>4 && _stricmp(path+nlen-4,".pdf")==0) {
                strncpy(g_splitPdfPath,path,MAX_PATH-1);
                InvalidateRect(hwnd, NULL, FALSE);
            } else MessageBoxA(hwnd,"Please drop a .pdf file.","Wrong File Type",MB_ICONWARNING|MB_OK);
        } else if (g_screen==SCR_ARRANGE) {
            /* Accept multiple dropped PDFs */
            UINT nfiles = DragQueryFileA(hd, 0xFFFFFFFF, NULL, 0);
            for (UINT f=0; f<nfiles && g_arr_count<MAX_ARR_FILES; f++) {
                char path[MAX_PATH]; DragQueryFileA(hd,f,path,MAX_PATH);
                int nlen=(int)strlen(path);
                if (nlen>4 && _stricmp(path+nlen-4,".pdf")==0)
                    strncpy(g_arr_paths[g_arr_count++], path, MAX_PATH-1);
            }
            g_arr_sort_dir = -1;
            arr_refresh_list(g_arr_count-1);
            arr_update_header();
        }
        DragFinish(hd);
        return 0;
    }

    case WM_ERASEBKGND: return 1;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

/* ============================================================
 * WINMAIN
 * ============================================================ */

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    (void)hPrev; (void)lpCmd;

    INITCOMMONCONTROLSEX icc={sizeof(icc),ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&icc);

    /* Detect system theme before anything is drawn */
    setup_theme();

    g_fntTitle   = make_font(24, 1, 0);
    g_fntHeading = make_font(15, 1, 0);
    g_fntBody    = make_font(10, 0, 0);
    g_fntBtnBig  = make_font(13, 1, 0);
    g_fntBtnCard = make_font(13, 1, 0);
    g_fntSmall   = make_font( 8, 0, 1);

    load_logo(hInst);
    g_hIconBig = (HICON)LoadImageA(hInst, MAKEINTRESOURCE(IDI_APPICON),
                                   IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    g_hIconSm  = (HICON)LoadImageA(hInst, MAKEINTRESOURCE(IDI_APPICON),
                                   IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);

    WNDCLASSEXA wc={0};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW|CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE+1);
    wc.lpszClassName = APP_CLASS;
    wc.hIcon         = g_hIconBig ? g_hIconBig : LoadIcon(NULL,IDI_APPLICATION);
    wc.hIconSm       = g_hIconSm  ? g_hIconSm  : LoadIcon(NULL,IDI_APPLICATION);
    RegisterClassExA(&wc);

    int sx=GetSystemMetrics(SM_CXSCREEN), sy=GetSystemMetrics(SM_CYSCREEN);
    RECT wr={0,0,WIN_W,WIN_H};
    AdjustWindowRect(&wr,WS_OVERLAPPEDWINDOW,FALSE);
    int ww=wr.right-wr.left, wh=wr.bottom-wr.top;

    HWND hwnd=CreateWindowExA(0, APP_CLASS, APP_TITLE,
        WS_OVERLAPPEDWINDOW,
        (sx-ww)/2, (sy-wh)/2, ww, wh,
        NULL, NULL, hInst, NULL);

    /* Apply dark/light title bar immediately after window creation */
    apply_dark_titlebar(hwnd);

    if (g_hIconBig) SendMessage(hwnd,WM_SETICON,ICON_BIG,  (LPARAM)g_hIconBig);
    if (g_hIconSm)  SendMessage(hwnd,WM_SETICON,ICON_SMALL,(LPARAM)g_hIconSm);

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg,NULL,0,0)) {
        TranslateMessage(&msg); DispatchMessage(&msg); }

    DeleteObject(g_fntTitle);   DeleteObject(g_fntHeading);
    DeleteObject(g_fntBody);    DeleteObject(g_fntBtnBig);
    DeleteObject(g_fntBtnCard); DeleteObject(g_fntSmall);
    if (g_hLogo)       DeleteObject(g_hLogo);
    if (g_hBrBg)       DeleteObject(g_hBrBg);
    if (g_hBrSubBg)    DeleteObject(g_hBrSubBg);
    if (g_hBrEdit)     DeleteObject(g_hBrEdit);
    if (g_hIconBig)    DestroyIcon(g_hIconBig);
    if (g_hIconSm)     DestroyIcon(g_hIconSm);
    if (g_hArrImgList) ImageList_Destroy(g_hArrImgList);

    return (int)msg.wParam;
}
