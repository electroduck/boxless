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

extern HINSTANCE g_hInstance;
static HANDLE s_hPipe = NULL;
static FrameHeader_t s_header;

__declspec(dllexport) void __stdcall AcceptFrame(unsigned nWidth, unsigned nHeight, unsigned nBPP, unsigned nPitch, unsigned flags,
	float fFPS, unsigned char* pData, unsigned char* pPalette)
{
	DWORD nWritten = 0, nError = 0;

	if (s_hPipe == NULL) {
		s_hPipe = CreateNamedPipeA("\\\\.\\pipe\\DOSDisplay", PIPE_ACCESS_OUTBOUND, PIPE_TYPE_MESSAGE, PIPE_UNLIMITED_INSTANCES, 0, 0, HB_WRITE_TIMEOUT, NULL);
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

	if (!WriteFile(s_hPipe, &s_header, sizeof(s_header), &nWritten, NULL)) {
		nError = GetLastError();
		if ((nError != ERROR_TIMEOUT) && (nError != ERROR_PIPE_LISTENING))
			AssertFailed("Writing header to pipe", nError);
	}

	if (!WriteFile(s_hPipe, pData, nHeight * nPitch, &nWritten, NULL)) {
		nError = GetLastError();
		if ((nError != ERROR_TIMEOUT) && (nError != ERROR_PIPE_LISTENING))
			AssertFailed("Writing data to pipe", nError);
	}
}
