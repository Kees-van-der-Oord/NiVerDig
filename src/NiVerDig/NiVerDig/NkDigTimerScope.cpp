#include "framework.h"

class panelScope;
wxString FormatPeriod(size_t period);
size_t GetPeriod(long index);
size_t FindPeriod(size_t timer);

#define SYNC_CHANNEL -1   // -1: data entry for sync
#define SYNC_END     0xFF // -1: end of scope mode
#define SYNC_TICK    0xFE // -2: make the graph tick

struct STimeRes
{
	int            id;
	const wchar_t* label;
	size_t         period;
};

STimeRes timeRes[] =
{
	{10000, _("   1 ms "),        10000ULL },
	{10001, _("   2 ms "),        20000ULL },
	{10002, _("   5 ms "),        50000ULL },
	{10003, _("  10 ms "),       100000ULL },
	{10004, _("  20 ms "),       200000ULL },
	{10005, _("  50 ms "),       500000ULL },
	{10006, _(" 100 ms "),      1000000ULL },
	{10007, _(" 200 ms "),      2000000ULL },
	{10008, _(" 500 ms "),      5000000ULL },
	{10009, _("   1 s  "),     10000000ULL },
	{10010, _("   2 s  "),     20000000ULL },
	{10011, _("   5 s  "),     50000000ULL },
	{10012, _("  10 s  "),    100000000ULL },
	{10013, _("  20 s  "),    200000000ULL },
	{10014, _("  30 s  "),    300000000ULL },
	{10015, _("   1 min"),    600000000ULL },
	{10016, _("   2 min"),   1200000000ULL },
	{10017, _("   5 min"),   1800000000ULL },
	{10018, _("  10 min"),   6000000000ULL },
	{10019, _("  20 min"),  12000000000ULL },
	{10020, _("  30 min"),  18000000000ULL },
	{10021, _("   1 hr "),  36000000000ULL },
	{10022, _("   2 hr "),  72000000000ULL },
	{10023, _("   4 hr "), 148000000000ULL },
	{10024, _("   6 hr "), 220000000000ULL },
	{10025, _("  12 hr "), 440000000000ULL },
	{10026, _("   1 day"), 880000000000ULL }
};

void FileTimeToUnixTime(FILETIME ft, time_t* t, int* ms)
{
	LONGLONG ll = ft.dwLowDateTime | (static_cast<LONGLONG>(ft.dwHighDateTime) << 32);
	ll -= 116444736000000000;
	*ms = (ll % 10000000) / 10000;
	ll /= 10000000;
	*t = static_cast<time_t>(ll);
}

size_t GetCurrentTimeInMs()
{
	SYSTEMTIME st;
	GetLocalTime(&st);
	tm  date = { 0 };
	date.tm_year = st.wYear - 1900; /* years since 1900 */
	date.tm_mon = st.wMonth - 1;     /* 0 - 11 range */
	date.tm_mday = st.wDay;
	date.tm_hour = st.wHour + 1;
	date.tm_min = st.wMinute;
	date.tm_sec = st.wSecond;
	date.tm_isdst = -1;   /* automatically determine DST */
	return mktime(&date) * 1000 + st.wMilliseconds;
}

size_t GetCurrentFileTime()
{
	//return GetCurrentTimeInMs() * 1000LL; // convert from s to us
	SYSTEMTIME st;
	GetSystemTime(&st);
	FILETIME ft;
	SystemTimeToFileTime(&st, &ft);
	size_t ull;
	((unsigned long*)&ull)[0] = ft.dwLowDateTime;
	((unsigned long*)&ull)[1] = ft.dwHighDateTime;
	return ull;
}

wxString FormatLocalFileTime(size_t t)
{
	FILETIME ft;
	ft.dwLowDateTime = ((unsigned long*)&t)[0];
	ft.dwHighDateTime = ((unsigned long*)&t)[1];
	FILETIME lft;
	FileTimeToLocalFileTime(&ft, &lft);
	SYSTEMTIME st;
	FileTimeToSystemTime(&lft, &st);
	return wxString::Format(wxT("%d-%d-%d %02d:%02d:%02d.%03d"), st.wDay, st.wMonth, st.wYear, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

wxString LogFormatLocalFileTimeUs(size_t t)
{
	FILETIME ft;
	ft.dwLowDateTime = ((unsigned long*)&t)[0];
	ft.dwHighDateTime = ((unsigned long*)&t)[1];
	FILETIME lft;
	FileTimeToLocalFileTime(&ft, &lft);
	SYSTEMTIME st;
	FileTimeToSystemTime(&lft, &st);
	st.wMilliseconds = 0;
	FILETIME ftSeconds;
	SystemTimeToFileTime(&st, &ftSeconds);
	size_t i64Ft;
	((unsigned long*)&i64Ft)[0] = lft.dwLowDateTime;
	((unsigned long*)&i64Ft)[1] = lft.dwHighDateTime;
	size_t i64FtSeconds;
	((unsigned long*)&i64FtSeconds)[0] = ftSeconds.dwLowDateTime;
	((unsigned long*)&i64FtSeconds)[1] = ftSeconds.dwHighDateTime;
	size_t us = (i64Ft - i64FtSeconds) / 10ULL;
	return wxString::Format(wxT("%04d-%02d-%02d %02d:%02d:%02d.%06lld"), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, us);
}

class panelScope : public formScope, public nkDigTimPanel
{
public:
	panelScope(frameMain* main, wxString file, EMODE mode)
	: formScope(main->m_mainPanel)
	, m_main(main)
	, m_thread(NULL)
	, m_adcResolution(0)
	, m_periodIndex(9) // 1 second
	, m_save(false)
	, m_format(FORMAT_BINARY)
	, m_mode(mode) 
	, m_lastsample(0)
	, m_data(INVALID_HANDLE_VALUE)
	, m_isTempData(true)
	{
		m_data_name[0] = 0;
		if (m_mode == MODE_VIEW)
		{
			m_tool->EnableTool(ID_TOOLON, false);
			m_tool->EnableTool(ID_TOOLADCRES, false);
		}
		if (m_mode == MODE_RECORD) m_save = true;
		OpenDataFile(file);

		if (m_main->m_pins.items.size() && (m_main->m_pins.items[0].values.size() > 1))
		{
			m_tool->SetToolLabel(ID_TOOLCHANNEL, m_main->m_pins.items[0].values[1]);
		}

		m_profilePrefix.Format(wxT("%s/Scope/"), m_main->m_portName);
		if (m_mode == MODE_RECORD)
		{
			m_main->m_profile->Read(m_profilePrefix + wxT("AdcRes"), &m_adcResolution);
			m_tool->SetToolLabel(ID_TOOLADCRES, wxString::Format(wxT("%ld-bit"), m_adcResolution ? m_main->m_adc_res : 8));
		}
		m_main->m_profile->Read(m_profilePrefix + wxT("Period"),&m_periodIndex);
		m_graph->SetPeriod(GetPeriod(m_periodIndex), false);
		m_tool->SetToolLabel(ID_TOOLPERIOD, FormatPeriod(m_graph->m_period));
		m_main->m_profile->Read(m_profilePrefix + wxT("Trigger"), (long*) & m_graph->m_triggerOn);
		m_tool->ToggleTool(ID_TOOLTRIGGER, m_graph->m_triggerOn);
		long channel = 0;
		m_main->m_profile->Read(m_profilePrefix + wxT("Channel"), &channel);
		if (channel < m_main->m_pins.items.size())
		{
			m_graph->SetTriggerChannel(channel);
			if (m_main->m_pins.items[channel].values.size() > 1)
			{
				m_tool->SetToolLabel(ID_TOOLCHANNEL, m_main->m_pins.items[channel].values[1]);
			}
		}
		m_main->m_profile->Read(m_profilePrefix + wxT("Polarity"), (long*)&m_graph->m_triggerPolarity);
		m_tool->SetToolBitmap(ID_TOOLPOLARITY, wxBitmap(m_graph->m_triggerPolarity ? wxT("res/TriggerUp.png"): wxT("res/TriggerDown.png"), wxBITMAP_TYPE_PNG));
		m_tool->SetToolShortHelp(ID_TOOLPOLARITY, m_graph->m_triggerPolarity ? wxT("trigger up"): wxT("trigger down"));
		m_main->m_profile->Read(m_profilePrefix + wxT("Mode"), (long*)&m_graph->m_triggerMode);
		m_tool->SetToolLabel(ID_TOOLMODE, m_graph->m_triggerMode == 0 ? wxT("Auto") : m_graph->m_triggerMode == 1 ? wxT("Normal"): wxT("Single"));

		m_main->m_profile->Read(wxT("Format"), (long*)&m_format);

		m_tool->Realize();

		switch (m_mode)
		{
		case MODE_VIEW:
			if (m_adcResolution) ZoomAll<word>();
			else ZoomAll<byte>();
			break;
		case MODE_RECORD:
			m_tool->ToggleTool(ID_TOOLSAVE, true);
			StartRecording(true);
			break;
		default:
			m_mode = MODE_RECORD;
			break;
		}
	}

	~panelScope()
	{
		CloseDataFile();
	}

	void OpenDataFile(wxString file)
	{
		if (m_data != INVALID_HANDLE_VALUE)
		{
			CloseDataFile();
		}
		if (m_mode != MODE_VIEW)
		{
			if(!m_save || !file.length())
			{ 
				GetTempPath(_MAX_FNAME, m_data_name);
				GetTempFileName(m_data_name, L"nkbef", 0, m_data_name);
				m_isTempData = true;
			}
			else
			{
				m_isTempData = false;
				if (file.Right(6).Lower().CompareTo(L".nkbef")) file += L".nkbef";
				while(wxFileExists(file) && wxFileName(file).GetSize().GetValue())
				{
					IncrementFileName(file);
				}
				wcscpy_s(m_data_name, countof(m_data_name), file);
			}
			m_data = CreateFile(m_data_name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
			m_lastsample = 0;
			m_main->m_pins.dataBits = m_adcResolution ? m_main->m_adc_res : 8;
			if (!m_isTempData)
			{
				m_filename = m_data_name;
			}
		}
		else
		{
			wcscpy_s(m_data_name, _MAX_FNAME, file);
			// if the file is blocked for writing, this is a recording in progress, so enable the timer to check for new data
			m_data = CreateFile(m_data_name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			m_tool->ToggleTool(ID_TOOLON, m_data == INVALID_HANDLE_VALUE);
			CloseHandle(m_data);
			m_data = CreateFile(m_data_name, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			m_main->m_pins.LoadFromFile(m_data, m_lastsample);
			m_adcResolution = m_main->m_pins.dataBits != 8;
			m_tool->SetToolLabel(ID_TOOLADCRES, wxString::Format(wxT("%ld-bit"), m_main->m_pins.dataBits));
		}
		if (m_data == INVALID_HANDLE_VALUE)
		{
			wxMessageBox(wxString::Format(wxT("Error %s opening file %s"), wxSysErrorMsg(), m_data_name),wxMessageBoxCaptionStr, wxICON_ERROR | wxOK);
		}
		m_graph->Init(m_main,m_data,m_lastsample, m_main->m_pins.dataBits);
	}

	void CloseDataFile()
	{
		CloseHandle(m_data);
		m_data = INVALID_HANDLE_VALUE;
		if ((m_mode == MODE_RECORD) && m_isTempData)
		{
			DeleteFile(m_data_name);
		}
	}

	bool CanClosePanel(wxFrame * mainFrame, bool allow_veto)
	{
		// if the window is miminized, assume closure is initiated by windows shut down or deliberite termination of the application.
		if (allow_veto && m_thread && !mainFrame->IsIconized())
		{
			int answer = wxMessageBox(wxT("Data capture is active: do you want to stop this ?"), wxMessageBoxCaptionStr, wxOK | wxCANCEL);
			if (answer == wxCANCEL)
			{
				return false;
			}
		}
		StartRecording(false);
		return true;
	}

	void SetPeriod(long index, bool keep_center);
	void SetAdcResolution(long index);

	virtual void m_toolOnOnToolClicked(wxCommandEvent& event)
	{
		event.Skip();
		StartRecording(event.IsChecked());
	}

	void StartRecording(bool record)
	{
		if (record)
		{
			m_graph->Reset();
			size_t pos = 0;
			OpenDataFile(m_save && (m_format == FORMAT_BINARY) ? m_filename  : wxString(""));
			SetFilePointerEx(m_data, *(LARGE_INTEGER*)&pos, NULL, FILE_BEGIN);
			SetEndOfFile(m_data);
		}
		StartThread(record);
		m_graph->Start(record);
		m_tool->ToggleTool(ID_TOOLON, record);
		m_tool->Refresh();
		if (record)
		{
			if (!m_isTempData)
			{
				SetStatus(wxString("recording ") + m_filename);
			}
			else
			{
				SetStatus(wxT("recording"));
			}
		}
		else
		{
			SetStatus(wxString(""));
			if (m_save && m_filename.length())
			{
				if (wcscmp(m_data_name, m_filename))
				{
					if(m_adcResolution) SaveFile<word>();
					else SaveFile<byte>();
				}
				else
				{
					SaveFileInfo(m_data, &m_lastsample);
					m_graph->SetLastSample(m_lastsample);
				}
			}
			m_main->SetStatus(wxT("scope mode stopped"));
		}
	}

	virtual void m_toolTriggerOnToolClicked(wxCommandEvent& event) 
	{ 
		event.Skip(); 
		m_graph->SetTriggerOn(event.IsChecked());
		m_main->m_profile->Write(m_profilePrefix + wxT("Trigger"), m_graph->m_triggerOn);
	}

	virtual void m_toolArmOnToolClicked(wxCommandEvent& event) 
	{ 
		event.Skip(); 
		m_graph->SetArm(event.IsChecked());
		if (event.IsChecked() && (m_graph->m_triggerMode == NkDigTimerGraph::modeSingle))
		{
			m_graph->SetTriggerMode(NkDigTimerGraph::modeSingle);
		}
	}
	
	virtual void m_toolLeftLeftOnToolClicked(wxCommandEvent& event) 
	{ 
		event.Skip(); 
		m_graph->Move(-1);
		m_tool->ToggleTool(ID_TOOLARM, false);
		m_tool->Refresh();
	}

	virtual void m_toolLeftOnToolClicked(wxCommandEvent& event) 
	{ 
		event.Skip(); 
		m_graph->Move(-0.2);
		m_tool->ToggleTool(ID_TOOLARM, false);
		m_tool->Refresh();
	}

	virtual void m_toolLargerOnToolClicked(wxCommandEvent& event)
	{ 
		event.Skip(); 
		SetPeriod(m_periodIndex - 1, true);
		m_tool->ToggleTool(ID_TOOLARM, false);
		m_tool->Refresh();
	}

	virtual void m_toolSmallerOnToolClicked(wxCommandEvent& event) 
	{ 
		event.Skip(); 
		SetPeriod(m_periodIndex + 1, true);
		m_tool->ToggleTool(ID_TOOLARM, false);
		m_tool->Refresh();
	}

	virtual void m_toolZoomAllOnToolClicked(wxCommandEvent& event) 
	{ 
		event.Skip();
		if(m_adcResolution) ZoomAll<word>();
		else ZoomAll<byte>();
	}

	template <typename T>
	void ZoomAll()
	{
		// read the first and the last timestamp in the file.
		// to prevent interfering with the recording, duplicate the handle
		size_t size = 0;
		if (m_lastsample)
		{
			size = m_lastsample;
		}
		else
		{
			GetFileSizeEx(m_data, (LARGE_INTEGER*)&size);
		}
		size_t count = size / sizeof(fileSample<T>);
		if (count < 1) return;
		HANDLE h = INVALID_HANDLE_VALUE;
		DuplicateHandle(GetCurrentProcess(), m_data, GetCurrentProcess(), &h, GENERIC_READ, FALSE, 0);
		if (h == INVALID_HANDLE_VALUE) return;
		fileSample<T> first = { 0 }, last = { 0 };
		DWORD read;
		while(1)
		{ 
			size_t pos = 0;
			SetFilePointerEx(h, *(LARGE_INTEGER*)&pos, NULL, FILE_BEGIN);
			if (!ReadFile(h, &first, sizeof(first), &read, NULL) || (read != sizeof(first))) break;
			if (count > 1)
			{
				pos = (count - 1) * sizeof(fileSample<T>);
				SetFilePointerEx(h, *(LARGE_INTEGER*)&pos, NULL, FILE_BEGIN);
				if (!ReadFile(h, &last, sizeof(last), &read, NULL) || (read != sizeof(first))) break;
			}
			size_t period = (last.timestamp - first.timestamp);
			m_periodIndex = FindPeriod(period);
			STimeRes* t = timeRes + m_periodIndex;
			m_tool->SetToolLabel(ID_TOOLPERIOD, t->label);
			m_graph->SetRange(first.timestamp, first.timestamp + t->period);
			m_tool->ToggleTool(ID_TOOLARM, !m_graph->m_frozen);
			m_tool->Realize();
			break;
		}

		CloseHandle(h);
	}

	virtual void m_toolRightOnToolClicked(wxCommandEvent& event) 
	{ 
		event.Skip(); 
		m_graph->Move(+0.2);
		m_tool->ToggleTool(ID_TOOLARM, false);
		m_tool->Refresh();
	}
	
	virtual void m_toolRightRightOnToolClicked(wxCommandEvent& event) 
	{ 
		event.Skip(); 
		m_graph->Move(+1);
		m_tool->ToggleTool(ID_TOOLARM, false);
		m_tool->Refresh();
	}

	virtual void m_toolSaveOnToolClicked(wxCommandEvent& event)
	{
		event.Skip();

		bool checked = event.IsChecked();
		if (!checked)
		{
			m_save = false;
			return;
		}

		wxFileName fn(m_filename);
		while(fn.Exists() && fn.GetSize().GetValue())
		{
			IncrementFileName(m_filename);
			fn = wxFileName(m_filename);
		}

		wxFileDialog dlg(this, wxT("Save NiVerDig Digital Timer Events Recording"), fn.GetPath(), fn.GetFullName(),
			"Binary Event Format (*.nkbef)|*.nkbef|Text Event Format (*.nktef)|*.nktef|Relative Timing Text Event Format (*.nkref)|*.nkref", wxFD_SAVE /*| wxFD_OVERWRITE_PROMPT*/);
		dlg.SetFilterIndex(m_format);
		while (1)
		{
			if (dlg.ShowModal() == wxID_CANCEL)
			{
				m_tool->ToggleTool(ID_TOOLSAVE, false);
				m_save = false;
				return;
			}
			// wxFileDialog adds the extension AFTER the checking if the file exists ...
			wxString name = dlg.GetPath();
			fn = wxFileName(name);
			if(fn.Exists() && fn.GetSize().GetValue())
			{
				int answer = wxMessageBox(wxString::Format(wxT("%s exists: overwrite ?"), name), wxMessageBoxCaptionStr, wxYES | wxNO | wxCANCEL);
				if (answer == wxCANCEL)
				{
					m_tool->ToggleTool(ID_TOOLSAVE, false);
					m_save = false;
					return;
				}
				if (answer == wxNO)
				{
					continue;
				}
			}
			break;
		}

		m_filename = dlg.GetPath();
		m_format = (eFormat) dlg.GetFilterIndex();
		m_main->m_profile->Write(wxT("Format"), (long)m_format);
		m_save = true;

		if (!m_thread)
		{
			if (wxGetFileLength(m_data))
			{
				if (m_adcResolution) SaveFile<word>();
				else SaveFile<byte>();
			}
		}
		else
		{
			wxMessageBox(wxT("The recordings will be saved when recording is stopped"));
		}
	}

	virtual void m_toolOpenOnToolClicked(wxCommandEvent& event) 
	{ 
		event.Skip(); 
		wxFileDialog dlg(this, wxT("Open NiVerDig Digital Timer Events Recording"), wxEmptyString, wxEmptyString,
			"NiVerDig Digital Events Recording Files (*.nkbef)|*.nkbef", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
		if (dlg.ShowModal() == wxID_CANCEL)
		{
			return;
		}

		if (m_mode == MODE_RECORD)
		{
			wxExecute(wxString::Format(wxT("\"%s\" \"%s\""), ::wxStandardPaths::Get().GetExecutablePath(), dlg.GetPath()));
		}
		else
		{
			OpenDataFile(dlg.GetPath());
			if (m_adcResolution) ZoomAll<word>();
			else ZoomAll<byte>();
		}
	}

	void StartThread(bool on)
	{
		m_tool->ToggleTool(ID_TOOLARM, on);
		if (!on)
		{
			if (!m_thread) return;
			NkComPort_WriteA(m_main->m_port,"s",1);
			for (int i = 0; m_thread->IsRunning() && (i < 300); ++i)
			{
				Sleep(1);
			}
			m_thread->m_stop = true;
			m_thread->Wait();
			delete m_thread;
			m_thread = NULL;

			return;
		}
		else
		{
			if (m_thread) return;
			if (m_adcResolution) m_thread = new threadScope<word>(this);
			else m_thread = new threadScope<byte>(this);
			m_thread->Create();
			m_thread->SetPriority(100);
			if(m_adcResolution) m_main->WriteLine(wxT("scope 16\n"));
			else m_main->WriteLine(wxT("scope\n"));
			wchar_t answer[64];
			m_main->ReadLine(answer, 64, 100);
			m_thread->Run();
		}
	}

	template <typename T>
	void SaveFile()
	{
		if (m_format == FORMAT_BINARY)
		{
			CopyFile(m_data_name, m_filename, FALSE);
			// add the pin information
			HANDLE h = CreateFile(m_filename, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
			if (h != INVALID_HANDLE_VALUE)
			{
				SaveFileInfo(h,NULL);
				CloseHandle(h);
			}
		}
		else
		{
			std::vector<SItem>& pins = m_main->m_pins.items;
			HANDLE h1 = INVALID_HANDLE_VALUE;
			DuplicateHandle(GetCurrentProcess(), m_data, GetCurrentProcess(), &h1, GENERIC_READ, FALSE, 0);
			HANDLE h2 = CreateFile(m_filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
			if ((h1 != INVALID_HANDLE_VALUE) && (h2 != INVALID_HANDLE_VALUE))
			{
				for (size_t i = 0; i < pins.size(); ++i)
				{
					if (pins[i].values.size() > 1)
					{
						wxString line = wxString::Format("pin\t%lld\t%S\n", i, pins[i].values[1]);
						WriteFile(h2, (const char*)line, line.length() * sizeof(char), NULL, NULL);
					}
				}
				wxString header = "time\tpin\tstate\n";
				WriteFile(h2, (const char*)header, header.length() * sizeof(char), NULL, NULL);
				SetFilePointer(h1, 0, NULL, FILE_BEGIN);
				fileSample<T> sample;
				DWORD read = 0;
				wxString line;
				if (ReadFile(h1, &sample, sizeof(sample), &read, NULL) && (read == sizeof(sample)))
				{
					size_t start = sample.timestamp;
					do
					{
						size_t channel = sample.channel;
						if(channel < pins.size())
						{
							if (m_format == FORMAT_TEXT_ABS)
							{
								line = wxString::Format("%ls\t%lld\t%hu\n", LogFormatLocalFileTimeUs(sample.timestamp), channel, sample.state);
							}
							else
							{
								line = wxString::Format("%lld\t%lld\t%hu\n", (sample.timestamp - start) / 10ULL, channel, sample.state);
							}
							WriteFile(h2, (const char*)line, line.length() * sizeof(char), NULL, NULL);
						}
					} while (ReadFile(h1, &sample, sizeof(sample), &read, NULL) && (read == sizeof(sample)));
				}
			}
			if (h1 != INVALID_HANDLE_VALUE) CloseHandle(h1);
			if (h2 != INVALID_HANDLE_VALUE) CloseHandle(h2);
			if ((h1 == INVALID_HANDLE_VALUE) || (h2 == INVALID_HANDLE_VALUE))
			{
				m_main->SetStatus(wxString::Format(wxT("error saving file %s."), m_filename));
				return;

			}
		}
		m_main->SetStatus(wxString::Format(wxT("file %s saved."),m_filename));
		IncrementFileName(m_filename);
	}

	void SaveFileInfo(HANDLE h, size_t * last_sample)
	{
		SetFilePointer(h, 0, NULL, FILE_END);
		size_t len = GetFilePointerEx(h);
		if (last_sample) *last_sample = len;
		std::vector<SItem>& pins = m_main->m_pins.items;
		for (size_t i = 0; i < pins.size(); ++i)
		{
			SItem& pin = pins[i];
			if (pin.values.size() > 1) // values[1] is the name
			{
				wxString line = wxString::Format(wxT("pin\t%lld\t%s\t%lld\n"), i, pin.values[1], pin.type);
				WriteFile(h, (const wchar_t*)line, line.length() * sizeof(wchar_t), NULL, NULL);
			}
		}
		wxString line = wxString::Format(wxT("bits\t%ld\n"), m_main->m_pins.dataBits);
		WriteFile(h, (const wchar_t*)line, line.length() * sizeof(wchar_t), NULL, NULL);
		// last number is the start of the pins section
		WriteFile(h, &len, sizeof(size_t), NULL, NULL);
	}
	
	void IncrementFileName(wxString &filename)
	{
		wxFileName fn(filename);
		wxString name = fn.GetName();
		wxRegEx re(wxT("(.*?)(\\d+)$"));
		long index = 0;
		if (re.Matches(name))
		{
			re.GetMatch(name, 2).ToLong(&index);
			++index;
			name = re.GetMatch(name, 1) + wxString::Format(wxT("%ld"), index);
		}
		else
		{
			name = name + wxT("1");
		}
		fn.SetName(name);
		filename = fn.GetFullPath();
	}

	void OnGraphDisarm(NkDigTimerGraphEvent& graph)
	{
		m_tool->ToggleTool(ID_TOOLARM, false);
		m_tool->Refresh();
	}

	void SetStatus(wxString status)
	{
		m_scopeStatus->SetLabel(status);
	}

	frameMain*   m_main;
	HANDLE       m_data;
	wchar_t      m_data_name[_MAX_FNAME];
	bool         m_isTempData;
	threadScopeBase* m_thread;
	long         m_adcResolution;
	long         m_periodIndex;
	wxString     m_profilePrefix;
	bool         m_save;
	wxString     m_filename;
	enum eFormat {FORMAT_BINARY, FORMAT_TEXT_ABS, FORMAT_TEXT_REL};
	eFormat      m_format;
	enum EMODE   m_mode;
	size_t       m_lastsample; // used in viewer mode: offset of last sample

	wxDECLARE_EVENT_TABLE();
	void OnDropDownToolbarAdcRes(wxAuiToolBarEvent& evt);
	void OnAdcResolution(wxCommandEvent& event);
	void OnDropDownToolbarPeriod(wxAuiToolBarEvent& evt);
	void OnPeriod(wxCommandEvent& event);
	void OnDropDownToolbarPolarity(wxAuiToolBarEvent& evt);
	void OnPolarity(wxCommandEvent& event);
	void OnDropDownToolbarChannel(wxAuiToolBarEvent& evt);
	void OnChannel(wxCommandEvent& event);
	void OnDropDownToolbarMode(wxAuiToolBarEvent& evt);
	void OnMode(wxCommandEvent& event);
};

wxBEGIN_EVENT_TABLE(panelScope, formScope)
EVT_AUITOOLBAR_TOOL_DROPDOWN(ID_TOOLADCRES, panelScope::OnDropDownToolbarAdcRes)
EVT_MENU_RANGE(20006, 20007, panelScope::OnAdcResolution)
EVT_AUITOOLBAR_TOOL_DROPDOWN(ID_TOOLPERIOD, panelScope::OnDropDownToolbarPeriod)
EVT_MENU_RANGE(10000, 10026, panelScope::OnPeriod)
EVT_AUITOOLBAR_TOOL_DROPDOWN(ID_TOOLPOLARITY, panelScope::OnDropDownToolbarPolarity)
EVT_MENU_RANGE(20000, 20001, panelScope::OnPolarity)
EVT_AUITOOLBAR_TOOL_DROPDOWN(ID_TOOLCHANNEL, panelScope::OnDropDownToolbarChannel)
EVT_MENU_RANGE(30000, 30100, panelScope::OnChannel)
EVT_AUITOOLBAR_TOOL_DROPDOWN(ID_TOOLMODE, panelScope::OnDropDownToolbarMode)
EVT_MENU_RANGE(20003, 20005, panelScope::OnMode)
EVT_NKDIGTIMERGRAPHEVENT(wxID_ANY, panelScope::OnGraphDisarm)
wxEND_EVENT_TABLE()

template <typename T>
class fileSampleFifo : public std::vector<fileSample<T> >
{
public:
	typedef std::vector<fileSample<T> > base;
	typedef base::iterator iterator;
	typedef base::reverse_iterator riterator;

	fileSampleFifo(HANDLE file, size_t delay)
		: m_file(file)
		, m_delay(delay)
		, m_current(0)
	{
		base::resize(2048);
		m_first = base::end();
		m_last = base::end();
		m_count = 0;
	}

	void push(char channel, T state, size_t timestamp)
	{
#ifdef _DEBUG
//		OutputDebugString(wxString::Format(wxT("f %2d s %2u t %10lld\n"), channel, state, timestamp));
#endif
		// make room if full: write writes at least one
		if ((m_count + 1) == base::size())
		{
			write(1);
		}

		// position of new sample
		iterator new_last;
		if (m_first == base::end())
		{
			new_last = m_first = base::begin();
		}
		else
		{
			new_last = last() + 1;
			if (new_last == base::end()) new_last = base::begin();
		}

		// check out of order
		iterator s = new_last;
		while(1)
		{
			iterator p = s;
			prev(p);
			if (p == base::end()) break;
			if (p->timestamp <= timestamp) break;
			*s = *p; 
			s = p;
		}
		s->channel = channel;
		s->state = state;
		s->timestamp = timestamp;
		if (timestamp > m_current)
		{
			m_current = timestamp;
		}
		m_last = new_last;
		++m_count;
	}

	iterator first()
	{
		return m_first;
	}

	iterator last()
	{
		return m_last;
	}

	void next(iterator& i)
	{
		if (i == m_last)
		{
			i = end<T>();
		}
		else
		{
			++i;
			if (i == end<T>())
			{
				i = begin<T>();
			}
		}
	}

	void prev(iterator& i)
	{
		if (i == m_first)
		{
			i = base::end();
		}
		else
		{
			if (i == base::begin())
			{
				i = base::end();
			}
			--i;
		}
	}

	void write(size_t write_at_least_one)
	{
		DWORD written;
		if (!m_count) return;
		// write all samples older than delay
		iterator i1 = first();
		iterator i2 = i1 + write_at_least_one; // always write one to make room
		bool found_recent = false;
		if (i1 != m_last)
		{
			for (; (i2 != base::end()) && (i2 != m_last); ++i2)
			{
				if ((m_current - i2->timestamp) < m_delay)
				{
					found_recent = true;
					break;
				}
			}
		}
		size_t count = i2 - i1;
		if (count)
		{
			WriteFile(m_file, &*i1, sizeof(fileSample<T>) * count, &written, NULL);
			//OutputDebugString(wxString::Format(wxT("written %ld\n"), written));
			m_count -= count;
			m_first = i2;
			if (m_first == base::end()) m_first = base::begin();
		}
		if (m_count && !found_recent && (m_first > m_last))
		{
			// check the begin of the vector for older items
			iterator i1 = base::begin();
			iterator i2 = i1;
			for (; i2 != m_last; ++i2)
			{
				if ((m_current - i2->timestamp) < m_delay)
				{
					break;
				}
			}
			count = i2 - i1;
			if (count)
			{
				WriteFile(m_file, &*i1, sizeof(fileSample<T>) * count, &written, NULL);
				//OutputDebugString(wxString::Format(wxT("written %ld\n"), written));
				m_count -= count;
				m_first = i2;
				if (m_first == base::end()) m_first = base::begin();
			}
		}
	}

	size_t    m_count;
	iterator  m_first;
	iterator  m_last;
	HANDLE    m_file;
	size_t    m_delay;
	size_t    m_current;
};

template <typename T>
void* threadScope<T>::Entry()
{
	NKCOMPORT* port = m_scope->m_main->m_port;

	std::vector<T> states;
	states.resize(m_scope->m_main->m_pins.items.size());
	for (auto& s : states) { s = -1; }

	char serialData[sizeof(serialSample<T>) * 1024];
	serialSample<T>* serSamples = (struct serialSample<T>*)serialData;
	fileSampleFifo<T> fileData(m_scope->m_data,10000);

	size_t offset = 0;

	long read = NkComPort_ReadA(port, serialData, sizeof(serialSample<T>), 400);
	if ((read != sizeof(serialSample<T>)) || (serSamples[0].channel != SYNC_CHANNEL) || (serSamples[0].state != SYNC_TICK))
	{
		// to do warn scope that something went wrong
		return NULL;
	}
	size_t start = GetCurrentFileTime();
	unsigned long last_tick = serSamples[0].tick;
	m_time = start - serSamples[0].tick * 10ULL;

	while (!m_stop)
	{
		long read = NkComPort_ReadA(port, serialData + offset, sizeof(serialData) - offset, 0);
		if (read <= 0) continue;
		read += offset;
#ifdef _DEBUG
/*
		OutputDebugString(wxString::Format(wxT("read %ld\n"), read));
		BYTE* p = (BYTE*)serialData;
 		for(size_t i = 0; i < read; ++i)
	    {
			OutputDebugString(wxString::Format(wxT("%02hx "), p[i] ));
		}
		OutputDebugString(wxT("\n"));
*/
#endif
		long count = read / sizeof(serialSample<T>);
		if (!count) continue;
		serialSample<T>* sentinel = serSamples + count;
		for (serialSample<T>* s = serSamples; s < sentinel; ++s)
		{
#ifdef _DEBUG
//			OutputDebugString(wxString::Format(wxT("s %2d s %4X t %10ld\n"), s->channel, s->state, s->tick));
#endif
			if (s->tick < last_tick)
			{
				// 16-bits timer overflow (happens every 8 seconds)
				// the change notifications might not come in order ...
				// the idle updates are send every 100 ms (100000 us)
				// if the difference is larger than 4s, it was really from the previous round
				unsigned long period = last_tick - s->tick;
				if (period > 0x80000000)
				{
					m_time += 0x100000000 * 10ULL;
#ifdef _DEBUG
					OutputDebugString(wxString::Format(wxT("overflow %lx %lx\n"), last_tick, s->tick));
#endif
				}
				else
				{
#ifdef _DEBUG
					OutputDebugString(wxString::Format(wxT("sample out of order %lx %lx\n"), last_tick, s->tick));
#endif
				}
			}
			else
			{
				// s->tick > last_tick
				unsigned long period = s->tick - last_tick;
				if (period > 0x80000000)
				{
					// jitter back before the overflow ...
					m_time -= 0x100000000 * 10ULL;
#ifdef _DEBUG
					OutputDebugString(wxString::Format(wxT("sample out of order %lx %lx\n"), last_tick, s->tick));
#endif
				}
			}
			last_tick = s->tick;
			if (s->channel < 0)
			{
				// special command ...
				if (s->state == SYNC_TICK)
				{
					// tick to make the graph move: insert state for each pin
					for (size_t i = 0; i < states.size(); ++i)
					{
						fileData.push(~i, states[i], m_time + s->tick * 10ULL); // use bitwise not channel to indicate this is a tick event
					}
					fileData.write(0);
					continue;
				}
				if (s->state == SYNC_END)
				{
					// end of scope mode
					fileData.m_delay = 0;
					fileData.write(0);
					return NULL;
				}
			}
			if(s->channel < states.size())
			{
				states[s->channel] = s->state;
			}
			fileData.push(s->channel, s->state, m_time + s->tick * 10ULL);
		}
		long remain = read % sizeof(serialSample<T>);
		if (remain)
		{
			memmove(serialData, sentinel, remain);
		}
		offset = remain;
	}

	return NULL;
}

wxString FormatPeriod(size_t period)
{
	for (size_t i = 0; i < countof(timeRes); ++i)
	{
		if (timeRes[i].period >= period)
		{
			return timeRes[i].label;
		}
	}
	return wxEmptyString;
}

size_t FindPeriod(size_t period)
{
	for (size_t i = 0; i < countof(timeRes); ++i)
	{
		if (timeRes[i].period >= period)
		{
			return i;
		}
	}
	return countof(timeRes) - 1;
}

size_t GetPeriod(long index)
{
	if ((index >= 0) && (index < countof(timeRes)))
	{
		return timeRes[index].period;
	}
	return US_PER_SECOND;
}

void panelScope::OnDropDownToolbarAdcRes(wxAuiToolBarEvent& evt)
{
	wxAuiToolBar* tb = static_cast<wxAuiToolBar*>(evt.GetEventObject());

	tb->SetToolSticky(evt.GetId(), true);

	// create the popup menu
	wxMenu menuPopup;

	menuPopup.Append(new wxMenuItem(&menuPopup, 20006, wxT("8-bit")));
	if (m_main->m_adc_res != 8)
	{
		menuPopup.Append(new wxMenuItem(&menuPopup, 20007, wxString::Format(wxT("%ld-bit"), m_main->m_adc_res)));
	}

	// line up our menu with the button
	wxRect rect = tb->GetToolRect(evt.GetId());
	wxPoint pt = tb->ClientToScreen(rect.GetBottomLeft());
	pt = ScreenToClient(pt);

	PopupMenu(&menuPopup, pt);

	// make sure the button is "un-stuck"
	tb->SetToolSticky(evt.GetId(), false);
}

void panelScope::OnAdcResolution(wxCommandEvent& event)
{
	long index = event.GetId() - 20006;
	if ((index < 0) || (index > 1)) return;
	SetAdcResolution(index);
}

void panelScope::SetAdcResolution(long index)
{
	m_adcResolution = index;
	m_tool->SetToolLabel(ID_TOOLADCRES, wxString::Format(wxT("%ld-bit"), index ? m_main->m_adc_res : 8));
	m_main->m_profile->Write(m_profilePrefix + wxT("AdcRes"), index);
	if (m_thread)
	{
		StartRecording(false);
		StartRecording(true);
	}
}

void panelScope::OnDropDownToolbarPeriod(wxAuiToolBarEvent& evt)
{
	wxAuiToolBar* tb = static_cast<wxAuiToolBar*>(evt.GetEventObject());

	tb->SetToolSticky(evt.GetId(), true);

	// create the popup menu
	wxMenu menuPopup;

	for (size_t i = 0; i < countof(timeRes); ++i)
	{
		STimeRes* t = timeRes + i;
		menuPopup.Append(new wxMenuItem(&menuPopup, t->id, t->label));
	}

	// line up our menu with the button
	wxRect rect = tb->GetToolRect(evt.GetId());
	wxPoint pt = tb->ClientToScreen(rect.GetBottomLeft());
	pt = ScreenToClient(pt);

	PopupMenu(&menuPopup, pt);

	// make sure the button is "un-stuck"
	tb->SetToolSticky(evt.GetId(), false);
}

void panelScope::OnPeriod(wxCommandEvent& event)
{
	long index = event.GetId() - 10000;
	if ((index < 0) || (index >= countof(timeRes))) return;
	SetPeriod(index, m_mode == MODE_VIEW);
}

void panelScope::SetPeriod(long index, bool keep_center)
{
	if (index < 0) index = 0;
	if (index >= countof(timeRes)) index = countof(timeRes) - 1;
	m_periodIndex = index;
	STimeRes* t = timeRes + index;
	m_tool->SetToolLabel(ID_TOOLPERIOD, t->label);
	m_graph->SetPeriod(t->period,keep_center);
	m_main->m_profile->Write(m_profilePrefix + wxT("Period"), index);
	m_tool->Realize();
}

void panelScope::OnDropDownToolbarPolarity(wxAuiToolBarEvent& evt)
{
	wxAuiToolBar* tb = static_cast<wxAuiToolBar*>(evt.GetEventObject());

	tb->SetToolSticky(evt.GetId(), true);

	// create the popup menu
	wxMenu menuPopup;

	wxMenuItem* mi = menuPopup.Append(new wxMenuItem(&menuPopup, 20000, wxT("down")));
	mi->SetBitmap(wxBitmap(wxT("res/TriggerDown.png"), wxBITMAP_TYPE_PNG));
	mi = menuPopup.Append(new wxMenuItem(&menuPopup, 20001, wxT("up")));
	mi->SetBitmap(wxBitmap(wxT("res/TriggerUp.png"), wxBITMAP_TYPE_PNG));

	// line up our menu with the button
	wxRect rect = tb->GetToolRect(evt.GetId());
	wxPoint pt = tb->ClientToScreen(rect.GetBottomLeft());
	pt = ScreenToClient(pt);

	PopupMenu(&menuPopup, pt);

	// make sure the button is "un-stuck"
	tb->SetToolSticky(evt.GetId(), false);
}

void panelScope::OnPolarity(wxCommandEvent& event)
{
	NkDigTimerGraph::ETriggerPolarity polarity = NkDigTimerGraph::ETriggerPolarity(event.GetId() - 20000);

	switch (polarity)
	{
	case NkDigTimerGraph::triggerDown:
		m_tool->SetToolBitmap(ID_TOOLPOLARITY, wxBitmap(wxT("res/TriggerDown.png"), wxBITMAP_TYPE_PNG));
		m_tool->SetToolShortHelp(ID_TOOLPOLARITY, wxT("trigger down"));
		break;
	case NkDigTimerGraph::triggerUp:
		m_tool->SetToolBitmap(ID_TOOLPOLARITY, wxBitmap(wxT("res/TriggerUp.png"), wxBITMAP_TYPE_PNG));
		m_tool->SetToolShortHelp(ID_TOOLPOLARITY, wxT("trigger up"));
		break;
	default: return;
	}
	m_graph->SetTriggerPolarity(polarity);
	m_main->m_profile->Write(m_profilePrefix + wxT("Polarity"), (long)polarity);
}

void panelScope::OnDropDownToolbarChannel(wxAuiToolBarEvent& evt)
{
	wxAuiToolBar* tb = static_cast<wxAuiToolBar*>(evt.GetEventObject());

	tb->SetToolSticky(evt.GetId(), true);

	// create the popup menu
	wxMenu menuPopup;

	std::vector<SItem>& items = m_main->m_pins.items;
	for (size_t i = 0; i < items.size(); ++i)
	{
		if (items[i].values.size() > 1)
		{
			menuPopup.Append(new wxMenuItem(&menuPopup, 30000 + i, items[i].values[1]));
		}
	}

	// line up our menu with the button
	wxRect rect = tb->GetToolRect(evt.GetId());
	wxPoint pt = tb->ClientToScreen(rect.GetBottomLeft());
	pt = ScreenToClient(pt);

	PopupMenu(&menuPopup, pt);

	// make sure the button is "un-stuck"
	tb->SetToolSticky(evt.GetId(), false);
}

void panelScope::OnChannel(wxCommandEvent& event)
{
	size_t channel = event.GetId() - 30000;
	if (channel >= m_main->m_pins.items.size()) return;

	m_tool->SetToolLabel(ID_TOOLCHANNEL,m_main->m_pins.items[channel].values[1]);
	m_tool->Realize();
	m_graph->SetTriggerChannel(channel);
	m_main->m_profile->Write(m_profilePrefix + wxT("Channel"), channel);
}

void panelScope::OnDropDownToolbarMode(wxAuiToolBarEvent& evt)
{
	wxAuiToolBar* tb = static_cast<wxAuiToolBar*>(evt.GetEventObject());
	int id = evt.GetId();

	tb->SetToolSticky(evt.GetId(), true);

	// create the popup menu
	wxMenu menuPopup;

	menuPopup.Append(new wxMenuItem(&menuPopup, 20003, wxT("Auto")));
	menuPopup.Append(new wxMenuItem(&menuPopup, 20004, wxT("Normal")));
	menuPopup.Append(new wxMenuItem(&menuPopup, 20005, wxT("Single")));

	// line up our menu with the button
	wxRect rect = tb->GetToolRect(evt.GetId());
	wxPoint pt = tb->ClientToScreen(rect.GetBottomLeft());
	pt = ScreenToClient(pt);

	PopupMenu(&menuPopup, pt);

	// make sure the button is "un-stuck"
	tb->SetToolSticky(evt.GetId(), false);

}
void panelScope::OnMode(wxCommandEvent& event)
{
	NkDigTimerGraph::ETriggerMode mode = NkDigTimerGraph::ETriggerMode(event.GetId() - 20003);

	switch (mode)
	{
	case NkDigTimerGraph::modeAuto: m_tool->SetToolLabel(ID_TOOLMODE, wxT("Auto")); break;
	case NkDigTimerGraph::modeNormal: m_tool->SetToolLabel(ID_TOOLMODE, wxT("Normal")); break;
	case NkDigTimerGraph::modeSingle: m_tool->SetToolLabel(ID_TOOLMODE, wxT("Single")); break;
	default: break;
	}
	m_tool->Realize();
	m_graph->SetTriggerMode(mode);
	m_main->m_profile->Write(m_profilePrefix + wxT("Mode"), (long)mode);
	m_tool->ToggleTool(ID_TOOLARM, true);
	m_tool->Refresh();
}

wxPanel* CreateScopePanel(frameMain* parent, wxString file, EMODE mode)
{
	return new panelScope(parent, file, mode);
}

