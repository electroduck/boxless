#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>

#define HB_ASSERT(x) (!!(x) || AssertFailed(#x, GetLastError()))

int AssertFailed(LPCSTR pcszCode, DWORD nLastError);

extern HINSTANCE g_hInstance;
