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

class frameUploadSketch : public formUploadSketch
{
public:
	frameUploadSketch(frameMain* main);
	~frameUploadSketch();

	void UpdateComPorts();
	void UpdateHexFile();
	void StartUpload();
	void StopUpload();
	
	void m_comboPortOnCombobox(wxCommandEvent& event);
	void m_comboPortOnComboboxDropdown(wxCommandEvent& event);
	void m_choiceBoardOnChoice(wxCommandEvent& event);
	void m_fileSketchOnFileChanged(wxFileDirPickerEvent& event);
	void m_buttonStartOnButtonClick(wxCommandEvent& event);
	void OnProcessTerminate(wxProcessEvent& event);
	void OnUpdateTimer(wxTimerEvent& event);
	void OnCloseTimer(wxTimerEvent& event);
	void ReadProcessOutput();

	frameMain* m_main;
	std::map<wxString, wxString> m_ports;
	wxString m_port;
	wxString m_hex;
	wxProcessExecute* m_process;
	wxTimer m_updateTimer;
	wxTimer m_closeTimer;
	wxString m_progress;

private:
	wxDECLARE_EVENT_TABLE();
};
