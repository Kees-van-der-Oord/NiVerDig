#pragma once


class wxProcessExecute : public wxProcess {
public:
	wxProcessExecute(wxEvtHandler* parent, int id, int flags)
		: wxProcess(wxPROCESS_DEFAULT)
	{
		Init(parent, id, flags);
	}

	long Execute(const wxString cmd)
	{
		return wxExecute(cmd, wxEXEC_ASYNC, this);
	}

	virtual void OnTerminate(int pid, int status)
	{
		wxProcess::OnTerminate(pid, status);
	}
};

struct SVID_PID
{
	unsigned long vid;
	unsigned long pid;
	const wchar_t desc[64];
};

class arduinoDevice
{
public:
	wxString port;    // for devices that support upload throught he COM port
	wxString vid_pid; // for all devices
	wxString sn;      // for all devices
	wxString dev_num; // for DFU devices
	wxString name;    // friendly name
};

SVID_PID* FindKnownVidPid(unsigned long vid, unsigned long pid);
wxString FindKnownVidPid(wxString, arduinoDevice* device = NULL);


class arduinoDevices : public std::map<wxString,arduinoDevice>
{
public: 
	bool Add(arduinoDevice& device);
	void Insert(arduinoDevice& device) { insert(std::pair<wxString, arduinoDevice>(device.port, device)); }
};

class frameUploadSketch : public formUploadSketch
{
public:
	frameUploadSketch(frameMain* main);
	~frameUploadSketch();

	void UpdateComPorts();
	void UpdateHexFile();
	void StartUpload();
	void StopUpload();
	
	void m_BrowseOnButtonClick(wxCommandEvent& event);
	void m_comboPortOnCombobox(wxCommandEvent& event);
	void m_comboPortOnComboboxDropdown(wxCommandEvent& event);
	void m_choiceBoardOnChoice(wxCommandEvent& event);
	wxString GetSketch();
	int GetModel(wxString& mask);
	void m_buttonStartOnButtonClick(wxCommandEvent& event);
	void OnProcessTerminate(wxProcessEvent& event);
	void OnUpdateTimer(wxTimerEvent& event);
	void OnCloseTimer(wxTimerEvent& event);
	void ReadProcessOutput();

	frameMain* m_main;
	//std::map<wxString, wxString> m_ports;
	arduinoDevices m_devices;
	wxString m_port;
	wxString m_hex;
	wxProcessExecute* m_process;
	wxTimer m_updateTimer;
	wxTimer m_closeTimer;
	wxString m_progress;

private:
	wxDECLARE_EVENT_TABLE();
};
