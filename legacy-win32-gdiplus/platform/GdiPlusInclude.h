// platform/GdiPlusInclude.h
// The single place the project pulls in <windows.h> + <gdiplus.h>, so the
// two toolchain quirks below are handled once instead of in seven headers.
// Every platform/ file includes this instead of those two directly.
#pragma once

// 1) <windows.h> defines min/max as function-like MACROS unless NOMINMAX
//    is set. That breaks every std::min / std::max / std::clamp call in
//    platform/, because the preprocessor rewrites `std::min(a, b)` into
//    `std::((a) < (b) ? (a) : (b))`. Both build scripts pass -DNOMINMAX /
//    /DNOMINMAX; this guard keeps the header correct even if a file is
//    compiled by hand without that flag.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// 2) ...but the MICROSOFT Windows SDK's <gdiplustypes.h> then calls bare
//    min()/max() itself (inside Rect::Union and Rect::Intersect), so with
//    NOMINMAX defined it fails to compile with
//    "'min': identifier not found". Bringing the std:: versions into the
//    global namespace before including GDI+ is the standard workaround,
//    and is what lets build_windows_msvc.bat work. MinGW-w64's own GDI+
//    headers write those comparisons as ternaries and don't need this, so
//    it's a no-op there - but it must come BEFORE <gdiplus.h> either way.
#include <algorithm>
using std::min;
using std::max;

#include <gdiplus.h>
