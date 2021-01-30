#include "common.h"

HINSTANCE g_hInstance = NULL;

int AssertFailed(LPCSTR pcszCode, DWORD nLastError) {
	char szBuf[512];
	DWORD nWritten;

	sprintf_s(szBuf, sizeof(szBuf), "Assertion failed: %s\r\nLast Win32 error: 0x%08X", pcszCode, nLastError);

	OutputDebugStringA(szBuf);
	OutputDebugStringA("\r\n");

	WriteFile(GetStdHandle(STD_ERROR_HANDLE), szBuf, strlen(szBuf), &nWritten, NULL);

	if (MessageBoxA(NULL, szBuf, "Error", MB_ICONERROR | MB_APPLMODAL | MB_CANCELTRYCONTINUE) == IDCANCEL)
		ExitProcess(1);

	return 0;
}
