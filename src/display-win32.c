// Simple display driver
#if 1

#include "display.h"
#include "devices.h"
#include "util.h"
#include <stdlib.h>

#include <windows.h>
#include <windowsx.h>

#include "util.h"

static HINSTANCE hInst;
static HWND hWnd;
static WNDCLASS wc;
static HDC dc_dest, dc_src; // Drawing contexts
static void* pixels;
static int cheight, cwidth, mouse_enabled;
static HBITMAP hBmp;
// Position of the cursor, relative to the whole screen
static int screenx, screeny;
// Position of the cursor, relative to the window
static int windowx, windowy;
static int lastx, lasty;
// Coordinates of cursor when captured
static int original_x, original_y;

enum {
    MENU_EXIT,
    MENU_SAVE_STATE
};

static inline void lparam_to_xy(LPARAM lparam, int* x, int* y)
{
    *x = GET_X_LPARAM(lparam);
    *y = GET_Y_LPARAM(lparam);
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

static void display_capture_mouse(int yes)
{
    if (yes) {
        RECT rect;
        // Get window size and adjust it
        GetWindowRect(hWnd, &rect);
        ClipCursor(&rect);
        SetCapture(hWnd);
        ShowCursor(FALSE);
        SetCursorPos(screenx, screeny);
    } else {
        //ClipCursor(NULL);
        SetCapture(NULL);
        ShowCursor(TRUE);
    }
    mouse_enabled = yes;
    display_set_title();
}

// Converts a Win32 virtual key code to a PS/2 scan code.
// See "sdl_keysym_to_scancode" in display.c for a similar implementation
// https://stanislavs.org/helppc/make_codes.html
static int win32_to_scancode(int w)
{
    switch (w) {
    case VK_BACK:
        return 0x0E;
    case VK_CAPITAL:
        return 0x3A;
    case VK_RETURN:
        return 0x1C;
    case VK_ESCAPE:
        return 0x01;
    case VK_MENU: // ALT
        return 0x38; // using left alt
    case VK_CONTROL:
        return 0x1D; // using left ctrl
    case VK_LSHIFT:
        return 0x2A;
    case VK_NUMLOCK:
        return 0x45;
    case VK_RSHIFT:
        return 0x36;
    case VK_SCROLL:
        return 0x46;
    case VK_SPACE:
        return 0x39;
    case VK_TAB:
        return 0x0F;
    case VK_F1... VK_F12:
        return w - VK_F1 + 0x3B;
    case VK_NUMPAD0:
        return 0x52;
    case VK_NUMPAD1:
        return 0x4F;
    case VK_NUMPAD2:
        return 0x50;
    case VK_NUMPAD3:
        return 0x51;
    case VK_NUMPAD4:
        return 0x4B;
    case VK_NUMPAD5:
        return 0x4C;
    case VK_NUMPAD6:
        return 0x4D;
    case VK_NUMPAD7:
        return 0x47;
    case VK_NUMPAD8:
        return 0x48;
    case VK_NUMPAD9:
        return 0x49;
    case VK_DECIMAL: // keypad period/del
        return 0x53;
    case VK_MULTIPLY: // keypad asterix/prtsc
        return 0x37;
    case VK_SUBTRACT: // keypad dash
        return 0x4A;
    case VK_ADD:
        return 0x4E;

    case 0x31 ... 0x39: // 1-9
        return w + 2 - 0x31;
    case 0x30: // 0
        return 0x0B;
// lazy - copy-pasted from display.c with a few hackish macros
#define toUpper(b) b - ('a' - 'A')

    case toUpper('a'):
        return 0x1E;
    case toUpper('b'):
        return 0x30;
    case toUpper('c'):
        return 0x2E;
    case toUpper('d'):
        return 0x20;
    case toUpper('e'):
        return 0x12;
    case toUpper('f'):
        return 0x21;
    case toUpper('g'):
        return 0x22;
    case toUpper('h'):
        return 0x23;
    case toUpper('i'):
        return 0x17;
    case toUpper('j'):
        return 0x24;
    case toUpper('k'):
        return 0x25;
    case toUpper('l'):
        return 0x26;
    case toUpper('m'):
        return 0x32;
    case toUpper('n'):
        return 0x31;
    case toUpper('o'):
        return 0x18;
    case toUpper('p'):
        return 0x19;
    case toUpper('q'):
        return 0x10;
    case toUpper('r'):
        return 0x13;
    case toUpper('s'):
        return 0x1F;
    case toUpper('t'):
        return 0x14;
    case toUpper('u'):
        return 0x16;
    case toUpper('v'):
        return 0x2F;
    case toUpper('w'):
        return 0x11;
    case toUpper('x'):
        return 0x2D;
    case toUpper('y'):
        return 0x15;
    case toUpper('z'):
        return 0x2C;
    case VK_OEM_MINUS: // -/_
        return 0x0C;
    case VK_OEM_PLUS: // +/=
        return 0x0D;
    case VK_OEM_4: // {/[
        return 0x1A;
    case VK_OEM_6: // }/]
        return 0x1B;
    case VK_OEM_5: // \/|
        return 0x2B;
    case VK_OEM_1: // ;/:
        return 0x27;
    case VK_OEM_7: // '/"
        return 0x28;
    case VK_OEM_3: // `/~
        return 0x2C;
    case VK_OEM_COMMA: // ,/<
        return 0x33;
    case VK_OEM_PERIOD: // ./>
        return 0x34;
    case VK_OEM_2: // / or ?
        return 0x35;

    case VK_DELETE:
        return 0xE053;
    case VK_DOWN:
        return 0xE050;
    case VK_END:
        return 0xE04F;
    case VK_HOME:
        return 0xE047;
    case VK_INSERT:
        return 0xE052;
    case VK_LEFT:
        return 0xE048;
    case VK_PRIOR: // pgdn
        return 0xE049;
    case VK_NEXT: // pgup
        return 0xE051;
    case VK_RIGHT:
        return 0xE04D;
    case VK_UP:
        return 0xE048;
    }
}

static inline void display_kbd_send_key(int k)
{
    if (k & 0xFF00)
        kbd_add_key(k >> 8);
    kbd_add_key(k & 0xFF);
}

static LRESULT CALLBACK display_callback(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    char filename[4096];
    switch (msg) {
    case WM_CREATE:
        break;
    case WM_MOVE:
        // Recalculate the center of our window, if mouse capture is set
        windowx = (cwidth >> 1);
        windowy = (cheight >> 1);
        screenx = windowx + LOWORD(lparam);
        screeny = windowy + HIWORD(lparam);
        break;
    case WM_DESTROY:
        printf("Exiting.\n");
        exit(0);
        break;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE && mouse_enabled)
            display_capture_mouse(0);
        else
            display_kbd_send_key(win32_to_scancode(wparam));
        printf("Key down!!\n");
        break;
    case WM_KEYUP:
        printf("Key up!!\n");
        break;
    case WM_MOUSEMOVE:
        if (mouse_enabled) {
            // Windows gives us absolute coordinates, so we have to do the calculations ourselves
            int x, y;
            lparam_to_xy(lparam, &x, &y);
            int dx = x - windowx, dy = y - windowy;
            //printf("x/y: %d, %d wx/wy: %d, %d, dx/dy: %d, %d\n", x, y, windowx, windowy, dx, dy);
            kbd_send_mouse_move(dx, dy);
            SetCursorPos(screenx, screeny);
        }
        break;
    case WM_RBUTTONDOWN:
        if (!mouse_enabled) {
            lparam_to_xy(lparam, &lastx, &lasty);
            display_capture_mouse(1);
        } else {
            kbd_mouse_down(MOUSE_STATUS_NOCHANGE, MOUSE_STATUS_NOCHANGE, MOUSE_STATUS_PRESSED);
        }
        break;
    case WM_RBUTTONUP:
        kbd_mouse_down(MOUSE_STATUS_NOCHANGE, MOUSE_STATUS_NOCHANGE, MOUSE_STATUS_RELEASED);
        break;
    case WM_LBUTTONDOWN:
        kbd_mouse_down(MOUSE_STATUS_PRESSED, MOUSE_STATUS_NOCHANGE, MOUSE_STATUS_NOCHANGE);
        break;
    case WM_LBUTTONUP:
        kbd_mouse_down(MOUSE_STATUS_RELEASED, MOUSE_STATUS_NOCHANGE, MOUSE_STATUS_NOCHANGE);
        break;
    case WM_MBUTTONDOWN:
        kbd_mouse_down(MOUSE_STATUS_NOCHANGE, MOUSE_STATUS_PRESSED, MOUSE_STATUS_NOCHANGE);
        break;
    case WM_MBUTTONUP:
        kbd_mouse_down(MOUSE_STATUS_NOCHANGE, MOUSE_STATUS_RELEASED, MOUSE_STATUS_NOCHANGE);
        break;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
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

            if (GetSaveFileName(&ofn)) {
                state_store_to_file(filename);
                printf("SELECTED\n");
            } else {
                printf("NOT SELECTED\n");
            }
            break;
        }
        }
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

void display_init(void)
{
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
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
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
    while (PeekMessage(&blah, hWnd, 0, 0, PM_REMOVE)) {
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
    if (width == 0 || height == 0)
        return;
    if (dc_src)
        DeleteObject(dc_src);
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
    if (!hBmp) {
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
    if (!AdjustWindowRectEx(&rect, GetWindowLong(hWnd, GWL_STYLE), TRUE, 0)) {
        printf("Failed to AdjustWindowRect\n");
        exit(0);
    }
    SetWindowPos(hWnd, (HWND)0, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOOWNERZORDER);
}
void* display_get_pixels(void)
{
    return pixels;
}
void display_handle_events(void)
{
    MSG blah;
    while (PeekMessage(&blah, hWnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&blah);
        DispatchMessage(&blah);
    }
}
void display_release_mouse(void)
{
}
void display_sleep(int ms)
{
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