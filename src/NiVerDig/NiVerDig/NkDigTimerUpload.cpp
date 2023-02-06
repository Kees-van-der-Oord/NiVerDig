#include "framework.h"

frameUploadSketch::frameUploadSketch(frameMain* main)
	: formUploadSketch(main)
	, m_main(main)
    , m_process(NULL)
    , m_updateTimer(this,100)
    , m_closeTimer(this, 101)
{
    UpdateComPorts();
}

frameUploadSketch::~frameUploadSketch()
{
    StopUpload();
}

wxBEGIN_EVENT_TABLE(frameUploadSketch, formUploadSketch)
EVT_END_PROCESS(wxID_ANY, frameUploadSketch::OnProcessTerminate)
EVT_TIMER(100,frameUploadSketch::OnUpdateTimer)
EVT_TIMER(101, frameUploadSketch::OnCloseTimer)
wxEND_EVENT_TABLE()

void frameUploadSketch::UpdateComPorts()
{
	wxString curSel = m_comboPort->GetValue();
    int space = curSel.Find(wxT(' '));
    if(space != wxNOT_FOUND) curSel = curSel.Left(space);
    curSel.MakeUpper();
    m_comboPort->Clear();

    GetAllPortsInfo(m_ports);

    int curIndex = -1;
    for (auto i : m_ports)
    {
        wxString port = i.first;
        wxString info = FindKnownVidPid(i.second);
        m_comboPort->Append(port + wxT(" ") + info);
        if (!port.Cmp(curSel)) curIndex = m_comboPort->GetCount() - 1;
    }
    if (curIndex == -1) curIndex = 0;
    m_comboPort->Select(curIndex);
    UpdateHexFile();
}

void frameUploadSketch::UpdateHexFile()
{
    wxString curPort = m_comboPort->GetValue();
    wxString model;
    int iModel = 0;
    curPort.MakeLower();
    if (curPort.Find(wxT("uno")) != wxNOT_FOUND) { model = wxT("uno"); iModel = 1;  }
    if (curPort.Find(wxT("mega")) != wxNOT_FOUND) { model = wxT("mega"); iModel = 2; }
    m_choiceBoard->SetSelection(iModel);
    m_choiceBoard->Enable(model.IsEmpty());

    wxFileName curHex = m_fileSketch->GetFileName();
    wxString curHexName = curHex.GetName();
    if (model.IsEmpty()) return;
    if (curHexName.Find(model) != wxNOT_FOUND) return;

    wxString folder = curHex.GetPath();
    if (folder.IsEmpty())
    {
        folder = wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath();
    }
    wxArrayString hexFiles;
    wxDir::GetAllFiles(folder, &hexFiles, wxString::Format(wxT("*.%s.ino.hex"),model), wxDIR_FILES);
    if (!hexFiles.size())
    {
        folder = wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath();
        wxDir::GetAllFiles(folder, &hexFiles, wxString::Format(wxT("*.%s.ino.hex"), model), wxDIR_FILES);
    }
    for (auto file : hexFiles)
    {
        curHex = wxFileName(file);
        m_fileSketch->SetFileName(curHex);
        break;
    }

    m_buttonStart->Enable(!curHex.GetName().IsEmpty() && m_choiceBoard->GetSelection());
}


void frameUploadSketch::m_comboPortOnCombobox(wxCommandEvent& event) 
{ 
	event.Skip();
    UpdateHexFile();
}

void frameUploadSketch::m_comboPortOnComboboxDropdown(wxCommandEvent& event) 
{ 
	event.Skip(); 
    UpdateComPorts();
}

void frameUploadSketch::m_choiceBoardOnChoice(wxCommandEvent& event) 
{ 
    event.Skip(); 
    m_buttonStart->Enable(!m_fileSketch->GetFileName().GetName().IsEmpty() && m_choiceBoard->GetSelection());
}

void frameUploadSketch::m_fileSketchOnFileChanged(wxFileDirPickerEvent& event) 
{ 
	event.Skip(); 
    m_buttonStart->Enable(!m_fileSketch->GetFileName().GetName().IsEmpty() && m_choiceBoard->GetSelection());
}

void frameUploadSketch::m_buttonStartOnButtonClick(wxCommandEvent& event) 
{ 
	event.Skip();
    if (m_process)
    {
        StopUpload();
        m_buttonStart->SetLabel(wxT("Start"));
    }
    else
    {
        StartUpload();
        m_buttonStart->SetLabel(wxT("Stop"));
    }
}

void frameUploadSketch::StartUpload()
{
    wxString port = m_comboPort->GetValue();
    int space = port.Find(wxT(' '));
    if (space != wxNOT_FOUND) port = port.Left(space);
    m_port = port;

    if (!m_main->m_portName.CmpNoCase(port))
    {
        m_main->SetPort(wxEmptyString);
    }
    wxFileName f(wxStandardPaths::Get().GetExecutablePath());
    wxString folder = f.GetPath();
    wxString args;
    switch (m_choiceBoard->GetSelection())
    {
    case 1: args = wxT(" -patmega328p -carduino"); break;
    case 2: args = wxT("-patmega2560 -cwiring"); break;
    default: return;
    }
    wxString hex = m_fileSketch->GetFileName().GetFullPath();

    wxString command = wxString::Format(wxT("\"%s\\avrdude.exe\" -C\"%s\\avrdude.conf\" -v %s -P%s  -b115200 -D -Uflash:w:\"%s\":i"),folder, folder, args, port, hex);

    m_process = new wxProcessExecute(this, 1, wxPROCESS_REDIRECT | wxEVT_END_PROCESS);
    m_process->Execute(command);
    OutputDebugString(command); OutputDebugString(L"\n");
    m_progress.Clear();
    m_textProgress->Clear();
    m_updateTimer.Start(100);
}

void frameUploadSketch::StopUpload()
{
    if (m_process)
    {
        m_process->Kill(m_process->GetPid());
        m_updateTimer.Stop();
    }
}

void frameUploadSketch::OnProcessTerminate(wxProcessEvent& event)
{
    ReadProcessOutput();
    int result = event.GetExitCode();
    delete m_process;
    m_process = NULL;
    m_buttonStart->SetLabel(wxT("Start"));
    if (!result && m_main->m_portName.IsEmpty())
    {
        m_main->SetPort(m_port);
        m_closeTimer.Start(1000, true);
    }
}

void frameUploadSketch::OnUpdateTimer(wxTimerEvent& event)
{
    ReadProcessOutput();
}

void frameUploadSketch::ReadProcessOutput()
{
    if (!m_process) return;
    wxInputStream* is = m_process->GetInputStream();
    wxString text;
    if (is)
    {
        wxTextInputStream tis(*is);
        while (is->CanRead())
        {
            wxChar c = tis.GetChar();
            text += c;
            
        }
    }
    wxInputStream* es = m_process->GetErrorStream();
    if (es)
    {
        wxTextInputStream tes(*es);
        while (es->CanRead())
        {
            wxChar c = tes.GetChar();
            text += c;
        }
    }
    if (!text.IsEmpty())
    {
        m_textProgress->AppendText(text);
    }
}

void frameUploadSketch::OnCloseTimer(wxTimerEvent& event)
{
    EndModal(wxOK);
}
