#pragma once

#if defined(_WIN32) && defined(_MSC_VER)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

static inline void WINAPI hxcppTestFutureSystemTime(LPSYSTEMTIME result)
{
    SYSTEMTIME value = {};
    value.wYear = 2040;
    value.wMonth = 1;
    value.wDay = 1;
    value.wMilliseconds = 125;
    *result = value;
}

#define GetSystemTime hxcppTestFutureSystemTime
#define HXCPP_TEST_FUTURE_CLOCK 1
#endif
