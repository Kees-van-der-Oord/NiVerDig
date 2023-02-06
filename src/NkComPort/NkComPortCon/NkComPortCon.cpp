// NkTs2FLCtrl.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "../NkComPort/NkComPort.h"

wchar_t port_name[64] = L"";
bool auto_connect = false;
bool unicode = false;
bool silent = false;
bool verbose = false;
bool usedlg = false;
FILE * input = stdin;
FILE * output = NULL;
NKCOMPORT * port = NULL;
#define command_size 4096
wchar_t answer[command_size];
wchar_t command[command_size];
#define history_size 40
wchar_t history[history_size][4096] = {0};
int64_t  history_cursor = -1;
int64_t history_begin = 0;
int64_t history_end = -1;
bool history_changed = true;
wchar_t commandline[260] = L"";
typedef std::map<std::wstring,__int64> t_labels;
t_labels labels;

long GetComPort();
long FindComPort();
void Log(const wchar_t * format,...);
int process_stdin();
int process_input();
FILE * open_file(const wchar_t * name, const wchar_t * mode, int flags);
void print_usage(void);
int read_compare_line(wchar_t * expected, __int64 timeout);

void print_usage(void)
{
	fwprintf(stderr,
		L"NkComPortCon.exe [<port>[:<parameters>]] [options]\n"
		L"example: NkComPortCom CoolLED:115200,n,8,1,n,100 -i input.txt -o log.txt\n"
		L" port:          'COM1' etc or unique substring of COM port description\n"
		L" parameters:    <baudrate>,<parity>,<data>,<stop>,<flow>,<timeout>\n"
		L"		baudrate: 3200 to 1000000\n"
		L"		parity:   n,o,e,m,s (none,odd,even,mark,space)\n"
		L"		data:     7,8\n"
		L"		stop:     1,2\n"
		L"		flow:     n,x,p,b (none,xonxoff,rtscts,both\n"
		L"		timeout:  0 to 10000 ms\n"
		L" options:\n"
		L" -g: show select port dialog\n"
		L" -o <filename>: log to file\n"
		L" -i <filename>: play file\n"
		L"    ::<comment>                  comment\n"
		L"    :<label>                     define label\n"
		L"    <timestamp> Send: <command>  send command\n"
		L"    <timestamp> Recv: <response> read response\n"
		L"    <timestamp> Goto: <label>    jump back to label\n"
		L"    timestamp:                   YYYY-dd-mm HH:MM:SS.sss 2017-01-16 14:43:28.499\n"
		);
}

void history_add(wchar_t* command)
{
	// check if the command
	if (history_cursor == -1)
	{
		history_cursor = 0;
		history_begin = 0;
		history_end = 0;
	}
	else
	{
		if (!wcscmp(command, history[history_cursor])) return;
		history_end = (history_end + 1) % history_size;
		if(history_end == history_begin) history_begin = (history_begin + 1) % history_size;
	}
	wcscpy_s(history[history_end], command_size, command);
	history_cursor = history_end;
}

void history_get(wchar_t* command, int64_t dir)
{
	if (dir > 0)
	{
		if (history_cursor != history_end)
		{
			history_cursor = (history_cursor + 1) % history_size;
		}
	}
	if (dir < 0)
	{
		if (history_cursor != history_begin)
		{
			history_cursor = (history_cursor - 1) % history_size;
		}
	}
	wcscpy_s(command, command_size, history[history_cursor]);
}

int _tmain(int argc, _TCHAR* argv[])
{
	--argc; ++argv;
	for(;argc;--argc,++argv)
	{
		if(argv[0][0] != L'-') 
		{
			if(!port_name[0])
			{
				wcsncpy_s(port_name,countof(port_name),argv[0],countof(port_name));
				FindComPort();
				continue;
			}
		}
		if(!wcscmp(argv[0],L"-u")) unicode = true;
		else if(!wcscmp(argv[0],L"-s")) silent = true;
		else if(!wcscmp(argv[0],L"-v")) verbose = true;
		else if(!wcscmp(argv[0],L"-g")) usedlg = true;
		else if(!wcscmp(argv[0],L"-i"))
		{
			if(argc == 1) 
			{
				fwprintf(stderr,L"no filename for input option -i\n");
				continue;
			}
			--argc; ++argv;
			FILE * f = open_file(argv[0],L"rt",_SH_DENYWR);
			if(!f) fwprintf(stderr,L"error opening input file %s\n",argv[0]);
			else input = f;
		}
		else if(!wcscmp(argv[0],L"-o"))
		{
			if(argc == 1)
			{
				fwprintf(stderr,L"no filename for output option -i\n");
				continue;
			}
			--argc; ++argv;
			FILE * f = open_file(argv[0],L"at",_SH_DENYWR);
			if(!f) fwprintf(stderr,L"error opening output file %s\n",argv[0]);
			else output = f;
		}
		else 
		{
			fwprintf(stderr,L"invalid argument %s\n",argv[0]);
			print_usage();
		}
	}
	if(!silent)
	{
		fwprintf(stderr,L"Nikon COM port terminal\n");
		fwprintf(stderr,L"instruments-support.eu@nikon.com\n");
		fwprintf(stderr,L"press <Ctrl>-C to disconnect/quit\n");
	}
	while(1)
	{
		NkComPort_Close(&port);
		if(!auto_connect && !GetComPort()) return 0;
		if(!silent) fwprintf(stderr,L"connecting ...\n");
		NkComPort_Open(&port,port_name);
		auto_connect = 0;
		if(NkComPort_GetConnectionDetails(port,port_name,64))
		{
			if(!silent) fwprintf(stderr,L"connected to: %s\n",port_name);
			break;
		}
	}
	if(input == stdin) process_stdin();
	else process_input();
	wprintf(L"disconnected\n");
	NkComPort_Close(&port);
	return 0;
}

int process_stdin()
{
	//while(read_compare_line(NULL,0) >= 0) {}
	size_t i = 0;    // characters in the command line
	size_t j = 0;    // current position of the cursor
	size_t jmax = 0; // most right character printed
	bool insert = true;
	bool history = false;
	bool last_was_cr = false;
	while(NkComPort_IsConnected(port))
	{
		bool send_command = false;
		while(_kbhit())
		{
			wchar_t c[2] = { 0 };
			c[0] = _getwch();
			switch (c[0])
			{
			case 27: // ESC
				command[0] = 0;
				wprintf(L"\r");
				for (size_t t = jmax; t; --t) wprintf(L" ");
				wprintf(L"\r");
				i = j = jmax;
				break;
			case '\n': continue; // ignore newlines
			case '\b':
				if (j > 0)
				{
					--j;
					wcscpy_s(command + j, countof(command) - j, command + j + 1);
					--i;
					wprintf(L"\r");
					wprintf(command);
					for (size_t t = jmax - i; t; --t) wprintf(L" ");
					for (size_t t = (jmax - j); t; --t) wprintf(L"\b");
					--jmax;
				}
				break;
			case 0xE0:
				c[0] = _getwch(); // skip the '['
				switch (c[0])
				{
				case L'G': // home
					while (j) { wprintf(L"\b"); --j; }
					break;
				case L'O': // end
					if (j < i) { wprintf(command + j); j = i; }
					break;
				case L'H': // up
				case L'P': // down
					if(c[0] == L'H') history_get(command, history ? -1 : 0);
					else if (history) history_get(command, 1);
					history = true;
					wprintf(L"\r");
					wprintf(command);
					i = j = wcslen(command);
					if (jmax > i)
					{
						for (size_t t = jmax - i; t; --t) wprintf(L" ");
						for (size_t t = jmax - j; t; --t) wprintf(L"\b");
					}
					jmax = i;
					break;
				case L'M': // right
					if (j < i)
					{
						wchar_t t[2] = { command[j] };
						wprintf(t);
						++j;
					}
					break;
				case L'K': // left
					if (j > 0)
					{
						wprintf(L"\b");
						--j;
					}
					break;
					/*
									case L'R': // insert
										{
											HANDLE console = GetStdHandle(STD_INPUT_HANDLE);
											DWORD mode = 0;
											GetConsoleMode(console, &mode);
											insert = !insert;
											if (insert) mode |= ENABLE_INSERT_MODE;
											else mode &= ~ENABLE_INSERT_MODE;
											SetConsoleMode(console, mode);
											break;
										}
					*/
				}
				break;
			default:
				if (c[0] == L'\r')
				{
					j = i;
					history_add(command);
				}
				if (j < i)
				{
					memmove(command + j + 1, command + j, i - j + 1);
				}
				command[j] = c[0];
				++i;
				++jmax;
				command[i] = 0;
				wprintf(command + j);
				++j;
				for (size_t t = i - j; t; --t)	wprintf(L"\b");
				send_command = i >= (countof(command) - 1);
				if(c[0] == L'\r')
				{
					wprintf(L"\n");
					send_command = 1;
				}
			break;
			}
		}
		if(send_command)
		{
			NkComPort_WriteLine(port,command);
			i = 0;
			j = 0;
			jmax = 0;
			command[i] = 0;
			history = false;
		}
		while(NkComPort_ReadLine(port,answer,countof(answer),1))
		{
			for(int i = 0; (i < countof(answer)) && answer[i]; ++i)
			{
				if(last_was_cr && (answer[i] == L'\n')) continue;
				wprintf(L"%c",answer[i]);
				last_was_cr = answer[i] == L'\r';
				if(last_was_cr) wprintf(L"\n");
			}
		}
	}
	return 0;
}

bool iswidchar(wchar_t c) { return iswalnum(c) || (c == L'_'); }

__int64 scan_timestamp(wchar_t * &p)
{
	tm date;
	int result;
	long ms;
	time_t time;
	int len = 0;
	result = swscanf_s(p,L"%d-%d-%d %d:%d:%d.%d%n",
		&date.tm_year, &date.tm_mon, &date.tm_mday,
		&date.tm_hour, &date.tm_min, &date.tm_sec,
		&ms,&len);
	if(result != 7) return -1;
	if(ms < 0) return -1;
	if(ms > 999) return -1;
	date.tm_year -= 1900; /* years since 1900 */
	date.tm_mon -= 1;     /* 0 - 11 range */
	date.tm_isdst = -1;   /* automatically determine DST */
	time = mktime(&date);
	if(time == -1) return -1;
	p += len;
	return time * 1000 + ms; // milliseconds since the start of the Unix epoch: midnight UTC of January 1, 1970 
}

int wcsrmv(wchar_t * s, const wchar_t * c)
{
	wchar_t * d = s;
	while(*s)
	{
		if(!wcschr(c,*s))
		{
			*d = *s;
			++d;
		}
		++s;
	}
	*d = 0;
	return 0;
}

int read_compare_line(wchar_t * expected, __int64 timeout)
{
	if(timeout < 0) timeout = 0;
	if(timeout > 10000) timeout = 10000;
	__int64 ticks = GetTickCount64();
	int result = NkComPort_ReadLine(port,answer,countof(answer),(DWORD) timeout + 50);
	ticks = GetTickCount64() - ticks;
	if(result <= 0)
	{
		if(expected)
		{
			wcsrmv(expected,L"\r\n");
			Log(L"Recv: NOK Timeout != '%s' (%I64d,%I64d ms)\n",expected,ticks,timeout);
		}
		return -1;
	}
	wcsrmv(answer, L"\r\n");
	if (!expected)
	{
		Log(L"Recv: '%s' (%I64d,%I64d ms)\n", answer, ticks, timeout);
	}
	else
	{
		wcsrmv(expected, L"\r\n");
		int nOK = wcscmp(expected, answer);
		Log(L"Recv: %s '%s' %s '%s' (%I64d,%I64d ms)\n", nOK ? L"NOK" : L" OK", answer, nOK ? L"!=" : L"==", expected, ticks, timeout);
	}
	return 1;
}

__int64 time_of_last_command = 0;
__int64 tick_of_last_command = 0;

void  sleep_until_tick(__int64 time_of_new_command)
{
	if(time_of_last_command)
	{
		__int64 wait = time_of_new_command - time_of_last_command;
		__int64 timeout = tick_of_last_command + wait;
		__int64 sleep = timeout - GetTickCount64();
		while(sleep > 0) 
		{
			fwprintf(stderr,L"waiting %20I64d ms\r",sleep);
			if(sleep > 1000) sleep = 1000;
			Sleep((DWORD)sleep);
			sleep = timeout - GetTickCount64();
		}
		fwprintf(stderr,L"%64s\r",L"");
	}
	time_of_last_command = time_of_new_command;
	tick_of_last_command = GetTickCount64();
}

int process_input()
{
	__int64 time_of_new_command = 0;
	int len;
	while(fgetws(commandline,countof(commandline),input))
	{
		__int64 pos = _ftelli64(input);
		wchar_t *p = commandline;
		while(iswspace(*p)) ++p;
		// first handle labels and comments
		if(*p == L':')
		{
			++p;
			if(*p == L':') continue; // comment
			while(iswspace(*p)) ++p;
			wchar_t *q = p;
			while(iswidchar(*q)) ++q;
			size_t len = q - p;
			if(!len) continue; // no id
			std::wstring name(p,len);
			labels[name] = pos;
			continue;
		}

		// next a normal command line
		time_of_new_command = scan_timestamp(p);
		if(swscanf_s(p,L"%64s%n",command,(unsigned short)countof(command),&len) != 1) continue;
		p += len;

		// first the timestamp
		if(time_of_new_command == -1) continue;
		if(wcscmp(command,L"Recv")) 
		{
			sleep_until_tick(time_of_new_command);
		}

		// next the command
		if(!wcscmp(command,L"Goto"))
		{
			if(swscanf_s(p,L"%64s",command,(unsigned short)countof(command)) != 1) continue;
			wchar_t * q = command;
			if(*q == L':') ++q;
			std::wstring label(q);
			t_labels::iterator i = labels.find(label);
			if(i == labels.end()) continue;
			pos = i->second;
			_fseeki64(input,pos,SEEK_SET);
			continue;
		} 
		if(!wcscmp(command,L"Send"))
		{
			while(read_compare_line(NULL,0) >= 0) {};
			command[0] = 0;
			swscanf_s(p,L"%64s",command,(unsigned short)countof(command));
			size_t l = wcslen(command);
			wcscat_s(command+l,countof(command)-l,L"\n");
			NkComPort_WriteLine(port,command);
			Log(L"Send:     %s",command);
			continue;
		}
		if(!wcscmp(command,L"Recv"))
		{
			while(iswspace(*p)) ++p;
			read_compare_line(p,time_of_new_command - time_of_last_command);
			continue;
		}
		if(!NkComPort_IsConnected(port))
		{
			Log(L"Disconnected\n");
			break;
		}
	}
	return 0;
}

long GetComPort()
{
	if(usedlg)
	{
		return NkComPort_SelectPortDialog(GetConsoleWindow(),L"NkComPortConsole",port_name,64);
	}
	wchar_t buffer[2048];
	long port_count = NkComPort_ListPortsEx(buffer, 2048);
	if(auto_connect && (port_count == 1) && !verbose)
	{
		wcsncpy_s(port_name,64,buffer,wcscspn(buffer,L"\t"));
		return 1;
	}
	fwprintf(stderr,L"Available COM ports:\n");
	fwprintf(stderr,L"%s",buffer);
	fwprintf(stderr,L"Please enter COM  port: ");
	if(!_getws_s(port_name,64))
	{
		return 0;
	}
	return 1;
}

wchar_t * wcsistr(wchar_t * str, wchar_t * pat)
{
	size_t l = wcslen(pat);
	for(;*str;++str)
		if(!_wcsnicmp(str,pat,l))
			return str;
	return NULL;
}

wchar_t * wcsbol(wchar_t * buf, wchar_t * p)
{
	// walk back to the begin of the line
	for(--p, --buf; p > buf; --p) 
	{ 
		if((*p == L'\n') || (*p == L'\r')) 
		{
			break; 
		} 
	}
	++p;
	return p;
}

long FindComPort()
{
	wchar_t buffer[2048];
	long port_count = NkComPort_ListPorts(buffer, 2048);
	if(!port_count) return 0;
	port_count = 0;
	wchar_t * match = NULL;
	wchar_t name[64] = L"";
	wchar_t params[64] = L"";
	size_t n = wcscspn(port_name,L":");
	wcsncpy_s(name,countof(name),port_name,n);
	wcscpy_s(params,countof(params),port_name+n);
	size_t param_len = wcslen(params);
	for(wchar_t * p = wcsistr(buffer,name); p; p = wcsistr(p+1,name))
	{
		match = p;
		++port_count;
		p = wcschr(p,L'\n');
	}
	if(port_count != 1) return 0;
	match = wcsbol(buffer,match);
	wchar_t * tab = wcspbrk(match,L"\t\n");
	if(!tab) return 0;
	n = tab-match;
	if((n + 1 + param_len) >= countof(port_name)) return 0;
	wcsncpy_s(port_name,countof(port_name),match,n);
	if(param_len) wcscat_s(port_name+n,countof(port_name)-n,params);
	auto_connect = true;
	return 1;
}

FILE * open_file(const wchar_t * name, const wchar_t * mode, int flags)
{
#ifdef _DEBUG
	wchar_t dir[260];
	GetCurrentDirectory(260,dir);
#endif
	return _wfsopen(name,mode,flags);
}

typedef struct t_timestamp
{
	__int64 ms_start; // time in ms since Jan 1, 1970
	t_timestamp()
	{
		ms_start = time(NULL) * 1000 - GetTickCount64();
	}
	__int64 GetTimeStamp()
	{
		return ms_start + GetTickCount64();
	}
	void FormatTimeStamp(wchar_t * buffer, size_t size)
	{
		if(size < 24) return;
		__int64 now = GetTimeStamp();
		time_t time = now / 1000;
		DWORD ms = (DWORD)(now%1000);
		tm tm;
		//gmtime_s(&tm, &time);
		localtime_s(&tm,&time);
		size_t n = wcsftime(buffer,size, L"%Y-%m-%d %H:%M:%S",&tm);
		if(n != 19) return;
		wsprintf(buffer+n,L".%03d",ms);
	}
} t_timestamp;
t_timestamp g_timestamp;

void Log(const wchar_t * format,...)
{
	va_list args;
	va_start(args, format);
	vfwprintf(stderr,format,args);
	if(output)
	{
		wchar_t buf[24];
		g_timestamp.FormatTimeStamp(buf,24);
		fwprintf(output,L"%s ",buf);
		vfwprintf(output,format,args);
		fflush(output);
	}
	va_end(args);

}

