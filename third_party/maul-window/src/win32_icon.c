// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Win32 window icons.

#include "win32_icon.h"

#include "icon.h"

// An icon of an image: its colour a 32-bit DIB with straight alpha,
// rows from the top, and an empty mask.
static HICON Make(const mwinIconCopyImage* image)
{
    BITMAPV5HEADER header = {
        .bV5Size = sizeof(header),
        .bV5Width = (LONG)image->width,
        .bV5Height = -(LONG)image->height,
        .bV5Planes = 1,
        .bV5BitCount = 32,
        .bV5Compression = BI_BITFIELDS,
        .bV5RedMask = 0x00FF0000,
        .bV5GreenMask = 0x0000FF00,
        .bV5BlueMask = 0x000000FF,
        .bV5AlphaMask = 0xFF000000,
    };
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color =
        CreateDIBSection(screen, (const BITMAPINFO*)&header, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    HBITMAP mask = CreateBitmap((int)image->width, (int)image->height, 1, 1, nullptr);
    HICON icon = nullptr;
    if (color != nullptr && mask != nullptr)
    {
        uint8_t* out = bits;
        size_t count = (size_t)image->width * image->height;
        for (size_t i = 0; i < count; i++)
        {
            const uint8_t* in = image->pixels + i * 4;
            out[i * 4 + 0] = in[2];
            out[i * 4 + 1] = in[1];
            out[i * 4 + 2] = in[0];
            out[i * 4 + 3] = in[3];
        }
        ICONINFO info = {.fIcon = TRUE, .hbmMask = mask, .hbmColor = color};
        icon = CreateIconIndirect(&info);
    }
    if (color != nullptr)
    {
        DeleteObject(color);
    }
    if (mask != nullptr)
    {
        DeleteObject(mask);
    }
    return icon;
}

mwinOutcome mwinWin32SetIcon(mwinWin32Window* window, const mwinRequest* request)
{
    const mwinIconCopy* copy = request->value.icon;
    HICON icons[2] = {nullptr, nullptr};
    if (copy->count > 0)
    {
        int bigSide = GetSystemMetricsForDpi(SM_CXICON, window->dpi);
        int smallSide = GetSystemMetricsForDpi(SM_CXSMICON, window->dpi);
        icons[0] = Make(mwinIconFor(copy, (uint32_t)bigSide));
        icons[1] = Make(mwinIconFor(copy, (uint32_t)smallSide));
        if (icons[0] == nullptr || icons[1] == nullptr)
        {
            for (int i = 0; i < 2; i++)
            {
                if (icons[i] != nullptr)
                {
                    DestroyIcon(icons[i]);
                }
            }
            return mwin_outcomeFailed;
        }
    }
    SendMessageW(window->hwnd, WM_SETICON, ICON_BIG, (LPARAM)icons[0]);
    SendMessageW(window->hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icons[1]);
    mwinWin32ReleaseIcons(window);
    window->icons[0] = icons[0];
    window->icons[1] = icons[1];
    return mwin_outcomeDone;
}

void mwinWin32ReleaseIcons(mwinWin32Window* window)
{
    for (int i = 0; i < 2; i++)
    {
        if (window->icons[i] != nullptr)
        {
            DestroyIcon(window->icons[i]);
            window->icons[i] = nullptr;
        }
    }
}
