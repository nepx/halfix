// Simple display driver
#if 1

#include "display.h"
#include "devices.h"
#include "util.h"
#include <stdlib.h>

#include <windows.h>


#include "util.h"

static HINSTANCE hInst;
static HWND hWnd;
static WNDCLASS wc;
static HDC dc_dest, dc_src; // Drawing contexts
static void* pixels;
static int cheight, cwidth, mouse_enabled;
static HBITMAP hBmp;

enum {
    MENU_EXIT,
    MENU_SAVE_STATE
};

static LRESULT CALLBACK display_callback(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    char filename[4096];
    switch(msg){
        case WM_CREATE:
            break; 
        case WM_DESTROY:
            printf("Exiting.\n");
            exit(0);
            break;
        case WM_KEYDOWN:
            printf("Key down!!\n");
            break;
        case WM_KEYUP:
            printf("Key up!!\n");
            break;
        case WM_COMMAND:
            switch(LOWORD(wparam)){
                case MENU_EXIT:
                    printf("Exiting.\n");
                    exit(0);
                case MENU_SAVE_STATE: {
                    OPENFILENAME ofn;
                    ZeroMemory(&ofn, sizeof(OPENFILENAME));

                    ofn.lStructSize = sizeof(OPENFILENAME);
                    ofn.hwndOwner = hWnd;
                    ofn.lpstrFilter = "All files (*.)\0*.*\0\0";
                    filename[0] = 0;
                    ofn.lpstrFile = filename;
                    ofn.nMaxFile = 4095;
                    ofn.Flags = OFN_EXPLORER;
                    ofn.lpstrDefExt = "";
                    ofn.lpstrTitle = "Save state to...";
                    ofn.lpstrInitialDir = ".";
                    
                    if(GetSaveFileName(&ofn)) {
                        state_store_to_file(filename);
                        printf("SELECTED\n");
                    }else{
                        printf("NOT SELECTED\n");
                    }
                    break;
                }
            }
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

static void display_set_title(void)
{
    char buffer[1000];
    sprintf(buffer, "Halfix x86 Emulator - "
                    " [%d x %d] - %s",
        cwidth, cheight,
        mouse_enabled ? "Press ESC to release mouse" : "Right-click to capture mouse");
    SetWindowText(hWnd, buffer);
}

void display_init(void) {
    // Hopefully, this file will always be compiled into an executable: 
    // https://stackoverflow.com/questions/21718027/getmodulehandlenull-vs-hinstance
    hInst = GetModuleHandle(NULL);
    wc.lpszClassName = "Halfix";
    wc.hInstance = hInst;
    wc.hbrBackground = GetSysColorBrush(COLOR_3DFACE);
    wc.lpfnWndProc = display_callback;
    wc.hCursor = LoadCursor(0, IDC_ARROW);

    RegisterClass(&wc);

    hWnd = CreateWindow(
        wc.lpszClassName, 
        "Halfix", 
        WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        // Create it at some random spot
        100, 
        100, 
        // Make it 640x400
        640, 
        400, 
        // No parent window
        NULL, 
        // No menu
        NULL, 
        // HINSTANCE
        hInst,
        // void 
        NULL);
    
    HMENU bar = CreateMenu(),
        file = CreateMenu();

    AppendMenu(file, MF_STRING, MENU_EXIT, "&Exit");
    AppendMenu(file, MF_STRING, MENU_SAVE_STATE, "&Save State");

    AppendMenu(bar, MF_POPUP, file, "&File");
    SetMenu(hWnd, bar);

    dc_dest = GetDC(hWnd);

    display_set_resolution(640, 400);

    // Now let it run through a few events. 
    MSG blah;
    while(PeekMessage(&blah, hWnd, 0, 0, PM_REMOVE)){
        TranslateMessage(&blah);
        DispatchMessage(&blah);
    }
}
void display_update(int scanline_start, int scanlines)
{
    HDC hdc, mdc;
    hdc = GetDC(hWnd);
    mdc = CreateCompatibleDC(hdc);
    SelectObject(mdc, hBmp);
    BitBlt(
        // Destination context
        dc_dest, 
        // Destination is at (0, 0)
        0, 0, 
        // Copy the entire rectangle
        cwidth, cheight, 
        // Our device context source
        dc_src, 
        // Copy from top corner of rectangle
        0, 0, 
        // Just copy -- don't do anything fancy.
        SRCCOPY);
    ReleaseDC(hWnd, hdc);
    UNUSED(scanline_start | scanlines);
}
void display_set_resolution(int width, int height)
{
    // CreateDIB section doesn't like it when our width/height are zero. 
    if(width == 0 || height == 0) return;
    if(dc_src) DeleteObject(dc_src);
    dc_src = CreateCompatibleDC(dc_dest);

    BITMAPINFO i;
    ZeroMemory(&i.bmiHeader, sizeof(BITMAPINFOHEADER));
    i.bmiHeader.biWidth = width;
    // Force the DIB to follow VGA/VESA rules:
    //  "If biHeight is negative, the bitmap is a top-down DIB with the origin at the upper left corner."
    i.bmiHeader.biHeight = -height;
    i.bmiHeader.biPlanes = 1;
    i.bmiHeader.biBitCount = 32;
    i.bmiHeader.biCompression = BI_RGB;
    i.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);

    void* pvBits;
    HDC hdc = GetDC(hWnd);
    hBmp = CreateDIBSection(hdc, &i, DIB_RGB_COLORS, &pvBits, NULL, 0);
    ReleaseDC(hWnd, hdc);
    if(!hBmp){
        printf("Failed to create DIB section: %p [%d %d]\n", dc_dest, width, height);
        abort();
    }
    pixels = pvBits;
    UNUSED(hBmp);

    SelectObject(dc_src, hBmp);

    cheight = height;
    cwidth = width;
    display_set_title();

    // Update window size
    RECT rect;
    // x-coordinate of the upper left corner
    rect.left = 0;
    // y-coordinate
    rect.top = 0;
    // x-coordinate of the lower left corner
    rect.right = cwidth;
    // y-coordinate
    rect.bottom = cheight;
    if(!AdjustWindowRectEx(&rect, GetWindowLong(hWnd, GWL_STYLE), TRUE, 0)){
        printf("Failed to AdjustWindowRect\n");
        exit(0);
    }
    SetWindowPos(hWnd, (HWND)0, 0, 0, rect.right-rect.left, rect.bottom-rect.top, SWP_NOMOVE | SWP_NOOWNERZORDER);
}
void* display_get_pixels(void)
{
    return pixels;
}
void display_handle_events(void){
    MSG blah;
    while(PeekMessage(&blah, hWnd, 0, 0, PM_REMOVE)){
        TranslateMessage(&blah);
        DispatchMessage(&blah);
    }
}
void display_release_mouse(void){
    
}
void display_sleep(int ms){
    UNUSED(ms);
}

#if 0
void display_init(void);
void display_update(int scanline_start, int scanlines);
void display_set_resolution(int width, int height);
void* display_get_pixels(void);
void display_handle_events(void);
void display_update_cycles(int cycles_elapsed, int us);
void display_sleep(int ms);

void display_release_mouse(void);
#endif

#else // Headless mode
#include "util.h"
static uint32_t pixels[800 * 500];
void display_init(void) {}
void display_update(int scanline_start, int scanlines)
{
    UNUSED(scanline_start | scanlines);
}
void display_set_resolution(int width, int height)
{
    UNUSED(width | height);
}
void* display_get_pixels(void)
{
    return pixels;
}
#endif