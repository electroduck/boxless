#include "common.h"
#include <SDL.h>

#define HB_INPUT_BY_WINDOW_MESSAGE 1

#pragma warning(disable:6387)

static DWORD CALLBACK s_InputThreadProc(LPVOID pParam);
static DWORD s_nThreadID = 0;
static HDESK s_hDOSDesktop = NULL; // does not need to be closed

__declspec(dllexport) void __stdcall HookInit(void) {
	HANDLE hThread;

	s_hDOSDesktop = GetThreadDesktop(GetCurrentThreadId());
	HB_ASSERT(s_hDOSDesktop);

	hThread = CreateThread(NULL, 0, s_InputThreadProc, NULL, 0, &s_nThreadID);
	HB_ASSERT(hThread);
	CloseHandle(hThread);

	OutputDebugStringA("Input thread created\r\n");
}

static DWORD CALLBACK s_InputThreadProc(LPVOID pParam) {
	HANDLE hSlot;
	MSG msg;
	DWORD nRead, nWindowPID, nThisPID;
	HWND hWnd = NULL;
	char szBuffer[128];
	SDL_Event evt;

	HB_ASSERT(SetThreadDesktop(s_hDOSDesktop));

	hSlot = CreateMailslotA("\\\\.\\mailslot\\DOSEvent", HB_INPUT_BY_WINDOW_MESSAGE ? sizeof(MSG) : sizeof(SDL_Event), INFINITE, NULL);
	HB_ASSERT((hSlot != NULL) && (hSlot != INVALID_HANDLE_VALUE));

#if HB_INPUT_BY_WINDOW_MESSAGE
	nThisPID = GetCurrentProcessId();
	HB_ASSERT(nThisPID);

	do {
		hWnd = FindWindowExA(NULL, hWnd, "SDL_app", NULL);
		HB_ASSERT(hWnd);
		HB_ASSERT(GetWindowThreadProcessId(hWnd, &nWindowPID));
	} while (nWindowPID != nThisPID);

	while (IsWindow(hWnd)) {
		HB_ASSERT(ReadFile(hSlot, &msg, sizeof(msg), &nRead, NULL));
		SendMessageA(hWnd, msg.message, msg.wParam, msg.lParam);
		sprintf_s(szBuffer, sizeof(szBuffer), "Sent message %04X (%08X, %08X) to HWND %08X\r\n", msg.message, msg.wParam, msg.lParam, (unsigned)hWnd);
		OutputDebugStringA(szBuffer);
	}
#else
	for (;;) {
		HB_ASSERT(ReadFile(hSlot, &evt, sizeof(evt), &nRead, NULL));
		HB_ASSERT(SDL_PushEvent(&evt) >= 0);
		OutputDebugStringA("Pushed event\r\n");
	}
#endif

	CloseHandle(hSlot);
	return 0;
}
