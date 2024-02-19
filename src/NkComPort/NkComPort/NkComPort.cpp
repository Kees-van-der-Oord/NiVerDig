// NkComPortUSB.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include "NkComPort.h"
#include "resource.h"
#if _MSC_VER > 1500
#include <setupapi.h>
#include <initguid.h>  // Put this in to get rid of linker errors.  
#include <Devpkey.h>
#include <winioctl.h>
#include <usbioctl.h>
#include <cfgmgr32.h>
#include "usbdesc.h"
#pragma comment(lib,"setupapi.lib")
#pragma comment(lib,"uuid.lib")
#endif

HMODULE g_hInstance = NULL;

#define countof(A) (sizeof(A) / sizeof((A)[0]))

BOOL APIENTRY DllMain( HMODULE hModule, 
                       DWORD  ul_reason_for_call, 
                       LPVOID lpReserved
					 )
{
    switch (ul_reason_for_call)
	{
		case DLL_PROCESS_ATTACH:
			g_hInstance = hModule;
			DisableThreadLibraryCalls(hModule);
			break;
		case DLL_THREAD_ATTACH:
		case DLL_THREAD_DETACH:
		case DLL_PROCESS_DETACH:
			break;
    }
    return TRUE;
}

#define MAX_PORT_NAME 260
#define PORTFORMAT L"%260[^: \t\n\r]"
#define MAX_STRINGLEN 1024

static const CLSID nkcomport_clsid =
   { 0xE310BB12, 0xCE10, 0x4B88, { 0xB4, 0xCA, 0x48, 0x9D, 0xC4, 0x19, 0xBC, 0x71 } };

// serial part
#define RXQUEUE         4096
#define TXQUEUE         4096
#define FC_DTRDSR       0x01
#define FC_RTSCTS       0x02
#define FC_XONXOFF      0x04
#define ASCII_BEL       0x07
#define ASCII_BS        0x08
#define ASCII_LF        0x0A
#define ASCII_CR        0x0D
#define ASCII_XON       0x11
#define ASCII_XOFF      0x13

typedef enum eHandshake {
	comNone       = 0, // (Default) No handshaking. 
	comXOnXOff    = 1, // XON/XOFF handshaking. 
	comRTS        = 2, // RTS/CTS (Request To Send/Clear To Send) handshaking. 
	comRTSXOnXOff = 3, // Both Request To Send and XON/XOFF handshaking.
} eHandshake;

typedef enum eParity {
	parityNone = NOPARITY,   // None
	parityOdd  = ODDPARITY,  // Odd
	parityEven = EVENPARITY, // Even 
	parityMark = MARKPARITY, // Mark 
	paritySpace= SPACEPARITY,// Space 
} eParity;

typedef enum eStopbit {
	stopbitOne     = ONESTOPBIT,
	stopbitOneHalf = ONE5STOPBITS,
	stopbitTwo     = TWOSTOPBITS,
} eStopbit;

struct SNkComPort
{
	DWORD           size;
	CLSID           clsid;
	HANDLE			port;
	wchar_t         name[MAX_PORT_NAME];
	int		        baudrate;
	eParity         parity;
	int             databits;
	eStopbit        stopbits;
	eHandshake      handshake;
	DWORD           timeout;
	char            buf[MAX_STRINGLEN];
	long            buf_count;
	SNkComPort()
	{
		size = sizeof(SNkComPort);
		memcpy(&clsid,&nkcomport_clsid,sizeof(CLSID));
		port = INVALID_HANDLE_VALUE;
		name[0] = 0;
		baudrate = 9600;
		parity = parityNone;
		databits = 8; 
		stopbits = stopbitOne;
		handshake = comNone;
		timeout = 100;
		buf[0] = 0;
		buf_count = 0;
	}
	~SNkComPort()
	{
		Close();
		memset(&clsid,0,sizeof(CLSID));
		size = 0;
	}
	long Open(const wchar_t * port);
	long TryAllPorts();
	long Close(void);
	long IsConnected(wchar_t * port, long buf_size);
	long Read(wchar_t * buffer, long buf_size, DWORD timeout);
	long ReadA(char* buffer, long buf_size, DWORD timeout);
	long ReadLine(wchar_t * buffer, long buf_size, DWORD timeout);
	long Write(const wchar_t * buffer, long buf_size);
	long WriteA(const char* buffer, long buf_size);
	long WriteLine(const wchar_t * buffer);
	long Setup(const wchar_t * options);
	void SetTimeOut(DWORD ms);
	bool ParseName(const wchar_t * port_name);
	void ParseDefaultOptions();
	void ParseOptions(const wchar_t * options);
	bool ParsePort(const wchar_t * port_name);
	void AddSettings();
	long Purge(DWORD flags);
	long SetBuffers(DWORD dwInQueue, DWORD dwOutQueue);
};

typedef struct SNkCSLock
{
	CRITICAL_SECTION * m_cs;
	LONG               m_count;
	SNkCSLock(CRITICAL_SECTION * cs, BOOL lock = TRUE)
		: m_cs(cs)
		, m_count(0)
	{
		m_cs = cs;
		if(lock) Lock();
	}
	~SNkCSLock()
	{
		while(m_count) Unlock();
	}
	void Lock()
	{
		EnterCriticalSection(m_cs);
		++m_count;
	}
	void Unlock()
	{
		LeaveCriticalSection(m_cs);
		--m_count;
	}
} SNkCsLock;

struct SNkComPortArray
{
	CRITICAL_SECTION m_cs;
	SNkComPort  **    m_data;
	DWORD            m_count;
	DWORD            m_size;
public:
	SNkComPortArray()
	{
		m_data  = NULL;
		m_count = 0;
		m_size  = 0;
		InitializeCriticalSection(&m_cs);
	}
	~SNkComPortArray()
	{
		remove_all();
		free(m_data);
		DeleteCriticalSection(&m_cs);
	}
	void remove_all()
	{
		SNkCSLock l(&m_cs);
		while(m_count)
		{
			SNkComPort * p = m_data[m_count-1];
			_remove(m_count - 1);
		}
	}
	void remove(SNkComPort * p)
	{
		SNkCSLock l(&m_cs);
		for(DWORD i = 0; i < m_count; ++i)
		{
			if(m_data[i] == p)
			{
				_remove(i);
				return;
			}
		}
	}
	void _remove(DWORD i)
	{
		if(i >= m_count) return;
		delete m_data[i];
		--m_count;
		for(; i < m_count; ++i)
		{
			m_data[i] = m_data[i+1];
		}
	}
	void add(SNkComPort * p)
	{
		SNkCSLock l(&m_cs);
		alloc(m_count + 1);
		m_data[m_count] = p;
		++m_count;
	}
	void alloc(DWORD size)
	{
		if(size > m_size)
		{
			m_data = (SNkComPort**)realloc(m_data,(size) * sizeof(SNkComPort*));
			m_size = size;
		}
	}
} g_nkcomports;

// return TRUE if:
//   the location can hold a pointer and is currently NULL
//   or
//   the location has a valid pointer
BOOL is_nkcomport_pointer_valid(SNkComPort **ppTs2)
{
	try {
		if(!ppTs2) return FALSE;
		if(IsBadWritePtr(ppTs2, sizeof(SNkComPort*))) return FALSE;
		SNkComPort * pTs2 = *ppTs2;
		if(!pTs2) return TRUE;
		if(IsBadWritePtr(pTs2, sizeof(SNkComPort))) return FALSE;
		if(pTs2->size != sizeof(SNkComPort)) return FALSE;
		if(memcmp(&pTs2->clsid,&nkcomport_clsid ,sizeof(CLSID))) return FALSE;
		return TRUE;
	} catch(...) {}
	return FALSE;
}

BOOL is_string_valid_read(const wchar_t * s, unsigned long max_length)
{
	try {
		if(!s) return FALSE;
		for(; max_length; --max_length, ++s)
		{
			if(IsBadReadPtr(s, sizeof(wchar_t))) return FALSE;
			if(!*s) return TRUE;
		}
	} catch(...) {}
	return FALSE;
}

BOOL is_string_valid_write(wchar_t * s, unsigned long max_length)
{
	try {
		if(!s) return FALSE;
		if(IsBadWritePtr(s, sizeof(wchar_t) * max_length)) return FALSE;
		return TRUE;
	} catch(...) {}
	return FALSE;
}

// return TRUE if the location has a valid pointer
BOOL check_nkcomport(SNkComPort **ppTs2)
{
	if(!is_nkcomport_pointer_valid(ppTs2)) return FALSE;
	if(*ppTs2) return TRUE;
	return FALSE;
}

NKCOMPORT_API NkComPort_Open(NKCOMPORT ** handle, const wchar_t * port)
{
	if(!is_nkcomport_pointer_valid(handle)) return E_INVALIDARG;
	if(!is_string_valid_read(port,MAX_PORT_NAME)) return E_INVALIDARG;
	if(NkComPort_IsConnected(*handle)) return TRUE;
	if(!*handle)
	{
		*handle = new SNkComPort();
		g_nkcomports.add(*handle);
	}
	return (*handle)->Open(port);
}

NKCOMPORT_API NkComPort_IsConnected(NKCOMPORT * handle)
{
	if(!check_nkcomport(&handle)) return FALSE;
	return handle->IsConnected(NULL,0);
}

NKCOMPORT_API NkComPort_GetConnectionDetails(NKCOMPORT * handle, wchar_t * port, long buf_size)
{
	if(!check_nkcomport(&handle)) return FALSE;
	return handle->IsConnected(port,buf_size);
}

NKCOMPORT_API NkComPort_SaveConnectionDetails(NKCOMPORT* handle)
{
	if (!check_nkcomport(&handle)) return FALSE;
	wchar_t name[64] = { 0 };
	if (!NkComPort_GetConnectionDetails(handle, name, 64)) return FALSE;
	size_t colon = wcscspn(name, L":");
	if (!name[colon] || (colon >= 63)) return FALSE;
	HKEY key = NULL;
	long result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Ports", 0, KEY_READ | KEY_SET_VALUE, &key);
	if (!key) return FALSE;
	wchar_t value_name[64];
	++colon;
	wcsncpy_s(value_name, 64, name+4, colon-4);
	RegSetValueEx(key, value_name, NULL, REG_SZ, (LPBYTE)(name + colon), DWORD(wcslen(name + colon) * 2 + 2));
	RegCloseKey(key);
	return TRUE;
}

NKCOMPORT_API NkComPort_Close(NKCOMPORT ** handle)
{
	if(!check_nkcomport(handle)) return FALSE;
	long result = (*handle)->Close();
	g_nkcomports.remove(*handle);
	*handle = NULL;
	return result;
}

NKCOMPORT_API NkComPort_ReadA(NKCOMPORT* handle, char* buffer, long buf_size, unsigned long timeout)
{
	if (!check_nkcomport(&handle)) return -1;
	return handle->ReadA(buffer, buf_size, timeout);
}

NKCOMPORT_API NkComPort_Read(NKCOMPORT * handle, wchar_t * buffer, long buf_size, unsigned long timeout)
{
	if(!check_nkcomport(&handle)) return -1;
	return handle->Read(buffer,buf_size,timeout);
}

NKCOMPORT_API NkComPort_ReadLine(NKCOMPORT * handle, wchar_t * buffer, long buf_size, unsigned long timeout)
{
	if(!check_nkcomport(&handle)) return -1;
	return handle->ReadLine(buffer,buf_size,timeout);
}

NKCOMPORT_API NkComPort_WriteA(NKCOMPORT* handle, const char* buffer, long buf_size)
{
	if (!check_nkcomport(&handle)) return -1;
	return handle->WriteA(buffer, buf_size);
}

NKCOMPORT_API NkComPort_Write(NKCOMPORT * handle, const wchar_t * buffer, long buf_size)
{
	if(!check_nkcomport(&handle)) return -1;
	return handle->Write(buffer,buf_size);
}

NKCOMPORT_API NkComPort_WriteLine(NKCOMPORT * handle, const wchar_t * buffer)
{
	if(!check_nkcomport(&handle)) return -1;
	return handle->WriteLine(buffer);
}

NKCOMPORT_API NkComPort_Purge(NKCOMPORT* handle, DWORD flags)
{
	if (!check_nkcomport(&handle)) return -1;
	return handle->Purge(flags);
}

NKCOMPORT_API NkComPort_SetBuffers(NKCOMPORT* handle, DWORD dwInQueue, DWORD dwOutQueue)
{
	if (!check_nkcomport(&handle)) return -1;
	return handle->SetBuffers(dwInQueue,dwOutQueue);
}

long SNkComPort::TryAllPorts()
{
	wchar_t buffer[2048];
	NkComPort_ListPorts(buffer,2048);
	for(wchar_t * p = buffer; *p; p += wcscspn(p,L"\n"))
	{
		if(*p == '\n') ++p;
		if(Open(p) == TRUE)
		{
			return TRUE;
		}
	}
	return -1;
}

bool SNkComPort::ParsePort(const wchar_t * port_name)
{
	bool ok = ParseName(port_name);
	ParseDefaultOptions();
	ParseOptions(port_name);
	return ok;
}

bool SNkComPort::ParseName(const wchar_t * port_name)
{
	name[0] = 0;
	size_t len = MAX_PORT_NAME;
	if(wcsncmp(port_name,L"\\\\.\\",4))
	{
		wcscpy_s(name,len,L"\\\\.\\");
		len -= 4;
	}
	size_t n = wcscspn(port_name,L" :\t\r\n");
	wcsncat_s(name,len,port_name,n);
	return n > 0;
}

long SNkComPort::Open(const wchar_t * port_name)
{
	if(!port_name || !port_name[0])
	{
		return TryAllPorts();
	}
	Close();
	ParseName(port_name);
	port = CreateFile(
		name,
		GENERIC_READ | GENERIC_WRITE,
		0,                    // exclusive access
		NULL,                 // no security attrs
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL );
	if(port == INVALID_HANDLE_VALUE)
	{
		return FALSE;
	}
	if(!Setup(port_name))
	{
		return FALSE;
	}
	//Sleep(2000); // arduino Uno reboots after connect ..
	return TRUE;
}

long SNkComPort::Close(void)
{
	if(port == INVALID_HANDLE_VALUE) return TRUE;
	::CloseHandle(port);
	port = INVALID_HANDLE_VALUE;
	name[0] = 0;
	buf[0] = 0;
	buf_count = 0;
	return TRUE;
}

long SNkComPort::IsConnected(wchar_t * port_name, long buf_size)
{
	if(port == INVALID_HANDLE_VALUE) return FALSE;
	if(!port_name) return TRUE;
	if(buf_size >= MAX_PORT_NAME) return E_INVALIDARG;;
	if(!is_string_valid_write(port_name,buf_size)) return E_INVALIDARG;
	long n = (long) wcslen(name);
	if(n >= buf_size) n = buf_size - 1;
	wcsncpy_s(port_name,buf_size,name,n);
	return TRUE;
}

long SNkComPort::ReadA(char* buffer, long buf_size, DWORD timeout)
{
	if (buf_size < 1) return E_INVALIDARG;
	//if (buf_size >= MAX_STRINGLEN) return E_INVALIDARG;
	//if (!is_string_valid_write(buffer, buf_size)) return E_INVALIDARG;
	//buffer[0] = 0;
	ULONGLONG dwTimeoutTick = GetTickCount64() + timeout;
	if (!IsConnected(NULL, 0)) return RPC_E_DISCONNECTED;
	long buf_count = 0;
	while (buf_count < buf_size)
	{
		DWORD dwRead = 0;
		if (!ReadFile(port, buffer + buf_count, buf_size-buf_count, &dwRead, NULL))
		{
			int err = GetLastError();
			if (err > 0) return -err;
		}
		buf_count += dwRead;
		if (buf_count < buf_size)
		{
			if (GetTickCount64() > dwTimeoutTick)
			{
				break;
			}
			continue;
		}
	}
	return buf_count;
}

long SNkComPort::Read(wchar_t * buffer, long buf_size, DWORD timeout)
{
	if(buf_size < 1) return E_INVALIDARG;
	if(buf_size >= MAX_STRINGLEN) return E_INVALIDARG;
	if(!is_string_valid_write(buffer,buf_size)) return E_INVALIDARG;
	--buf_size; // allow for the terminating 0
	buffer[0] = 0;
	ULONGLONG dwTimeoutTick = GetTickCount64() + timeout;
	if(!IsConnected(NULL,0)) return RPC_E_DISCONNECTED;
	while(buf_count < buf_size)
	{
		DWORD dwRead = 0;
		if(!ReadFile(port,buf + buf_count,1,&dwRead,NULL))
		{
			int err = GetLastError();
			if(err > 0) return -err;
		}
		if(!dwRead)
		{
			if(GetTickCount64() > dwTimeoutTick) 
			{
				break;
			}
			continue;
		}
		++buf_count;
		buf[buf_count] = 0;
	}
	long cr = buf_size;
	if(cr > buf_count) cr = buf_count;
	if (cr)
	{
		long cn = MultiByteToWideChar(CP_UTF8, 0, buf, cr, buffer, buf_size);
		if (cn >= 0) buffer[cn] = 0;
		long len = buf_count - cr;
		memmove(buf, buf + cr, len);
		buf_count = len;
		buf[buf_count] = 0;
	}
	return cr;
}

long SNkComPort::ReadLine(wchar_t * buffer, long buf_size, DWORD timeout)
{
	if(buf_size < 2) return E_INVALIDARG;
	if(!is_string_valid_write(buffer,buf_size)) return E_INVALIDARG;
	--buf_size; // allow for the terminating 0
	buffer[0] = 0;
	ULONGLONG dwTimeoutTick = GetTickCount64() + timeout;
	if(!IsConnected(NULL,0)) return RPC_E_DISCONNECTED;
	if(!strchr(buf,'\r')) 
	{
		for(;;)
		{
			if(buf_count >= (MAX_STRINGLEN-1))
			{
				break;
			}
			DWORD dwRead = 0;
			if(!ReadFile(port,buf + buf_count,1,&dwRead,NULL))
			{
				int err = GetLastError();
				if(err > 0) return -err;
			}
			if(!dwRead)
			{
				if(GetTickCount64() > dwTimeoutTick) 
				{
					break;
				}
				continue;
			}
			++buf_count;
			buf[buf_count] = 0;
			if(buf_count >= buf_size)
			{
				break;
			}
			//if(!g_bBinaryMode)
			{
				if(buf[buf_count-1] == '\n')
				{
					break;
				}
			}
		}
	}
	long cr = (long) strcspn(buf,"\r\n");
	if(buf[cr] == '\r') ++cr;
	if(buf[cr] == '\n') ++cr;
	if (cr)
	{
		long cn = MultiByteToWideChar(CP_UTF8, 0, buf, cr, buffer, buf_size);
		while(cr && (cn == 0))
		{
			--cr;
			cn = MultiByteToWideChar(CP_UTF8, 0, buf, cr, buffer, buf_size);
		}
		if (cn >= 0) buffer[cn] = 0;
		long len = (long)strlen(buf + cr);
		memmove(buf, buf + cr, len);
		buf_count = len;
		buf[buf_count] = 0;
	}
	return cr;
}

long SNkComPort::WriteA(const char* buffer, long buf_size)
{
	if (port == INVALID_HANDLE_VALUE) return FALSE;
	//if (!is_string_valid_read(buffer, MAX_STRINGLEN)) return E_INVALIDARG;
	long len = buf_size;
	if (len <= 0) { len = (long)strlen(buffer); }
	DWORD written = 0;
	if (!WriteFile(port, buffer, len, &written, NULL))
	{
		return GetLastError() | 0x80000000;
	}
	return written;
}

long SNkComPort::Write(const wchar_t * buffer, long buf_size)
{
	if(port == INVALID_HANDLE_VALUE) return FALSE;
	if(!is_string_valid_read(buffer,MAX_STRINGLEN)) return E_INVALIDARG;
	long len = buf_size;
	if(len <= 0) { len = (long)wcslen(buffer); }
	char bufA[MAX_STRINGLEN*2+1] = "";
	WideCharToMultiByte(CP_UTF8,0,buffer,len,bufA,MAX_STRINGLEN*2,NULL,NULL);
	DWORD written = 0;
	len = (long) strlen(bufA);
	if(!WriteFile(port,bufA,len,&written,NULL))
	{
		return GetLastError() | 0x80000000;
	}
	return written;
}

long SNkComPort::WriteLine(const wchar_t * buffer)
{
	if(port == INVALID_HANDLE_VALUE) return FALSE;
	if(!is_string_valid_read(buffer,MAX_STRINGLEN)) return E_INVALIDARG;
	long len = (long) wcslen(buffer);
	char bufA[MAX_STRINGLEN*2+1] = "";
	WideCharToMultiByte(CP_UTF8,0,buffer,len,bufA,MAX_STRINGLEN*2,NULL,NULL);
	DWORD written = 0;
	len = (long) strlen(bufA);
	if(!strchr("\r\n",bufA[len-1]))
	{
		bufA[len] = '\r';
		++len;
	}
	if(!WriteFile(port,bufA,len,&written,NULL))
	{
		return GetLastError() | 0x80000000;
	}
	return written;
}

long SNkComPort::Purge(DWORD flags)
{
	if (port == INVALID_HANDLE_VALUE) return FALSE;
	return PurgeComm(port, flags);
}

long SNkComPort::SetBuffers(DWORD dwInQueue, DWORD dwOutQueue)
{
	if (port == INVALID_HANDLE_VALUE) return FALSE;
	return SetupComm(port, dwInQueue, dwOutQueue);
}

long baudrates[18] = {300,600,1200,2400,4800,9600,19200,38400,57600,74880,115220,230400,250000,500000,1000000,2000000,5000000,10000000};
wchar_t szParity[5] = {L'n',L'o',L'e',L'm',L's'};
const wchar_t * szParities[5] = {L"none",L"odd",L"even",L"mark",L"space"};
wchar_t szFlowControl[] = {L'n',L'x',L'p',L'b'};
const wchar_t * szFlowControls[4] = {L"none",L"xon/xoff",L"rts/cts",L"both"};
const int iDataBits[5] = { 4,5,6,7,8};
const wchar_t * szDataBits[5] = { L"4",L"5",L"6",L"7",L"8"};
const int eStopBits[3] = { ONESTOPBIT, ONE5STOPBITS, TWOSTOPBITS };
const wchar_t * szStopBits[3] = { L"1",L"1.5",L"2"};  

void SNkComPort::ParseOptions(const wchar_t * options)
{
	int i;
	const wchar_t * p = wcspbrk(options,L":\t\n ");
	if(!p || (*p != L':')) return;
	++p;
	long    br = -1; // baudrate
	wchar_t pa = -1; // parity
	long    db = -1; // databits
	double  sb = -1; // stopbits
	wchar_t fc = -1; // flowcontrol
	long    to = -1; // timeout
	swscanf_s(p,L"%ld,%1c,%ld,%lf,%1c,%ld",&br,&pa,1,&db,&sb,&fc,1,&to);
	if(br != -1) baudrate = br;
	if(pa != -1) { pa = towlower(pa); for(i = 0; i < countof(szParity); ++i) if(pa == szParity[i]) break; if(i < countof(szParity)) parity = (eParity)i; }
    if(db != -1) databits = db;
	if(sb != -1) stopbits = (sb == 1.) ? stopbitOne : (sb == 1.5) ? stopbitOneHalf : stopbitTwo;
	if(fc != -1) { fc = towlower(fc); for(i = 0; i < countof(szFlowControl); ++i) if(fc == szFlowControl[i]) break; if(i < countof(szFlowControl)) handshake = (eHandshake)i; }
	if(to != -1) timeout = to;
}

void SNkComPort::ParseDefaultOptions()
{
	// first fill in the defaults from the registry
	HKEY key = NULL;
	RegOpenKey(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Ports",&key);
	if(key)
	{
		wchar_t value_name[256];
		wchar_t com_opt[256] = L":";
		swprintf_s(value_name,256,L"%s:",name+4);
		DWORD dwType = 0;
		DWORD dwSize = sizeof(com_opt) - 2;
		RegQueryValueEx(key,value_name,NULL,&dwType,(LPBYTE)(com_opt+1),&dwSize);
		RegCloseKey(key);
		ParseOptions(com_opt);
	}
};

long SNkComPort::Setup(const wchar_t * options)
{
	ParseDefaultOptions();
	ParseOptions(options);

	DCB dcb;
	dcb.DCBlength = sizeof(DCB) ;
	GetCommState(port,&dcb);

	BOOL useDtrDsr = FALSE;
	BOOL useRtsCts = (handshake == comRTSXOnXOff) || (handshake == comRTS) ;
	BOOL useXonXoff = (handshake == comRTSXOnXOff) || (handshake == comXOnXOff) ;

	dcb.BaudRate = baudrate;
	dcb.fBinary  = TRUE;
	dcb.fParity  = parity ? 1 : 0;
	dcb.Parity  = parity;
	if(useDtrDsr)
	{
		dcb.fOutxDsrFlow = 1;
		dcb.fDtrControl = DTR_CONTROL_HANDSHAKE ;
	}
	else
	{
		dcb.fOutxDsrFlow = 0;
		dcb.fDtrControl = DTR_CONTROL_ENABLE ;
	}
	if(useRtsCts)
	{
		dcb.fOutxCtsFlow = 1;
		dcb.fRtsControl = RTS_CONTROL_HANDSHAKE ;
	}
	else
	{
		dcb.fOutxCtsFlow = 0;
		dcb.fRtsControl = RTS_CONTROL_ENABLE ;
	}
	dcb.fDsrSensitivity = 0;
	dcb.fTXContinueOnXoff = 0;
	if(useXonXoff)
	{
		dcb.fInX = dcb.fOutX = 1;
	}
	else
	{
		dcb.fInX = dcb.fOutX = 0;
	}
	dcb.fErrorChar = 0;
	dcb.fNull = 0;
	dcb.fAbortOnError = 0;
	dcb.XonLim = 1;
	dcb.XoffLim = 1;
	dcb.ByteSize = databits;
	dcb.StopBits = stopbits;
	dcb.XonChar = ASCII_XON ;
	dcb.XoffChar = ASCII_XOFF ;
	dcb.ErrorChar = 0;
	dcb.EofChar = 0;
	dcb.EvtChar = 0;

	if(!SetupComm(port, RXQUEUE,TXQUEUE))
	{
		return FALSE;
	}
	PurgeComm(port,PURGE_TXABORT|PURGE_RXABORT|PURGE_TXCLEAR|PURGE_RXCLEAR) ;
	SetCommMask(port, EV_RXCHAR|EV_BREAK) ;
	SetCommState(port, &dcb ) ;
	GetCommState(port, &dcb ) ;
	SetTimeOut(timeout);

	AddSettings();
	return TRUE;
}

void SNkComPort::AddSettings()
{
	long n = (long) wcslen(name);
	long l = countof(name) - n - 1;
	if(l > 30)
	{
		wchar_t ip = parity;
		if(ip < 0) ip = 0; if(ip >= (countof(szParity)-1)) ip = countof(szParity) - 1;
		wchar_t fc = handshake;
		if(fc < 0) fc = 0; if(fc >= (countof(szFlowControl)-1)) ip = countof(szFlowControl) - 1;
		swprintf_s(name + n,l, L":%ld,%c,%ld,%s,%c,%ld",
			baudrate,
			szParity[ip],
			databits,
			szStopBits[stopbits],
			szFlowControl[fc],
			timeout);
	}
}

void SNkComPort::SetTimeOut(DWORD ms)
{
	COMMTIMEOUTS CommTimeOuts ;
	CommTimeOuts.ReadIntervalTimeout = 0;
	CommTimeOuts.ReadTotalTimeoutMultiplier = 0;
	CommTimeOuts.ReadTotalTimeoutConstant = ms;
	CommTimeOuts.WriteTotalTimeoutMultiplier = 0 ;
	CommTimeOuts.WriteTotalTimeoutConstant = ms;
	SetCommTimeouts(port, &CommTimeOuts ) ;
	timeout = ms;
}

BOOL FindComPort(wchar_t * name)
{
	HKEY hRootKey = HKEY_LOCAL_MACHINE;
	LPCTSTR szSerialCom = L"HARDWARE\\DEVICEMAP\\SERIALCOMM";
	HKEY hkReg = 0;
	DWORD dwValues;
	TCHAR szName[256];
	DWORD dwName = sizeof(szName);
	TCHAR bData[256];
	DWORD dwType;
	DWORD dwData = sizeof(bData);
	long port_count = 0;

    if (( RegOpenKeyEx(hRootKey, szSerialCom, 0,KEY_READ, &hkReg) == ERROR_SUCCESS ) &&
		( RegQueryInfoKey(hkReg,NULL,NULL,NULL,NULL,NULL,NULL,&dwValues,NULL,NULL,NULL,NULL) == ERROR_SUCCESS) &&
		( dwValues >= 1)) 
	{
		for(DWORD index = 0; RegEnumValue(hkReg,index,szName,&dwName,NULL,&dwType,(LPBYTE)bData,&dwData) == ERROR_SUCCESS; index++)
		{
			if(dwType == REG_SZ)
			{
				if(!wcscmp(bData,name))
				{
					port_count = 1;
					break;
				}
			}
			dwData = sizeof(bData);
			dwName = sizeof(szName);
		}
	}
	if(hkReg)
	{
		RegCloseKey(hkReg);
	}
	return port_count;
}

#if _MSC_VER > 1500

NKCOMPORT_API NkComPort_ListPorts(wchar_t * buffer, long buf_size)
{
	HKEY hRootKey = HKEY_LOCAL_MACHINE;
	LPCTSTR szSerialCom = L"HARDWARE\\DEVICEMAP\\SERIALCOMM";
	HKEY hkReg = 0;
	DWORD dwValues;
	TCHAR szName[256];
	DWORD dwName = sizeof(szName);
	TCHAR szPort[256];
	DWORD dwType;
	DWORD dwData = sizeof(szPort);
	long port_count = 0;
	long len;

	buffer[0] = 0;
    if (( RegOpenKeyEx(hRootKey, szSerialCom, 0,KEY_READ, &hkReg) == ERROR_SUCCESS ) &&
		( RegQueryInfoKey(hkReg,NULL,NULL,NULL,NULL,NULL,NULL,&dwValues,NULL,NULL,NULL,NULL) == ERROR_SUCCESS) &&
		( dwValues >= 1)) 
	{
		for(DWORD index = 0; RegEnumValue(hkReg,index,szName,&dwName,NULL,&dwType,(LPBYTE)szPort,&dwData) == ERROR_SUCCESS; index++)
		{
			if(dwType == REG_SZ)
			{
				// add port name
				len = (long) wcslen(szPort);
				if((len + 2) >= buf_size) break;
				wcscpy_s(buffer,buf_size,szPort);
				buffer += len;
				*buffer = L'\n';
				++buffer;
				*buffer = L'\0';
				buf_size -= len + 1;
				++port_count;
			}
			dwData = sizeof(szPort);
			dwName = sizeof(szName);
		}
	}
	if(hkReg)
	{
		RegCloseKey(hkReg);
	}
	return port_count;
}

#endif

long wcsrpl(wchar_t * str, wchar_t c1, wchar_t c2)
{
	long n = 0;
	for(;*str;++str)
	{
		if(*str == c1) *str = c2;
		++n;
	}
	return n;
}

#if _MSC_VER <= 1500 

long SearchComPorts(HKEY hkReg, wchar_t * vidpid, wchar_t * &buffer, long & buf_size)
{
	buffer[0] = 0;
	DWORD dwSubKeys;
	TCHAR szName[256];
	DWORD dwName = sizeof(szName);
	TCHAR szPort[256];
	DWORD dwPort = sizeof(szPort);
	TCHAR bData[256];
	DWORD dwType;
	DWORD dwData = sizeof(bData);
	long port_count = 0;
	long len;

    if (//( RegOpenKeyEx(hRootKey, szArduinoUno, 0,KEY_READ, &hkReg) == ERROR_SUCCESS ) &&
		( RegQueryInfoKey(hkReg,NULL,NULL,NULL,&dwSubKeys,NULL,NULL,NULL,NULL,NULL,NULL,NULL) == ERROR_SUCCESS) &&
		( dwSubKeys >= 1)) 
	{
		// arduino Uno's
		for(DWORD index = 0; RegEnumKey(hkReg,index,szName,dwName) == ERROR_SUCCESS; index++)
		{
			HKEY hKey = NULL;
			if(RegOpenKeyEx(hkReg,szName,0,KEY_READ,&hKey) == ERROR_SUCCESS)
			{
				dwName = sizeof(szName);
				if((RegQueryValueEx(hKey,L"FriendlyName",NULL,&dwType,(LPBYTE)szName,&dwName) == ERROR_SUCCESS) &&
						(dwType == REG_SZ))
				{
					HKEY hKey2 = NULL;
					if(RegOpenKeyEx(hKey,L"Device Parameters",0,KEY_READ,&hKey2) == ERROR_SUCCESS)
					{
						dwPort = sizeof(szPort);
						if((RegQueryValueEx(hKey2,L"PortName",NULL,&dwType,(LPBYTE)szPort,&dwPort) == ERROR_SUCCESS) &&
							(dwType == REG_SZ))
						{
							if(FindComPort(szPort))
							{
								// add port name
								len = (long) wcslen(szPort);
								if((len + 2) >= buf_size) break;
								wcscpy_s(buffer,buf_size,szPort);
								buffer += len;
								*buffer = L'\t';
								++buffer;
								*buffer = L'\0';
								buf_size -= len + 1;
								++port_count;
								// add friendly name
								len = (long) wcslen(szName);
								if((len + 2) >= buf_size) break;
								wcscpy_s(buffer,buf_size,szName);
								buffer += len;
								*buffer = L'\t';
								++buffer;
								*buffer = L'\0';
								buf_size -= len + 1;
								// add vidpid
								len = (long)wcslen(vidpid);
								wcsrpl(vidpid,L'_',L':');
								wcsrpl(vidpid,L'&',L' ');
								wcsrpl(vidpid,L'+',L' ');
								if ((len + 2) >= buf_size) break;
								wcscpy_s(buffer, buf_size, vidpid);
								buffer += len;
								*buffer = L'\n';
								++buffer;
								*buffer = L'\0';
								buf_size -= len + 1;
							}
						}
						RegCloseKey(hKey2);
					}
				}
				RegCloseKey(hKey);
			}
			dwName = sizeof(szName);
			dwData = sizeof(bData);
		}
	}
	if(hkReg)
	{
		//RegCloseKey(hkReg);
	}
	return port_count;
}

#define MAX_KEY_LENGTH 255

NKCOMPORT_API NkComPort_ListPorts(wchar_t * buffer, long buf_size)
{
	if(!is_string_valid_write(buffer, buf_size)) { return E_INVALIDARG; }

	HKEY hKey = NULL;
	HKEY hSubKey = NULL;
	DWORD dwSubKeyCount = 0;
	DWORD cbName;
	TCHAR achKey[MAX_KEY_LENGTH];   // buffer for subkey name
	int com_port_count = 0;

	//LPCTSTR szArduinoUno = L"SYSTEM\\CurrentControlSet\\Enum\\USB\\VID_2A03&PID_0043";
	//LPCTSTR szArduinoUno = L"SYSTEM\\CurrentControlSet\\Enum\\FTDIBUS\\VID_0403+PID_6001+A703FLEGA";

	buffer[0] = 0;
    if(RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Enum\\FTDIBUS", 0,KEY_READ, &hKey) == ERROR_SUCCESS )
	{
		dwSubKeyCount = 0;
		RegQueryInfoKey(hKey,NULL,NULL,NULL,&dwSubKeyCount,NULL,NULL,NULL,NULL,NULL,NULL,NULL);
		for(DWORD i = 0; i < dwSubKeyCount; ++i)
		{
			cbName = MAX_KEY_LENGTH;
			achKey[0] = 0;
            if(RegEnumKeyEx(hKey,i,achKey,&cbName,NULL,NULL,NULL,NULL) == ERROR_SUCCESS) 
            {
				if(RegOpenKeyEx(hKey, achKey, 0,KEY_READ, &hSubKey) == ERROR_SUCCESS )
				{
					com_port_count  += SearchComPorts(hSubKey,achKey,buffer,buf_size);
					RegCloseKey(hSubKey);
				}
			}
		}
		RegCloseKey(hKey);
	}

    if(RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Enum\\USB", 0,KEY_READ, &hKey) == ERROR_SUCCESS )
	{
		dwSubKeyCount = 0;
		RegQueryInfoKey(hKey,NULL,NULL,NULL,&dwSubKeyCount,NULL,NULL,NULL,NULL,NULL,NULL,NULL);
		for(DWORD i = 0; i < dwSubKeyCount; ++i)
		{
			cbName = MAX_KEY_LENGTH;
			achKey[0] = 0;
            if(RegEnumKeyEx(hKey,i,achKey,&cbName,NULL,NULL,NULL,NULL) == ERROR_SUCCESS) 
            {
				if(RegOpenKeyEx(hKey, achKey, 0,KEY_READ, &hSubKey) == ERROR_SUCCESS )
				{
					com_port_count  += SearchComPorts(hSubKey,achKey,buffer,buf_size);
					RegCloseKey(hSubKey);
				}
			}
		}
		RegCloseKey(hKey);
	}

	return com_port_count;
}

#else

void* MALLOC(size_t s)
{
	void* p = malloc(s);
	if (!p) return p;
	memset(p, 0, s);
	return p;
}

PUSB_DESCRIPTOR_REQUEST
GetConfigDescriptor(
	HANDLE  hHubDevice,
	ULONG   ConnectionIndex,
	UCHAR   DescriptorIndex
)
{
	BOOL    success = 0;
	ULONG   nBytes = 0;
	ULONG   nBytesReturned = 0;

	UCHAR   configDescReqBuf[sizeof(USB_DESCRIPTOR_REQUEST) +
		sizeof(USB_CONFIGURATION_DESCRIPTOR)];

	PUSB_DESCRIPTOR_REQUEST         configDescReq = NULL;
	PUSB_CONFIGURATION_DESCRIPTOR   configDesc = NULL;


	// Request the Configuration Descriptor the first time using our
	// local buffer, which is just big enough for the Cofiguration
	// Descriptor itself.
	//
	nBytes = sizeof(configDescReqBuf);

	configDescReq = (PUSB_DESCRIPTOR_REQUEST)configDescReqBuf;
	configDesc = (PUSB_CONFIGURATION_DESCRIPTOR)(configDescReq + 1);

	// Zero fill the entire request structure
	//
	memset(configDescReq, 0, nBytes);

	// Indicate the port from which the descriptor will be requested
	//
	configDescReq->ConnectionIndex = ConnectionIndex;

	//
	// USBHUB uses URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE to process this
	// IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION request.
	//
	// USBD will automatically initialize these fields:
	//     bmRequest = 0x80
	//     bRequest  = 0x06
	//
	// We must inititialize these fields:
	//     wValue    = Descriptor Type (high) and Descriptor Index (low byte)
	//     wIndex    = Zero (or Language ID for String Descriptors)
	//     wLength   = Length of descriptor buffer
	//
	configDescReq->SetupPacket.wValue = (USB_CONFIGURATION_DESCRIPTOR_TYPE << 8)
		| DescriptorIndex;

	configDescReq->SetupPacket.wLength = (USHORT)(nBytes - sizeof(USB_DESCRIPTOR_REQUEST));

	// Now issue the get descriptor request.
	//
	success = DeviceIoControl(hHubDevice,
		IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,
		configDescReq,
		nBytes,
		configDescReq,
		nBytes,
		&nBytesReturned,
		NULL);

	if (!success)
	{
		return NULL;
	}

	if (nBytes != nBytesReturned)
	{
		return NULL;
	}

	if (configDesc->wTotalLength < sizeof(USB_CONFIGURATION_DESCRIPTOR))
	{
		return NULL;
	}

	// Now request the entire Configuration Descriptor using a dynamically
	// allocated buffer which is sized big enough to hold the entire descriptor
	//
	nBytes = sizeof(USB_DESCRIPTOR_REQUEST) + configDesc->wTotalLength;

	configDescReq = (PUSB_DESCRIPTOR_REQUEST)MALLOC(nBytes);

	if (configDescReq == NULL)
	{
		return NULL;
	}

	configDesc = (PUSB_CONFIGURATION_DESCRIPTOR)(configDescReq + 1);

	// Indicate the port from which the descriptor will be requested
	//
	configDescReq->ConnectionIndex = ConnectionIndex;

	//
	// USBHUB uses URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE to process this
	// IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION request.
	//
	// USBD will automatically initialize these fields:
	//     bmRequest = 0x80
	//     bRequest  = 0x06
	//
	// We must inititialize these fields:
	//     wValue    = Descriptor Type (high) and Descriptor Index (low byte)
	//     wIndex    = Zero (or Language ID for String Descriptors)
	//     wLength   = Length of descriptor buffer
	//
	configDescReq->SetupPacket.wValue = (USB_CONFIGURATION_DESCRIPTOR_TYPE << 8)
		| DescriptorIndex;

	configDescReq->SetupPacket.wLength = (USHORT)(nBytes - sizeof(USB_DESCRIPTOR_REQUEST));

	// Now issue the get descriptor request.
	//

	success = DeviceIoControl(hHubDevice,
		IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,
		configDescReq,
		nBytes,
		configDescReq,
		nBytes,
		&nBytesReturned,
		NULL);

	if (!success)
	{
		free(configDescReq);
		return NULL;
	}

	if (nBytes != nBytesReturned)
	{
		free(configDescReq);
		return NULL;
	}

	if (configDesc->wTotalLength != (nBytes - sizeof(USB_DESCRIPTOR_REQUEST)))
	{
		free(configDescReq);
		return NULL;
	}

	return configDescReq;
}

BOOL
AreThereStringDescriptors(
	PUSB_DEVICE_DESCRIPTOR          DeviceDesc,
	PUSB_CONFIGURATION_DESCRIPTOR   ConfigDesc
)
{
	PUCHAR                  descEnd = NULL;
	PUSB_COMMON_DESCRIPTOR  commonDesc = NULL;

	//
	// Check Device Descriptor strings
	//

	if (DeviceDesc->iManufacturer ||
		DeviceDesc->iProduct ||
		DeviceDesc->iSerialNumber
		)
	{
		return TRUE;
	}


	//
	// Check the Configuration and Interface Descriptor strings
	//

	descEnd = (PUCHAR)ConfigDesc + ConfigDesc->wTotalLength;

	commonDesc = (PUSB_COMMON_DESCRIPTOR)ConfigDesc;

	while ((PUCHAR)commonDesc + sizeof(USB_COMMON_DESCRIPTOR) < descEnd &&
		(PUCHAR)commonDesc + commonDesc->bLength <= descEnd)
	{
		switch (commonDesc->bDescriptorType)
		{
		case USB_CONFIGURATION_DESCRIPTOR_TYPE:
		case USB_OTHER_SPEED_CONFIGURATION_DESCRIPTOR_TYPE:
			if (commonDesc->bLength != sizeof(USB_CONFIGURATION_DESCRIPTOR))
			{
				break;
			}
			if (((PUSB_CONFIGURATION_DESCRIPTOR)commonDesc)->iConfiguration)
			{
				return TRUE;
			}
			commonDesc = (PUSB_COMMON_DESCRIPTOR)((PUCHAR)commonDesc + commonDesc->bLength);
			continue;

		case USB_INTERFACE_DESCRIPTOR_TYPE:
			if (commonDesc->bLength != sizeof(USB_INTERFACE_DESCRIPTOR) &&
				commonDesc->bLength != sizeof(USB_INTERFACE_DESCRIPTOR2))
			{
				break;
			}
			if (((PUSB_INTERFACE_DESCRIPTOR)commonDesc)->iInterface)
			{
				return TRUE;
			}
			commonDesc = (PUSB_COMMON_DESCRIPTOR)((PUCHAR)commonDesc + commonDesc->bLength);
			continue;

		default:
			commonDesc = (PUSB_COMMON_DESCRIPTOR)((PUCHAR)commonDesc + commonDesc->bLength);
			continue;
		}
		break;
	}

	return FALSE;
}

typedef struct _STRING_DESCRIPTOR_NODE
{
	struct _STRING_DESCRIPTOR_NODE* Next;
	UCHAR                           DescriptorIndex;
	USHORT                          LanguageID;
	USB_STRING_DESCRIPTOR           StringDescriptor[1];
} STRING_DESCRIPTOR_NODE, * PSTRING_DESCRIPTOR_NODE;

PSTRING_DESCRIPTOR_NODE
GetStringDescriptor(
	HANDLE  hHubDevice,
	ULONG   ConnectionIndex,
	UCHAR   DescriptorIndex,
	USHORT  LanguageID
)
{
	BOOL    success = 0;
	ULONG   nBytes = 0;
	ULONG   nBytesReturned = 0;

	UCHAR   stringDescReqBuf[sizeof(USB_DESCRIPTOR_REQUEST) +
		MAXIMUM_USB_STRING_LENGTH];

	PUSB_DESCRIPTOR_REQUEST stringDescReq = NULL;
	PUSB_STRING_DESCRIPTOR  stringDesc = NULL;
	PSTRING_DESCRIPTOR_NODE stringDescNode = NULL;

	nBytes = sizeof(stringDescReqBuf);

	stringDescReq = (PUSB_DESCRIPTOR_REQUEST)stringDescReqBuf;
	stringDesc = (PUSB_STRING_DESCRIPTOR)(stringDescReq + 1);

	// Zero fill the entire request structure
	//
	memset(stringDescReq, 0, nBytes);

	// Indicate the port from which the descriptor will be requested
	//
	stringDescReq->ConnectionIndex = ConnectionIndex;

	//
	// USBHUB uses URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE to process this
	// IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION request.
	//
	// USBD will automatically initialize these fields:
	//     bmRequest = 0x80
	//     bRequest  = 0x06
	//
	// We must inititialize these fields:
	//     wValue    = Descriptor Type (high) and Descriptor Index (low byte)
	//     wIndex    = Zero (or Language ID for String Descriptors)
	//     wLength   = Length of descriptor buffer
	//
	stringDescReq->SetupPacket.wValue = (USB_STRING_DESCRIPTOR_TYPE << 8)
		| DescriptorIndex;

	stringDescReq->SetupPacket.wIndex = LanguageID;

	stringDescReq->SetupPacket.wLength = (USHORT)(nBytes - sizeof(USB_DESCRIPTOR_REQUEST));

	// Now issue the get descriptor request.
	//
	success = DeviceIoControl(hHubDevice,
		IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION,
		stringDescReq,
		nBytes,
		stringDescReq,
		nBytes,
		&nBytesReturned,
		NULL);

	//
	// Do some sanity checks on the return from the get descriptor request.
	//

	if (!success)
	{
		return NULL;
	}

	if (nBytesReturned < 2)
	{
		return NULL;
	}

	if (stringDesc->bDescriptorType != USB_STRING_DESCRIPTOR_TYPE)
	{
		return NULL;
	}

	if (stringDesc->bLength != nBytesReturned - sizeof(USB_DESCRIPTOR_REQUEST))
	{
		return NULL;
	}

	if (stringDesc->bLength % 2 != 0)
	{
		return NULL;
	}

	//
	// Looks good, allocate some (zero filled) space for the string descriptor
	// node and copy the string descriptor to it.
	//

	stringDescNode = (PSTRING_DESCRIPTOR_NODE)MALLOC(sizeof(STRING_DESCRIPTOR_NODE) +
		stringDesc->bLength);

	if (stringDescNode == NULL)
	{
		return NULL;
	}

	stringDescNode->DescriptorIndex = DescriptorIndex;
	stringDescNode->LanguageID = LanguageID;

	memcpy(stringDescNode->StringDescriptor,
		stringDesc,
		stringDesc->bLength);

	return stringDescNode;
}

HRESULT
GetStringDescriptors(
	_In_ HANDLE                         hHubDevice,
	_In_ ULONG                          ConnectionIndex,
	_In_ UCHAR                          DescriptorIndex,
	_In_ ULONG                          NumLanguageIDs,
	_In_reads_(NumLanguageIDs) USHORT* LanguageIDs,
	_In_ PSTRING_DESCRIPTOR_NODE        StringDescNodeHead
)
{
	PSTRING_DESCRIPTOR_NODE tail = NULL;
	PSTRING_DESCRIPTOR_NODE trailing = NULL;
	ULONG i = 0;

	//
	// Go to the end of the linked list, searching for the requested index to
	// see if we've already retrieved it
	//
	for (tail = StringDescNodeHead; tail != NULL; tail = tail->Next)
	{
		if (tail->DescriptorIndex == DescriptorIndex)
		{
			return S_OK;
		}

		trailing = tail;
	}

	tail = trailing;

	//
	// Get the next String Descriptor. If this is NULL, then we're done (return)
	// Otherwise, loop through all Language IDs
	//
	for (i = 0; (tail != NULL) && (i < NumLanguageIDs); i++)
	{
		tail->Next = GetStringDescriptor(hHubDevice,
			ConnectionIndex,
			DescriptorIndex,
			LanguageIDs[i]);

		tail = tail->Next;
	}

	if (tail == NULL)
	{
		return E_FAIL;
	}
	else {
		return S_OK;
	}
}

#define NUM_STRING_DESC_TO_GET 32

PSTRING_DESCRIPTOR_NODE
GetAllStringDescriptors(
	HANDLE                          hHubDevice,
	ULONG                           ConnectionIndex,
	PUSB_DEVICE_DESCRIPTOR          DeviceDesc,
	PUSB_CONFIGURATION_DESCRIPTOR   ConfigDesc
)
{
	PSTRING_DESCRIPTOR_NODE supportedLanguagesString = NULL;
	ULONG                   numLanguageIDs = 0;
	USHORT* languageIDs = NULL;

	PUCHAR                  descEnd = NULL;
	PUSB_COMMON_DESCRIPTOR  commonDesc = NULL;
	UCHAR                   uIndex = 1;
	UCHAR                   bInterfaceClass = 0;
	BOOL                    getMoreStrings = FALSE;
	HRESULT                 hr = S_OK;

	//
	// Get the array of supported Language IDs, which is returned
	// in String Descriptor 0
	//
	supportedLanguagesString = GetStringDescriptor(hHubDevice,
		ConnectionIndex,
		0,
		0);

	if (supportedLanguagesString == NULL)
	{
		return NULL;
	}

	numLanguageIDs = (supportedLanguagesString->StringDescriptor->bLength - 2) / 2;

	languageIDs = (USHORT*) &supportedLanguagesString->StringDescriptor->bString[0];

	//
	// Get the Device Descriptor strings
	//

	if (DeviceDesc->iManufacturer)
	{
		GetStringDescriptors(hHubDevice,
			ConnectionIndex,
			DeviceDesc->iManufacturer,
			numLanguageIDs,
			languageIDs,
			supportedLanguagesString);
	}

	if (DeviceDesc->iProduct)
	{
		GetStringDescriptors(hHubDevice,
			ConnectionIndex,
			DeviceDesc->iProduct,
			numLanguageIDs,
			languageIDs,
			supportedLanguagesString);
	}

	if (DeviceDesc->iSerialNumber)
	{
		GetStringDescriptors(hHubDevice,
			ConnectionIndex,
			DeviceDesc->iSerialNumber,
			numLanguageIDs,
			languageIDs,
			supportedLanguagesString);
	}

	//
	// Get the Configuration and Interface Descriptor strings
	//

	descEnd = (PUCHAR)ConfigDesc + ConfigDesc->wTotalLength;

	commonDesc = (PUSB_COMMON_DESCRIPTOR)ConfigDesc;

	while ((PUCHAR)commonDesc + sizeof(USB_COMMON_DESCRIPTOR) < descEnd &&
		(PUCHAR)commonDesc + commonDesc->bLength <= descEnd)
	{
		switch (commonDesc->bDescriptorType)
		{
		case USB_CONFIGURATION_DESCRIPTOR_TYPE:
			if (commonDesc->bLength != sizeof(USB_CONFIGURATION_DESCRIPTOR))
			{
				break;
			}
			if (((PUSB_CONFIGURATION_DESCRIPTOR)commonDesc)->iConfiguration)
			{
				GetStringDescriptors(hHubDevice,
					ConnectionIndex,
					((PUSB_CONFIGURATION_DESCRIPTOR)commonDesc)->iConfiguration,
					numLanguageIDs,
					languageIDs,
					supportedLanguagesString);
			}
			commonDesc = (PUSB_COMMON_DESCRIPTOR)((PUCHAR)commonDesc + commonDesc->bLength);
			continue;

		case USB_IAD_DESCRIPTOR_TYPE:
			if (commonDesc->bLength < sizeof(USB_IAD_DESCRIPTOR))
			{
				break;
			}
			if (((PUSB_IAD_DESCRIPTOR)commonDesc)->iFunction)
			{
				GetStringDescriptors(hHubDevice,
					ConnectionIndex,
					((PUSB_IAD_DESCRIPTOR)commonDesc)->iFunction,
					numLanguageIDs,
					languageIDs,
					supportedLanguagesString);
			}
			commonDesc = (PUSB_COMMON_DESCRIPTOR)((PUCHAR)commonDesc + commonDesc->bLength);
			continue;

		case USB_INTERFACE_DESCRIPTOR_TYPE:
			if (commonDesc->bLength != sizeof(USB_INTERFACE_DESCRIPTOR) &&
				commonDesc->bLength != sizeof(USB_INTERFACE_DESCRIPTOR2))
			{
				break;
			}
			if (((PUSB_INTERFACE_DESCRIPTOR)commonDesc)->iInterface)
			{
										
				GetStringDescriptors(hHubDevice,
					ConnectionIndex,
					((PUSB_INTERFACE_DESCRIPTOR)commonDesc)->iInterface,
					numLanguageIDs,
					languageIDs,
					supportedLanguagesString);
			}

			//
			// We need to display more string descriptors for the following
			// interface classes
			//
			bInterfaceClass = ((PUSB_INTERFACE_DESCRIPTOR)commonDesc)->bInterfaceClass;
			if (bInterfaceClass == USB_DEVICE_CLASS_VIDEO)
			{
				getMoreStrings = TRUE;
			}
			commonDesc = (PUSB_COMMON_DESCRIPTOR)((PUCHAR)commonDesc + commonDesc->bLength);
			continue;

		default:
			commonDesc = (PUSB_COMMON_DESCRIPTOR)((PUCHAR)commonDesc + commonDesc->bLength);
			continue;
		}
		break;
	}

	if (getMoreStrings)
	{
		//
		// We might need to display strings later that are referenced only in
		// class-specific descriptors. Get String Descriptors 1 through 32 (an
		// arbitrary upper limit for Strings needed due to "bad devices"
		// returning an infinite repeat of Strings 0 through 4) until one is not
		// found.
		//
		// There are also "bad devices" that have issues even querying 1-32, but
		// historically USBView made this query, so the query should be safe for
		// video devices.
		//
		for (uIndex = 1; SUCCEEDED(hr) && (uIndex < NUM_STRING_DESC_TO_GET); uIndex++)
		{
			hr = GetStringDescriptors(hHubDevice,
				ConnectionIndex,
				uIndex,
				numLanguageIDs,
				languageIDs,
				supportedLanguagesString);
		}
	}

	return supportedLanguagesString;
}

ULONG
FindStringDescriptor(
	wchar_t*                    buffer,
	DWORD                       bufferSize,
	PSTRING_DESCRIPTOR_NODE     USStringDescs,
	UCHAR                       Index,
	DWORD                       LanguageId = 0x0409
)
{
	ULONG nBytes = 0;
	for (; USStringDescs; USStringDescs = USStringDescs->Next)
	{
		if (USStringDescs->DescriptorIndex == Index && USStringDescs->LanguageID == LanguageId)
		{
			wcsncpy_s(buffer, bufferSize, USStringDescs->StringDescriptor->bString, USStringDescs->StringDescriptor->bLength);
			return USStringDescs->StringDescriptor->bLength;
		}
	}
	return 0;
}

static GUID GUID_DEVCLASS_PORTS = { 0x4d36e978, 0xe325, 0x11ce, 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18 };

static const GUID guidsArray[] =
{
    // GUID_DEVCLASS_PORTS (legacy)
	{ 0x4D36E978, 0xE325, 0x11CE, { 0xBF, 0xC1, 0x08, 0x00, 0x2B, 0xE1, 0x03, 0x18 } },
    // Virtual Ports Class GUID (i.e. com0com and etc)
	{ 0xDF799E12, 0x3C56, 0x421B, { 0xB2, 0x98, 0xB6, 0xD3, 0x64, 0x2B, 0xC8, 0x78 } },
    // Windows Modems Class GUID
	{ 0x4D36E96D, 0xE325, 0x11CE, { 0xBF, 0xC1, 0x08, 0x00, 0x2B, 0xE1, 0x03, 0x18 } },
    // Eltima Virtual Serial Port Driver v4 GUID
	{ 0xCC0EF009, 0xB820, 0x42F4, { 0x95, 0xA9, 0x9B, 0xFA, 0x6A, 0x5A, 0xB7, 0xAB } },
    // Advanced Virtual COM Port GUID
	{ 0x9341CD95, 0x4371, 0x4A37, { 0xA5, 0xAF, 0xFD, 0xB0, 0xA9, 0xD1, 0x96, 0x31 } },
    // GUID_DEVINTERFACE_COMPORT
	{ 0x86E0D1E0, 0x8089, 0x11D0, { 0x9C, 0xE4, 0x08, 0x00, 0x3E, 0x30, 0x1F, 0x73} },
    // GUID_DEVINTERFACE_MODEM
	{ 0x2C7089AA, 0x2E0E, 0x11D1, { 0xB1, 0x14, 0x00, 0xC0, 0x4F, 0xC2, 0xAA, 0xE4} },
};

NKCOMPORT_API NkComPort_ListPortsEx(wchar_t* buffer, long buf_size)
{
	HDEVINFO        hDevInfo;
	SP_DEVINFO_DATA DeviceInfoData;
	DWORD           i;
	DWORD           n = 0;

	// to make sure we don't miss any COM port legacy listed:
	wchar_t * bufRegEnumCom = (wchar_t *) alloca(buf_size * sizeof(wchar_t));
	NkComPort_ListPorts(bufRegEnumCom, buf_size);

	if (buf_size < 64) return -1;
	buf_size--; // allow for the terminating 0

	for (int g = 0; g < countof(guidsArray); ++g)
	{
		hDevInfo = SetupDiGetClassDevs(guidsArray + g, 0L, 0L, DIGCF_PRESENT);
		if (hDevInfo == INVALID_HANDLE_VALUE) continue;

		// Enumerate through all devices in Set.
		DeviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
		for (i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &DeviceInfoData) != 0; i++)
		{
			wchar_t         friendlyName[3 * MAX_PATH + 2] = { 0 };
			wchar_t         portName[MAX_PATH] = { 0 };
			wchar_t         vidPid[MAX_PATH] = { 0 };

			// read the port name
			HKEY hKey = SetupDiOpenDevRegKey(
				hDevInfo,
				&DeviceInfoData,
				DICS_FLAG_GLOBAL,
				0,
				DIREG_DEV,
				KEY_READ);
			if (hKey != INVALID_HANDLE_VALUE)
			{
				DWORD dwBufferSize = sizeof(portName);
				RegQueryValueEx(hKey, L"PortName", 0, NULL, (LPBYTE)portName, &dwBufferSize);
				RegCloseKey(hKey);
			}
			if (!portName[0]) continue;
			//MessageBox(NULL, portName, L"PortName", MB_OK);

			// friendly name
			DWORD dwRequiredSize;
			SetupDiGetDeviceRegistryProperty(hDevInfo, &DeviceInfoData, SPDRP_FRIENDLYNAME, 0L, (PBYTE)friendlyName, MAX_PATH, &dwRequiredSize);
			if (portName[0] && friendlyName[0])
			{
				wchar_t tmp[256];
				swprintf_s(tmp, countof(tmp), L" (%s", portName);
				wchar_t* p = wcsstr(friendlyName, tmp);
				if (p) *p = 0;
			}
			//MessageBox(NULL, friendlyName, L"friendlyName", MB_OK);
/*
			wchar_t hwIds[MAX_PATH];
			SetupDiGetDeviceRegistryProperty(hDevInfo, &DeviceInfoData, SPDRP_HARDWAREID, 0L, (PBYTE)hwIds, MAX_PATH, &dwRequiredSize);
			wchar_t* p = wcsstr(hwIds, L"VID");
			if (p)
			{
				wcscpy_s(vidPid, countof(vidPid), p);
				wcsrpl(vidPid, L'_', L':');
				wcsrpl(vidPid, L'&', L' ');
				wcsrpl(vidPid, L'+', L' ');
			}
*/
			wchar_t devInstId[MAX_PATH];
			DEVPROPTYPE devInstIdType;
			DWORD dwInstIdLen = MAX_PATH;
			if (SetupDiGetDeviceProperty(hDevInfo, &DeviceInfoData, &DEVPKEY_Device_InstanceId, &devInstIdType, (BYTE*)devInstId, MAX_PATH, &dwInstIdLen, 0))
			{
				DWORD VID = -1, PID = -1, REV = -1, MI = -1;
				wchar_t* p;
				wchar_t* sn = NULL;
				p = wcsstr(devInstId, L"VID_");
				if (p) { swscanf_s(p + 4, L"%lX", &VID); sn = wcschr(p, '\\'); }
				p = wcsstr(devInstId, L"PID_");
				if (p) swscanf_s(p + 4, L"%lX", &PID);
				p = wcsstr(devInstId, L"REV_");
				if (p) swscanf_s(p + 4, L"%lX", &REV);
				p = wcsstr(devInstId, L"MI_");
				if (p) swscanf_s(p + 4, L"%lX", &MI);

				swprintf_s(vidPid, MAX_PATH, L"VID:%04lX PID:%04lX", VID, PID);
				p = vidPid + wcslen(vidPid);
				if (REV != -1)
				{
					swprintf_s(p, MAX_PATH - (p - vidPid), L" REV:%04lX", REV);
					p = vidPid + wcslen(vidPid);
				}
				p = vidPid + wcslen(vidPid);
				wchar_t parent[MAX_PATH];
				if (MI != -1)
				{
					DEVPROPTYPE type;
					if (SetupDiGetDeviceProperty(hDevInfo, &DeviceInfoData, &DEVPKEY_Device_Parent, &type, (BYTE*)parent, MAX_PATH, &dwRequiredSize, 0))
					{
						wchar_t * q = wcschr(parent, L'\\');
						if(q) q = wcschr(q + 1, L'\\');
						if (q)
						{
							sn = q;
						}
					}
				}
				if (sn)
				{
					swprintf_s(p, MAX_PATH - (p - vidPid), L" SN:%s", sn + 1);
				}
			}

#if 0 // defined(DEBUG
			long port = -1;
			SetupDiGetDeviceRegistryProperty(hDevInfo, &DeviceInfoData, SPDRP_ADDRESS, 0L, (PBYTE)&port, sizeof(port), &dwRequiredSize);
			//wchar_t tmp[64];
			//swprintf_s(tmp, countof(tmp), L"%ld", port);
			//MessageBox(NULL, tmp, L"address", MB_OK);

			if (port != -1)
			{
				dwRequiredSize = 0;
				SetupDiGetDeviceRegistryProperty(hDevInfo, &DeviceInfoData, SPDRP_DEVICEDESC, NULL, NULL, 0, &dwRequiredSize);
				wchar_t* szDevDescr = NULL;
				if (dwRequiredSize)
				{
					szDevDescr = (wchar_t*)MALLOC(dwRequiredSize);
				}
				if (szDevDescr)
				{
					szDevDescr[0] = 0;
					SetupDiGetDeviceRegistryProperty(hDevInfo, &DeviceInfoData, SPDRP_DEVICEDESC, NULL, (LPBYTE)szDevDescr, dwRequiredSize, &dwRequiredSize);
				}

				wchar_t parent[MAX_PATH];
				DEVPROPTYPE type;
				if (SetupDiGetDeviceProperty(hDevInfo, &DeviceInfoData, &DEVPKEY_Device_Parent, &type, (BYTE*)parent, MAX_PATH, &dwRequiredSize, 0))
				{
					GUID* interfaceGUID = (GUID*)&GUID_CLASS_USBHUB;
					wchar_t* sysDeviceID = parent;
					wchar_t* ifaceList = NULL;
					ULONG ifacesLen = 0;;
					ULONG* outIfacesLen = &ifacesLen;
					//int GetInterfaces(const WCHAR * sysDeviceID, const LPGUID interfaceGUID, wchar_t** outIfaces, ULONG * outIfacesLen)
					{
						CONFIGRET cres;

						// Get list size
						ULONG ifaceListSize = 0;
						cres = CM_Get_Device_Interface_List_Size(&ifaceListSize, interfaceGUID, sysDeviceID, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
						if (cres == CR_SUCCESS)
						{
							// Allocate memory for the list
							ifaceList = new wchar_t[ifaceListSize * 2];    // Double the required size, in order minimize the chances of getting CR_BUFFER_SMALL errors
							// Populate the list
							cres = CM_Get_Device_Interface_List(interfaceGUID, sysDeviceID, ifaceList, ifaceListSize, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
							if (cres == CR_SUCCESS)
							{
								HANDLE hHubDevice = CreateFile(ifaceList, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
								if (hHubDevice != INVALID_HANDLE_VALUE)
								{
									USB_NODE_INFORMATION hubInfo;
									DWORD nBytes;
									if (DeviceIoControl(hHubDevice,
										IOCTL_USB_GET_NODE_INFORMATION,
										&hubInfo,
										sizeof(USB_NODE_INFORMATION),
										&hubInfo,
										sizeof(USB_NODE_INFORMATION),
										&nBytes,
										NULL))
									{
										//for (long i = 1; i < hubInfo.u.HubInformation.HubDescriptor.bNumberOfPorts; ++i)
										long i = port;
										{
											USB_NODE_CONNECTION_INFORMATION_EX connectionInfoEx;
											connectionInfoEx.ConnectionIndex = i;
											if (DeviceIoControl(hHubDevice,
												IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX,
												&connectionInfoEx,
												sizeof(connectionInfoEx),
												&connectionInfoEx,
												sizeof(connectionInfoEx),
												&nBytes,
												NULL))
											{
												if (connectionInfoEx.DeviceDescriptor.idProduct && connectionInfoEx.DeviceDescriptor.idVendor)
												{
													swprintf_s(vidPid, countof(vidPid), L"VID:%04X PID:%04X", connectionInfoEx.DeviceDescriptor.idVendor, connectionInfoEx.DeviceDescriptor.idProduct);
												}
												PUSB_DESCRIPTOR_REQUEST configDesc = GetConfigDescriptor(hHubDevice, i, 0);
												if (configDesc)
												{
													if (AreThereStringDescriptors(&connectionInfoEx.DeviceDescriptor, (PUSB_CONFIGURATION_DESCRIPTOR)(configDesc + 1)))
													{
														PSTRING_DESCRIPTOR_NODE StringDescs = GetAllStringDescriptors(hHubDevice, i, &connectionInfoEx.DeviceDescriptor, (PUSB_CONFIGURATION_DESCRIPTOR)(configDesc + 1));
														if (StringDescs != NULL)
														{
															wchar_t product[MAX_PATH] = { 0 };
															wchar_t manufacturer[MAX_PATH] = { 0 };
															wchar_t serialnumber[MAX_PATH] = { 0 };
															FindStringDescriptor(product, MAX_PATH, StringDescs, connectionInfoEx.DeviceDescriptor.iProduct);
															FindStringDescriptor(manufacturer, MAX_PATH, StringDescs, connectionInfoEx.DeviceDescriptor.iManufacturer);
															FindStringDescriptor(serialnumber, MAX_PATH, StringDescs, connectionInfoEx.DeviceDescriptor.iSerialNumber);
															if (product[0] || manufacturer[0] || serialnumber[0])
															{
																if (manufacturer[0]) wcscpy_s(friendlyName, countof(friendlyName), manufacturer);
																if (friendlyName[0] && product[0]) wcscat_s(friendlyName, countof(friendlyName), L" ");
																wcscat_s(friendlyName, countof(friendlyName), product);
																if (friendlyName[0] && serialnumber[0]) wcscat_s(friendlyName, countof(friendlyName), L" ");
																wcscat_s(friendlyName, countof(friendlyName), serialnumber);
															}
															PSTRING_DESCRIPTOR_NODE Next;
															do {

																Next = StringDescs->Next;
																free(StringDescs);
																StringDescs = Next;
															} while (StringDescs != NULL);
														}
													}
													free(configDesc);
												}
											}
										}
									}
									CloseHandle(hHubDevice);
								}
							}
							delete[] ifaceList;
						}
					}
				}
				if (szDevDescr)
				{
					free(szDevDescr);
				}
			}
#endif
			// print the strings to the buffer
			if (portName[0])
			{
				long l = long(wcslen(portName) + 1 + wcslen(friendlyName) + 1 + wcslen(vidPid) + 1);
				if (buf_size < l)
				{
					break;
				}
				swprintf_s(buffer, buf_size, L"%s\t%s\t%s\n", portName, friendlyName, vidPid);
				buf_size -= l;
				buffer += l;
				++n;

				// remove the port
				for (wchar_t* p = bufRegEnumCom; p && *p; )
				{
					size_t nl = wcscspn(p, L"\n");
					if (!wcsncmp(p, portName, nl))
					{
						wchar_t* q = p + nl;
						if (*q) ++q;
						while (*q)
						{
							*p = *q;
							++p;
							++q;
						}
						*p = 0;
						break;
					}
					p += nl;
					if (*p) ++p;
				}
			}
		}
		SetupDiDestroyDeviceInfoList(hDevInfo);
	}

	// copy COM ports with other GUID device interface classes
	size_t len = wcslen(bufRegEnumCom);
	if (buf_size > len)
	{
		wcscpy_s(buffer, buf_size, bufRegEnumCom);
		buffer += len;
	}

	buffer[0] = 0;

	return n;
}

#endif 

class CComPortSelectDialog {
public:
	wchar_t * port;
	long      buf_size;
	const wchar_t * title;
	wchar_t   ports[4096];
	HWND      m_hWnd;
	HDEVNOTIFY dev_notify;
	long ShowModal(HWND hParent);
	INT_PTR OnInitDialog(WPARAM wParam, LPARAM lParam);
	INT_PTR DialogProc(UINT uMsg,WPARAM wParam, LPARAM lParam);
	CComPortSelectDialog(wchar_t * p, long s, const wchar_t * t) { memset(this,0,sizeof(*this)); port = p; buf_size = s; title = t;}
	void InitPortSettings();
	void FillPortCombo();
	void SelectPort();
	void SetDefaultPortSettings();
	void OnOK();
	void OnDeviceChange(WPARAM wParam, LPARAM lParam);
	void RegisterDeviceEventNotifications();
	void OnPortChange(WPARAM wParam, LPARAM lParam);
};

INT_PTR CComPortSelectDialog::OnInitDialog(WPARAM wParam, LPARAM lParam)
{
	SetWindowText(m_hWnd,title);
	InitPortSettings();
	FillPortCombo();
	SelectPort();
	SetDefaultPortSettings();
	RegisterDeviceEventNotifications();
	return TRUE;
}

typedef struct _DEV_BROADCAST_DEVICEINTERFACE_W {
  DWORD dbcc_size;
  DWORD dbcc_devicetype;
  DWORD dbcc_reserved;
  GUID  dbcc_classguid;
  wchar_t dbcc_name[1];
} DEV_BROADCAST_DEVICEINTERFACE_W, *PDEV_BROADCAST_DEVICEINTERFACE_W;
#define DEV_BROADCAST_DEVICEINTERFACE DEV_BROADCAST_DEVICEINTERFACE_W
#define PDEV_BROADCAST_DEVICEINTERFACE PDEV_BROADCAST_DEVICEINTERFACE_W
#define DBT_DEVTYP_DEVICEINTERFACE 0x00000005
#if _MSC_VER <= 1500
const GUID GUID_DEVINTERFACE_COMPORT = {0x86E0D1E0,0x8089,0x11D0,{0x9C,0xE4,0x08,0x00,0x3E,0x30,0x1F,0x73}};
#endif
typedef struct _DEV_BROADCAST_HDR {
  DWORD dbch_size;
  DWORD dbch_devicetype;
  DWORD dbch_reserved;
} DEV_BROADCAST_HDR, *PDEV_BROADCAST_HDR;
#define DBT_DEVICEARRIVAL 0x8000
#define DBT_DEVICEREMOVECOMPLETE 0x8004

void CComPortSelectDialog::RegisterDeviceEventNotifications()
{
	DEV_BROADCAST_DEVICEINTERFACE filter = {sizeof(DEV_BROADCAST_DEVICEINTERFACE) };
	filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
	filter.dbcc_classguid = GUID_DEVINTERFACE_COMPORT;
    dev_notify = RegisterDeviceNotification(m_hWnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
}

void CComPortSelectDialog::OnDeviceChange(WPARAM wParam, LPARAM lParam)
{
    PDEV_BROADCAST_HDR lpdb = (PDEV_BROADCAST_HDR) lParam;
    if(!lpdb || (lpdb->dbch_devicetype != DBT_DEVTYP_DEVICEINTERFACE)) return;
    PDEV_BROADCAST_DEVICEINTERFACE lpdbv = (PDEV_BROADCAST_DEVICEINTERFACE) lpdb;
	const wchar_t * name = lpdbv->dbcc_name;
    switch (wParam)
    {
    case DBT_DEVICEARRIVAL:
    case DBT_DEVICEREMOVECOMPLETE:
        break;
	default:
		return;
	}
	size_t len = wcslen(name) + 1;
	wchar_t * buf = (wchar_t * ) ::HeapAlloc(GetProcessHeap(),0,len*2);
	if(!buf) return;
	wcscpy_s(buf,len,name);
	PostMessage(m_hWnd,WM_APP,wParam,(LPARAM)buf);
}

void CComPortSelectDialog::OnPortChange(WPARAM wParam, LPARAM lParam)
{
	wchar_t vidpid[MAX_PORT_NAME];
	wchar_t instid[MAX_PORT_NAME];
	wchar_t keyname[256];
	wchar_t friname[MAX_PORT_NAME] = L"";
	wchar_t comport[MAX_PORT_NAME] = L"";
	wchar_t tmp[MAX_PORT_NAME];

	wchar_t * name = (wchar_t*) lParam;
	if(!name) return;
	
	int fields = swscanf_s(name,L"\\\\?\\USB#%[^#]#%[^#]",vidpid,MAX_PORT_NAME,instid,MAX_PORT_NAME);
	::HeapFree(GetProcessHeap(),0,(LPVOID)lParam);
	if(fields != 2) return;

	wsprintf(keyname,L"SYSTEM\\CurrentControlSet\\Enum\\USB\\%s\\%s",vidpid,instid);
	HKEY key = NULL;
	RegOpenKey(HKEY_LOCAL_MACHINE,keyname,&key);
	if(key)
	{
		DWORD type;
		DWORD cbb = (DWORD)(countof(friname) * sizeof(wchar_t));
		RegQueryValueEx(key,L"FriendlyName",0,&type,(LPBYTE)friname,&cbb);
		RegCloseKey(key);
	}

	wcscat_s(keyname,countof(keyname),L"\\Device Parameters");
	key = NULL;
	RegOpenKey(HKEY_LOCAL_MACHINE,keyname,&key);
	if(key)
	{
		DWORD type;
		DWORD cbb = (DWORD)(countof(comport) * sizeof(wchar_t));
		RegQueryValueEx(key,L"PortName",0,&type,(LPBYTE)comport,&cbb);
		RegCloseKey(key);
	}

	if(!wcslen(comport)) return;

	// see if this port is in the list
	HWND hCtrl = GetDlgItem(m_hWnd,IDC_COMBO_PORT);
	UINT iItem = -1;
	size_t len = wcslen(comport);
	LRESULT cnt = SendMessage(hCtrl,CB_GETCOUNT,0,0);
	for(int i = 0; i < cnt; ++i)
	{
		SendMessage(hCtrl,CB_GETLBTEXT,i,(LPARAM)tmp);
		if(!wcsncmp(tmp,comport,len))
		{
			iItem = i;
			break;
		}
	}
	if((wParam == DBT_DEVICEARRIVAL) && (iItem == -1))
	{
		// add Friendly name
		len = wcslen(comport);
		size_t left = countof(comport)-len;
		size_t len2 = wcslen(friname);
		if(left > (len2 + 1))
		{
			wcscat_s(comport,left,L" "); ++len; --left;
			wcscat_s(comport,left,friname); len += len2; left -= len2;
		}
		len2 = wcslen(vidpid);
		if(left > (len2 + 1))
		{
			wcscat_s(comport,left,L" "); ++len; --left;
			wcscat_s(comport,left,vidpid);
		}
		iItem = (UINT) SendMessage(hCtrl,CB_ADDSTRING,0,(LPARAM)comport);
		SendMessage(hCtrl,CB_SETCURSEL,iItem,0);
	}
	if((wParam == DBT_DEVICEREMOVECOMPLETE) && (iItem != -1))
	{
		// remove
		SendMessage(hCtrl,CB_DELETESTRING,iItem,0);
		SendMessage(hCtrl,CB_SETCURSEL,0,0);
	}
	SetDefaultPortSettings();
}

void CComPortSelectDialog::InitPortSettings()
{
	HWND hCtrl;
	wchar_t buf[64];
	
	hCtrl = GetDlgItem(m_hWnd,IDC_COMBO_BAUDRATE);
	SendMessage(hCtrl,CB_RESETCONTENT,0,0);
	for(int i = 0; i < countof(baudrates); ++i)
	{
		wsprintf(buf,L"%ld",baudrates[i]);
		SendMessage(hCtrl,CB_ADDSTRING,0,(LPARAM)buf);
	}

	hCtrl = GetDlgItem(m_hWnd,IDC_COMBO_DATABITS);
	SendMessage(hCtrl,CB_RESETCONTENT,0,0);
	for(int i = 0; i < countof(szDataBits); ++i)
	{
		SendMessage(hCtrl,CB_ADDSTRING,0,(LPARAM)szDataBits[i]);
	}

	hCtrl = GetDlgItem(m_hWnd,IDC_COMBO_PARITY);
	SendMessage(hCtrl,CB_RESETCONTENT,0,0);
	for(int i = 0; i < countof(szParities); ++i)
	{
		SendMessage(hCtrl,CB_ADDSTRING,0,(LPARAM)szParities[i]);
	}

	hCtrl = GetDlgItem(m_hWnd,IDC_COMBO_STOPBITS);
	SendMessage(hCtrl,CB_RESETCONTENT,0,0);
	for(int i = 0; i < countof(szStopBits); ++i)
	{
		SendMessage(hCtrl,CB_ADDSTRING,0,(LPARAM)szStopBits[i]);
	}
	
	hCtrl = GetDlgItem(m_hWnd,IDC_COMBO_FLOWCONTROL);
	SendMessage(hCtrl,CB_RESETCONTENT,0,0);
	for(int i = 0; i < countof(szFlowControls); ++i)
	{
		SendMessage(hCtrl,CB_ADDSTRING,0,(LPARAM)szFlowControls[i]);
	}
}

size_t get_next_port(wchar_t * port, long cnt, wchar_t * & p, int tabs = 3)
{
	--cnt;
	long i = 0;
	while((i < cnt) && p[i] && (p[i] != L'\n'))
	{
		port[i] = p[i];
		if(port[i] == L'\t')
		{
			--tabs;
			if(tabs <= 0) break;
			port[i] = L' ';
		}
		++i;
	}
	port[i] = 0;
	while(p[i] && (p[i] != L'\n')) { ++i; }
	if(p[i] == L'\n') ++i;
	p += i;
	return i;
}

void CComPortSelectDialog::FillPortCombo()
{
	HWND hCtrl;
#if _MSC_VER <= 1500
	long port_count = NkComPort_ListPorts(ports, countof(ports));
#else
	long port_count = NkComPort_ListPortsEx(ports, countof(ports));
#endif

	wchar_t port[MAX_PORT_NAME];
	wchar_t * p = ports;
	hCtrl = GetDlgItem(m_hWnd,IDC_COMBO_PORT);
	SendMessage(hCtrl,CB_RESETCONTENT,0,0);
	for(int i = 0; i < port_count; ++i)
	{
		get_next_port(port,MAX_PORT_NAME,p);
		SendMessage(hCtrl,CB_ADDSTRING,0,(LPARAM)port);
	}
}

void CComPortSelectDialog::SelectPort()
{
	HWND hCtrl;
	wchar_t cur[MAX_PORT_NAME] = L"";
	wchar_t str[MAX_PORT_NAME];

	hCtrl = GetDlgItem(m_hWnd,IDC_COMBO_PORT);
	swscanf_s(port,PORTFORMAT,cur,260);
	size_t len = wcslen(cur);
	UINT curSel = 0;
	if(len)
	{
		LRESULT cnt = SendMessage(hCtrl,CB_GETCOUNT,0,0);
		for(int i = 0; i < cnt; ++i)
		{
			SendMessage(hCtrl,CB_GETLBTEXT,i,(LPARAM)str);
			if(!wcsncmp(str,cur,len))
			{
				curSel = i;
				break;
			}
		}
	}
	SendMessage(hCtrl,CB_SETCURSEL,curSel,0);
}

void CComPortSelectDialog::SetDefaultPortSettings()
{
	HWND hCtrl;
	SNkComPort cp;
	wchar_t sel[MAX_PORT_NAME];
	wchar_t selport[MAX_PORT_NAME];
	wchar_t buf[64];
	wchar_t * p;
	int i;

	// see if a port is selected from the predefined ports 
	hCtrl = GetDlgItem(m_hWnd,IDC_COMBO_PORT);
	LRESULT iCurSel = SendMessage(hCtrl,CB_GETCURSEL,0,0);
	if(iCurSel == -1)
	{
		SendMessage(hCtrl,WM_GETTEXT,MAX_PORT_NAME,(LPARAM)sel);
	}
	else
	{
		SendMessage(hCtrl,CB_GETLBTEXT,iCurSel,(LPARAM)sel);
	}
	p = sel;
	get_next_port(selport,MAX_PORT_NAME,p,1);

	// check if this is the currently selected port
	size_t len1 = wcscspn(selport,L": \t\n\r");
	size_t len2 = wcscspn(port,L": \t\n\r");
	size_t len = min(len1,len2);
	if(len && !wcsncmp(port,selport,len))
	{
		wcsncpy_s(selport,MAX_PORT_NAME,port,MAX_PORT_NAME);
	}

	cp.ParsePort(selport);

	hCtrl = GetDlgItem(m_hWnd,IDC_COMBO_BAUDRATE);
	wsprintf(buf,L"%ld",cp.baudrate);
	SendMessage(hCtrl,WM_SETTEXT,0,(LPARAM)buf);

	hCtrl = GetDlgItem(m_hWnd,IDC_COMBO_DATABITS);
	for(i = 0; i < countof(iDataBits); ++i) { if(cp.databits == iDataBits[i]) break; } if(i >= countof(iDataBits)) i = -1;
	SendMessage(hCtrl,CB_SETCURSEL,i,0);

	hCtrl = GetDlgItem(m_hWnd,IDC_COMBO_PARITY);
	SendMessage(hCtrl,CB_SETCURSEL,cp.parity,0);

	hCtrl = GetDlgItem(m_hWnd,IDC_COMBO_STOPBITS);
	SendMessage(hCtrl,CB_SETCURSEL,cp.stopbits,0);
	
	hCtrl = GetDlgItem(m_hWnd,IDC_COMBO_FLOWCONTROL);
	SendMessage(hCtrl,CB_SETCURSEL,cp.handshake,0);
}

void CComPortSelectDialog::OnOK()
{
	HWND hCtrl;
	SNkComPort cp;
	int i;
	wchar_t sel[MAX_PORT_NAME];
	wchar_t tmp[MAX_PORT_NAME];
	
	hCtrl = GetDlgItem(m_hWnd,IDC_COMBO_PORT);
	SendMessage(hCtrl,WM_GETTEXT,MAX_PORT_NAME,(LPARAM)sel);
	sel[wcscspn(sel,L" :\r\n\t")] = 0;

	cp.ParseName(sel);
	cp.ParseDefaultOptions();

	hCtrl = GetDlgItem(m_hWnd,IDC_COMBO_BAUDRATE);
	SendMessage(hCtrl,WM_GETTEXT,countof(tmp),(LPARAM)tmp);
	i = _wtoi(tmp);
	if(i) cp.baudrate = i;

	hCtrl = GetDlgItem(m_hWnd,IDC_COMBO_DATABITS);
	i = (int) SendMessage(hCtrl,CB_GETCURSEL,0,0);
	if((i >= 0) && (i < countof(iDataBits))) cp.databits = iDataBits[i];

	hCtrl = GetDlgItem(m_hWnd,IDC_COMBO_PARITY);
	cp.parity = (eParity) SendMessage(hCtrl,CB_GETCURSEL,0,0);

	hCtrl = GetDlgItem(m_hWnd,IDC_COMBO_STOPBITS);
	cp.stopbits = (eStopbit) SendMessage(hCtrl,CB_GETCURSEL,0,0);
	
	hCtrl = GetDlgItem(m_hWnd,IDC_COMBO_FLOWCONTROL);
	cp.handshake = (eHandshake) SendMessage(hCtrl,CB_GETCURSEL,0,0);

	cp.AddSettings();

	wcsncpy_s(port,buf_size,cp.name+4,buf_size);
}

INT_PTR CComPortSelectDialog::DialogProc(UINT uMsg,WPARAM wParam, LPARAM lParam)
{
	switch(uMsg)
	{
	case WM_COMMAND:
		switch(LOWORD(wParam))
		{
		case IDOK:
			OnOK();
			EndDialog(m_hWnd, TRUE);
			break;
		case IDCANCEL:
			EndDialog(m_hWnd, FALSE);
			break;
		case IDC_COMBO_PORT:
			if(HIWORD(wParam) == CBN_SELENDOK) SetDefaultPortSettings();;
			break;
		}
		break;
	case WM_CLOSE:
		EndDialog(m_hWnd, FALSE);
		break;
    case WM_DEVICECHANGE:
		OnDeviceChange(wParam,lParam);
		break;
	case WM_NCDESTROY:
		UnregisterDeviceNotification(dev_notify);
		break;
	case WM_APP:
		OnPortChange(wParam,lParam);
		break;
	}
	return FALSE;
}

INT_PTR CALLBACK NkComPortSelectDialogProc(HWND hDlg, UINT uMsg, WPARAM wParam,LPARAM lParam)
{
	CComPortSelectDialog * pMe = (CComPortSelectDialog*)GetWindowLongPtr(hDlg,DWLP_USER);
	switch(uMsg)
	{
		case WM_INITDIALOG:
			pMe = (CComPortSelectDialog*) lParam;
			SetWindowLongPtr(hDlg,DWLP_USER,(LONG_PTR)pMe);
			pMe->m_hWnd = hDlg;
			return pMe->OnInitDialog(wParam,lParam);
		case WM_NCDESTROY:
			pMe->DialogProc(uMsg,wParam,lParam);
			pMe->m_hWnd = NULL;
			SetWindowLongPtr(hDlg,DWLP_USER,(LONG_PTR)0);
			return FALSE;
		default:;
			if(pMe) return pMe->DialogProc(uMsg,wParam,lParam);
	}
	return FALSE;
}

long CComPortSelectDialog::ShowModal(HWND hParent)
{
	INT_PTR result = DialogBoxParam(g_hInstance,MAKEINTRESOURCE(IDD_SELECTCOMPORT),hParent,NkComPortSelectDialogProc,(LPARAM)this);
	return result ? TRUE : FALSE;
}

NKCOMPORT_API NkComPort_SelectPortDialog(HWND hParent, const wchar_t * title, wchar_t * port, long buf_size)
{
	CComPortSelectDialog dlg(port,buf_size,title);
	return dlg.ShowModal(hParent);
}