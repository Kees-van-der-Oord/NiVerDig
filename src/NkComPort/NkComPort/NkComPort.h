#pragma once
#ifdef NKCOMPORT_EXPORTS
#define NKCOMPORT_API  extern "C" __declspec(dllexport) long __stdcall
struct SNkComPort;
#define NKCOMPORT SNkComPort
#else
#define NKCOMPORT_API  extern "C" __declspec(dllimport) long __stdcall 
#define NKCOMPORT void 
#pragma comment(lib,"NkComPort.lib")
#endif

NKCOMPORT_API NkComPort_Open(NKCOMPORT ** handle, const wchar_t * port);
NKCOMPORT_API NkComPort_IsConnected(NKCOMPORT * handle);
NKCOMPORT_API NkComPort_GetConnectionDetails(NKCOMPORT * handle, wchar_t * port, long buf_size);
NKCOMPORT_API NkComPort_SaveConnectionDetails(NKCOMPORT* handle);
NKCOMPORT_API NkComPort_Close(NKCOMPORT ** handle);
NKCOMPORT_API NkComPort_Read(NKCOMPORT* handle, wchar_t* buffer, long buf_size, unsigned long timeout);
NKCOMPORT_API NkComPort_ReadLine(NKCOMPORT * handle, wchar_t * buffer, long buf_size, unsigned long timeout);
NKCOMPORT_API NkComPort_Write(NKCOMPORT * handle, const wchar_t * buffer, long buf_size);
NKCOMPORT_API NkComPort_WriteLine(NKCOMPORT * handle, const wchar_t * buffer);
NKCOMPORT_API NkComPort_Purge(NKCOMPORT* handle, DWORD flags = PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR);
NKCOMPORT_API NkComPort_SetBuffers(NKCOMPORT* handle, DWORD dwInQueue, DWORD dwOutQueue);
NKCOMPORT_API NkComPort_ListPorts(wchar_t * buffer, long buf_size); // \n separated list of COM ports
NKCOMPORT_API NkComPort_ListPortsEx(wchar_t* buffer, long buf_size); // for each port 3 \t-separated fields: name, friendly name, VID&PID
NKCOMPORT_API NkComPort_SelectPortDialog(HWND hParent, const wchar_t * title, wchar_t * port, long buf_size);

NKCOMPORT_API NkComPort_ReadA(NKCOMPORT* handle, char * buffer, long buf_size, unsigned long timeout);
NKCOMPORT_API NkComPort_WriteA(NKCOMPORT* handle, const char * buffer, long buf_size);
