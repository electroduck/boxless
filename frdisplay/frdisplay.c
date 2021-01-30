#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <TlHelp32.h>
#include <stdio.h>
#include <SDL.h>

#pragma warning(disable:6387)
#pragma warning(disable:28183)

#define FD_ASSERT(x) (!!(x) || (DebugBreak(), s_AssertFailed(#x, GetLastError())))

typedef struct _struct_FrameHeader {
	unsigned m_nWidth;
	unsigned m_nHeight;
	unsigned m_nBPP;
	unsigned m_nPitch;
	unsigned m_flags;
	float m_fFPS;
	unsigned m_arrPalette[256];
} FrameHeader_t;

typedef struct _struct_FullBitmapInfo {
	BITMAPINFOHEADER m_header;
	RGBQUAD m_arrPalette[256];
} FullBitmapInfo_t;

static int s_AssertFailed(LPCSTR pcszCode, DWORD nLastError);
static LRESULT CALLBACK s_DisplayWindowProc(HWND hWnd, UINT nMessage, WPARAM wParam, LPARAM lParam);
static DWORD CALLBACK s_ReaderThreadProc(LPVOID pParam);
static void s_ProcessKeyboardInput(UINT nMessage, WPARAM wParam, LPARAM lParam);
static DWORD s_ShowPIDMenu(void);
static void s_ReplacementPrintf(LPCSTR pcszFormat, ...);

static const char s_cszDisplayWindowClass[] = "FrameDisplayWindow";
static HWND s_hWndDisplay = NULL;
static HBITMAP s_hFrameBitmap = NULL;
static int s_nFrameWidth = 0, s_nFrameHeight = 0;
static CRITICAL_SECTION s_csFrameBitmap;
static HANDLE s_hSlotEvents = NULL;
static DWORD s_nDOSBoxPID = 0;
static char s_szDOSEventSlotName[MAX_PATH];
static char s_szDOSFramePipeName[MAX_PATH];

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pszCmdLine, int nShowCmd) {
	WNDCLASSEXA wcxa;
	HANDLE hThreadRead;
	DWORD nReaderThreadID;
	MSG msg;

	s_nDOSBoxPID = s_ShowPIDMenu();
	if (s_nDOSBoxPID == 0) return 0;
	sprintf_s(s_szDOSEventSlotName, sizeof(s_szDOSEventSlotName), "\\\\.\\mailslot\\DOSEvent%u", s_nDOSBoxPID);
	sprintf_s(s_szDOSFramePipeName, sizeof(s_szDOSFramePipeName), "\\\\.\\pipe\\DOSDisplay%u", s_nDOSBoxPID);

	InitializeCriticalSection(&s_csFrameBitmap);

	s_hSlotEvents = CreateFileA(s_szDOSEventSlotName, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	FD_ASSERT((s_hSlotEvents != NULL) && (s_hSlotEvents != INVALID_HANDLE_VALUE));

	ZeroMemory(&wcxa, sizeof(wcxa));
	wcxa.cbSize = sizeof(wcxa);
	wcxa.lpfnWndProc = s_DisplayWindowProc;
	wcxa.lpszClassName = s_cszDisplayWindowClass;
	wcxa.hCursor = LoadCursor(NULL, IDC_ARROW);
	FD_ASSERT(RegisterClassExA(&wcxa));

	s_hWndDisplay = CreateWindowExA(0, s_cszDisplayWindowClass, "Frame Display", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
		640, 480, NULL, NULL, hInstance, NULL);
	FD_ASSERT(s_hWndDisplay);

	hThreadRead = CreateThread(NULL, 0, s_ReaderThreadProc, NULL, 0, &nReaderThreadID);
	FD_ASSERT(hThreadRead);

	while (GetMessageA(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessageA(&msg);
	}

	CloseHandle(s_hSlotEvents);
	return 0;
}

static int s_AssertFailed(LPCSTR pcszCode, DWORD nLastError) {
	char szBuf[512];

	sprintf_s(szBuf, sizeof(szBuf), "Assertion failed: %s\r\nLast Win32 error: 0x%08X", pcszCode, nLastError);

	OutputDebugStringA(szBuf);
	OutputDebugStringA("\r\n");

	if (MessageBoxA(NULL, szBuf, "Error", MB_ICONERROR | MB_APPLMODAL | MB_CANCELTRYCONTINUE) == IDCANCEL)
		ExitProcess(1);

	return 0;
}

static LRESULT CALLBACK s_DisplayWindowProc(HWND hWnd, UINT nMessage, WPARAM wParam, LPARAM lParam) {
	PAINTSTRUCT ps;
	HDC hTempDC;
	HGDIOBJ hOldBitmap;
	RECT rcClient;
	int nPrevStretchMode;

	switch (nMessage) {
	case WM_CLOSE:
		FD_ASSERT(DestroyWindow(hWnd));
		return 0;

	case WM_PAINT:
		FD_ASSERT(GetClientRect(hWnd, &rcClient));
		EnterCriticalSection(&s_csFrameBitmap);
		FD_ASSERT(BeginPaint(hWnd, &ps));
		if (s_hFrameBitmap) {
			FD_ASSERT(hTempDC = CreateCompatibleDC(ps.hdc));
			FD_ASSERT(hOldBitmap = SelectObject(hTempDC, s_hFrameBitmap));
			nPrevStretchMode = SetStretchBltMode(ps.hdc, HALFTONE);
			FD_ASSERT(StretchBlt(ps.hdc, 0, 0, rcClient.right, rcClient.bottom, hTempDC, 0, 0, s_nFrameWidth, s_nFrameHeight, SRCCOPY));
			SetStretchBltMode(ps.hdc, nPrevStretchMode);
			SelectObject(hTempDC, hOldBitmap);
			DeleteDC(hTempDC);
		} else {
			FillRect(ps.hdc, &rcClient, GetStockObject(BLACK_BRUSH));
		}
		EndPaint(hWnd, &ps);
		FD_ASSERT(GdiFlush());
		LeaveCriticalSection(&s_csFrameBitmap);
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	case WM_KEYDOWN:
	case WM_KEYUP:
		s_ProcessKeyboardInput(nMessage, wParam, lParam);
		return 0;

	default:
		return DefWindowProcA(hWnd, nMessage, wParam, lParam);
	}
}

static DWORD CALLBACK s_ReaderThreadProc(LPVOID pParam) {
	HANDLE hPipe;
	FrameHeader_t header;
	DWORD nRead;
	BYTE* pData = NULL;
	HANDLE hHeap;
	HDC hScreenDC, hFrameDC;
	HGDIOBJ hOldBitmap;
	FullBitmapInfo_t fbmi;
	size_t nDataBytes, nDataBytesLast = 0;
	RECT rcClear = { 0 };
	unsigned nEntry;

	FD_ASSERT(hHeap = GetProcessHeap());
	FD_ASSERT(hPipe = CreateFileA(s_szDOSFramePipeName, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL));
	FD_ASSERT(hPipe != INVALID_HANDLE_VALUE);

	FD_ASSERT(hScreenDC = GetDC(NULL));
	FD_ASSERT(hFrameDC = CreateCompatibleDC(hScreenDC));
	ReleaseDC(NULL, hScreenDC);

	for (;;) {
		// read header
		FD_ASSERT(ReadFile(hPipe, &header, sizeof(header), &nRead, NULL));
		nDataBytes = header.m_nHeight * header.m_nPitch;
		if (!pData || (nDataBytes != nDataBytesLast)) {
			if (pData) FD_ASSERT(HeapFree(hHeap, 0, pData));
			FD_ASSERT(pData = HeapAlloc(hHeap, 0, nDataBytes));
			nDataBytesLast = nDataBytes;
		}

		// read data
		FD_ASSERT(ReadFile(hPipe, pData, nDataBytes, &nRead, NULL));

		fbmi.m_header.biBitCount = header.m_nBPP;
		fbmi.m_header.biClrImportant = 0;
		fbmi.m_header.biClrUsed = 1 << header.m_nBPP;
		fbmi.m_header.biCompression = BI_RGB;
		fbmi.m_header.biHeight = -(int)header.m_nHeight;
		fbmi.m_header.biPlanes = 1;
		fbmi.m_header.biSize = sizeof(fbmi.m_header);
		fbmi.m_header.biSizeImage = nDataBytes;
		fbmi.m_header.biWidth = header.m_nWidth;
		fbmi.m_header.biXPelsPerMeter = 0;
		fbmi.m_header.biYPelsPerMeter = 0;
		if (header.m_nBPP <= 8) {
			for (nEntry = 0; nEntry < fbmi.m_header.biClrUsed; nEntry++) {
				fbmi.m_arrPalette[nEntry].rgbRed   =  header.m_arrPalette[nEntry] & 0x0000'00FF;
				fbmi.m_arrPalette[nEntry].rgbGreen = (header.m_arrPalette[nEntry] & 0x0000'FF00) >> 8;
				fbmi.m_arrPalette[nEntry].rgbBlue  = (header.m_arrPalette[nEntry] & 0x00FF'0000) >> 16;
				fbmi.m_arrPalette[nEntry].rgbReserved = 0xFF;
			}
		}

		rcClear.right = header.m_nWidth;
		rcClear.bottom = header.m_nHeight;

		EnterCriticalSection(&s_csFrameBitmap);

		if ((s_nFrameWidth != header.m_nWidth) || (s_nFrameHeight != header.m_nHeight)) {
			if (s_hFrameBitmap) DeleteObject(s_hFrameBitmap);

			FD_ASSERT(hScreenDC = GetDC(NULL));
			FD_ASSERT(s_hFrameBitmap = CreateCompatibleBitmap(hScreenDC, header.m_nWidth, header.m_nHeight));
			ReleaseDC(NULL, hScreenDC);

			s_nFrameWidth = header.m_nWidth;
			s_nFrameHeight = header.m_nHeight;
		}

		FD_ASSERT(hOldBitmap = SelectObject(hFrameDC, s_hFrameBitmap));
		FD_ASSERT(FillRect(hFrameDC, &rcClear, GetStockObject(GRAY_BRUSH)));
		FD_ASSERT(StretchDIBits(hFrameDC, 0, 0, header.m_nWidth, header.m_nHeight, 0, 0, header.m_nWidth, header.m_nHeight, pData,
			&fbmi, DIB_RGB_COLORS, SRCCOPY));
		SelectObject(hFrameDC, hOldBitmap);

		FD_ASSERT(GdiFlush());
		LeaveCriticalSection(&s_csFrameBitmap);

		InvalidateRect(s_hWndDisplay, NULL, FALSE);
	}
}

static void s_ProcessKeyboardInput(UINT nMessage, WPARAM wParam, LPARAM lParam) {
	MSG msg = { 0 };
	DWORD nWritten = 0;

	msg.message = nMessage;
	msg.wParam = wParam;
	msg.lParam = lParam;

	FD_ASSERT(WriteFile(s_hSlotEvents, &msg, sizeof(msg), &nWritten, NULL));
}

static DWORD s_ShowPIDMenu(void) {
	HWND hConsoleWindow;
	PROCESSENTRY32 entry;
	HANDLE hSnapshot;
	char* pszPathChar;
	long nPID;
	char szPID[80];
	DWORD nRead;

	FD_ASSERT(AllocConsole());
	FD_ASSERT(hConsoleWindow = GetConsoleWindow());
	ShowWindow(hConsoleWindow, SW_SHOW);

	s_ReplacementPrintf("DOSBox processes:\r\n");

	hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	FD_ASSERT(hSnapshot != INVALID_HANDLE_VALUE);

	entry.dwSize = sizeof(entry);
	FD_ASSERT(Process32First(hSnapshot, &entry));

	do {
		for (pszPathChar = entry.szExeFile; *pszPathChar; pszPathChar++)
			*pszPathChar = toupper(*pszPathChar);

		if (strstr(entry.szExeFile, "DOSBOX"))
			s_ReplacementPrintf("%10u\t%s\r\n", entry.th32ProcessID, entry.szExeFile);
	} while (Process32Next(hSnapshot, &entry));

	CloseHandle(hSnapshot);

	nPID = 0; 
	while (!nPID) {
		s_ReplacementPrintf("Enter target PID, or negative to cancel: ");
		FD_ASSERT(ReadFile(GetStdHandle(STD_INPUT_HANDLE), szPID, sizeof(szPID), &nRead, NULL));
		nPID = strtol(szPID, NULL, 0);
	}

	ShowWindow(hConsoleWindow, SW_HIDE);
	FreeConsole();

	return (nPID < 0) ? 0 : (DWORD)nPID;
}

static void s_ReplacementPrintf(LPCSTR pcszFormat, ...) {
	char szBuffer[256];
	va_list va;
	DWORD nWritten;

	va_start(va, pcszFormat);
	vsprintf_s(szBuffer, sizeof(szBuffer), pcszFormat, va);
	va_end(va);

	FD_ASSERT(WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), szBuffer, strlen(szBuffer), &nWritten, NULL));
}