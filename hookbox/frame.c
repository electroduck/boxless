#include "common.h"
#define HB_WRITE_TIMEOUT 50

typedef struct _struct_FrameHeader {
	unsigned m_nWidth;
	unsigned m_nHeight;
	unsigned m_nBPP;
	unsigned m_nPitch;
	unsigned m_flags;
	float m_fFPS;
	unsigned m_arrPalette[256];
} FrameHeader_t;

static void s_InitDispWindow(void);
static BOOL s_CheckPipeWrite(BOOL bOK);

extern HINSTANCE g_hInstance;
static HANDLE s_hPipe = NULL;
static FrameHeader_t s_header;
static char s_szPipeName[128];

__declspec(dllexport) void __stdcall AcceptFrame(unsigned nWidth, unsigned nHeight, unsigned nBPP, unsigned nPitch, unsigned flags,
	float fFPS, unsigned char* pData, unsigned char* pPalette)
{
	DWORD nWritten = 0, nError = 0;

	if (s_hPipe == NULL) {
		sprintf_s(s_szPipeName, sizeof(s_szPipeName), "\\\\.\\pipe\\DOSDisplay%u", GetCurrentProcessId());
		s_hPipe = CreateNamedPipeA(s_szPipeName, PIPE_ACCESS_OUTBOUND, PIPE_TYPE_MESSAGE, PIPE_UNLIMITED_INSTANCES, 0, 0, HB_WRITE_TIMEOUT, NULL);
		HB_ASSERT((s_hPipe != NULL) && s_hPipe != INVALID_HANDLE_VALUE);
	}

	s_header.m_nWidth = nWidth;
	s_header.m_nHeight = nHeight;
	s_header.m_nBPP = nBPP;
	s_header.m_nPitch = nPitch;
	s_header.m_flags = flags;
	s_header.m_fFPS = fFPS;
	if (nBPP <= 8) {
		memcpy(s_header.m_arrPalette, pPalette, nBPP * 8 * 4);
		if (nBPP < 8)
			memset(&s_header.m_arrPalette[nBPP * 8], 0, (8 - nBPP) * 8 * 4);
	}

	if (s_CheckPipeWrite(WriteFile(s_hPipe, &s_header, sizeof(s_header), &nWritten, NULL)))
		s_CheckPipeWrite(WriteFile(s_hPipe, pData, nHeight * nPitch, &nWritten, NULL));
}

static BOOL s_CheckPipeWrite(BOOL bOK) {
	DWORD nError;

	if (!bOK) {
		nError = GetLastError();
		switch (nError) {
		case ERROR_NO_DATA:
		case ERROR_BROKEN_PIPE:
			// other end closed
			CloseHandle(s_hPipe);
			s_hPipe = NULL;
			return FALSE;

		case ERROR_TIMEOUT:
		case ERROR_PIPE_LISTENING:
			// skip frame
			return FALSE;

		default:
			AssertFailed("Writing header to pipe", nError);
		}
	}

	return TRUE;
}
