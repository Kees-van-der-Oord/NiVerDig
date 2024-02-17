#include "framework.h"

frameUploadSketch::frameUploadSketch(frameMain* main)
    : formUploadSketch(main)
    , m_main(main)
    , m_process(NULL)
    , m_updateTimer(this, 100)
    , m_closeTimer(this, 101)
{
    UpdateComPorts();
    UpdateHexFile();
}

frameUploadSketch::~frameUploadSketch()
{
    StopUpload();
}

wxBEGIN_EVENT_TABLE(frameUploadSketch, formUploadSketch)
EVT_END_PROCESS(1, frameUploadSketch::OnProcessTerminate)
EVT_TIMER(100, frameUploadSketch::OnUpdateTimer)
EVT_TIMER(101, frameUploadSketch::OnCloseTimer)
wxEND_EVENT_TABLE()

bool ParseDFU(wxString info, arduinoDevice & device)
{
    // Found Runtime: [2341:0069] ver=0100, devnum=3, cfg=1, intf=2, path="2-1", alt=0, name="DFU-RT Port", serial="35020F0C39313631F55533324B572D76"
    wxRegEx re(wxT("Found (Runtime|DFU): \\[([a-fA-F0-9]+):([a-fA-F0-9]+)\\].+devnum=(\\d+).+name=\"([^\"]+)\".+serial=\"([a-fA-F0-9]+)\""));
    if (!re.Matches(info)) return false;
    unsigned long vid = 0, pid = 0, dev_num;
    re.GetMatch(info, 2).ToULong(&vid, 16);
    re.GetMatch(info, 3).ToULong(&pid, 16);
    re.GetMatch(info, 4).ToULong(&dev_num);
    device.vid_pid = wxString::Format(wxT("%04lX:%04lX"), vid, pid);
    device.dev_num = wxString::Format(wxT("%ld"), dev_num);
    wxString name = re.GetMatch(info, 5);
    device.sn = re.GetMatch(info, 6);
    SVID_PID* vid_pid = FindKnownVidPid(vid, pid);
    if (vid_pid)
    {
        device.name = vid_pid->desc;
    }
    else
    {
        device.name = name;
    }
    device.port = wxString::Format(wxT("DFU device %ld"), dev_num);
    return true;
}

bool arduinoDevices::Add(arduinoDevice& device)
{
    // check if this port is already present
    arduinoDevices::const_iterator i = find(device.port);
    if (i != end())
    {
        return false;
    }

    // check if this device is already present
    if (device.vid_pid.length() && device.sn.length())
    {
        for (auto & i : *this)
        {
            arduinoDevice& d = i.second;
            if ((d.vid_pid == device.vid_pid) && (d.sn == device.sn))
            {
                if (!d.dev_num.length()) d.dev_num = device.dev_num;
                return false;
            }
        }
    }

    Insert(device);
    return true;
}

void frameUploadSketch::UpdateComPorts()
{
	wxString curSel = m_comboPort->GetValue();
    int space = curSel.Find(wxT('\t'));
    if(space != wxNOT_FOUND) curSel = curSel.Left(space);
    curSel.MakeUpper();

    m_comboPort->Clear();
    int curIndex = -1;

    m_devices.clear();

    // add COM port devices
    std::map<wxString, wxString> ports;
    GetAllPortsInfo(ports);
    for (auto i : ports)
    {
        wxString port = i.first;
        arduinoDevice device;
        device.port = port;
        wxString info = FindKnownVidPid(i.second, &device);
        if (m_devices.Add(device))
        {
            m_comboPort->Append(port + wxT("\t ") + info);
            if (!port.Cmp(curSel)) curIndex = m_comboPort->GetCount() - 1;
        }
    }

    // add DFU devices
    wxString command = wxString::Format(wxT("\"%s\\dfu-util.exe\" --list"), wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath());
    wxProcess process(wxPROCESS_REDIRECT);
    wxExecute(command, wxEXEC_SYNC, &process);
    wxTextInputStream tis(*process.GetInputStream());
    while (!tis.GetInputStream().Eof())
    {
        wxString line = tis.ReadLine();
        arduinoDevice device;
        if(ParseDFU(line,device) && m_devices.Add(device))
        {
            m_comboPort->Append(device.port + wxT("\t ") + device.name);
            if (!device.port.Cmp(curSel)) curIndex = m_comboPort->GetCount() - 1;
        }
    }

    if (curIndex == -1) curIndex = 0;
    m_comboPort->Select(curIndex);
}

int frameUploadSketch::GetModel(wxString& mask)
{
    wxString curPort = m_comboPort->GetValue();
    wxString model;
    int iModel = 0;
    curPort.MakeLower();
    if (curPort.Find(wxT("uno r3")) != wxNOT_FOUND) { model = wxT("unoR3"); iModel = 1; }
    if (curPort.Find(wxT("mega 2560 r3")) != wxNOT_FOUND) { model = wxT("megaR3"); iModel = 2; }
    if (curPort.Find(wxT("uno r4 minima")) != wxNOT_FOUND) { model = wxT("unoR4Minima"); iModel = 3; }
    if (curPort.Find(wxT("uno r4 wifi")) != wxNOT_FOUND) { model = wxT("unoR4WiFi"); iModel = 4; }
    if (iModel)
    {
        mask = wxString::Format(iModel > 2 ? wxT("*.%s.ino.bin") : wxT("*.%s.ino.hex"), model);
    }
    return iModel;
}

void frameUploadSketch::UpdateHexFile()
{
    wxString mask;
    int iModel = GetModel(mask);
    m_choiceBoard->SetSelection(iModel);
    m_choiceBoard->Enable(iModel > 0);
    if (!iModel) return;

    wxFileName curHex = GetSketch();
    wxString curHexName = curHex.GetName();
    wxString folder = curHex.GetPath();
    if (folder.IsEmpty())
    {
        folder = wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath() + wxT("\\hex");
    }
    wxArrayString hexFiles;
    wxDir::GetAllFiles(folder, &hexFiles, mask, wxDIR_FILES);
    if (!hexFiles.size())
    {
        folder = wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath() + wxT("\\hex");
        wxDir::GetAllFiles(folder, &hexFiles, mask, wxDIR_FILES);
    }
    m_sketch->Clear();
    if(hexFiles.size())
    {
        int iCurSel = hexFiles.size() - 1;
        for (int i = 0; i < hexFiles.size(); ++i)
        {
            wxString& file = hexFiles[i];
            m_sketch->Append(file);
            if (curHexName == file)
            {
                iCurSel = i;
            }
        }
        m_sketch->SetSelection(iCurSel);
        curHex = m_sketch->GetString(iCurSel);
    }

    m_buttonStart->Enable(!curHex.GetName().IsEmpty() && m_choiceBoard->GetSelection());
}

void frameUploadSketch::m_BrowseOnButtonClick(wxCommandEvent& event) 
{ 
    wxFileName hex = GetSketch();
    wxString folder = hex.GetPath();
    if (folder.IsEmpty())
    {
        folder = wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath();
    }
    wxFileDialog dlg(
        this,
        wxT("Select compiled Arduino Sketch"),
        hex.GetPath(),
        wxEmptyString,
        wxT("All Sketches|*ino*|Uno R3 Sketches|*.unoR3.ino.hex|Mega R3 Sketches|*.megaR3.ino.hex|Uno R4 Minima Sketches|*.unoR4Minima.ino.bin|Uno R4 WiFi Sketches|*.unoR4WiFi.ino.bin"),
        wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    dlg.SetFilterIndex(m_choiceBoard->GetSelection());
    if (!dlg.ShowModal()) return;

    hex = dlg.GetPath();
    folder = hex.GetPath();

    wxString mask;
    int iModel = GetModel(mask);
    m_sketch->Clear();
    wxArrayString hexFiles;
    wxDir::GetAllFiles(folder, &hexFiles, mask, wxDIR_FILES);
    if (hexFiles.size())
    {
        for (int i = 0; i < hexFiles.size(); ++i)
        {
            wxString& file = hexFiles[i];
            m_sketch->Append(file);
        }
    }
    int iCurSel = m_sketch->FindString(hex.GetFullPath());
    if (iCurSel == wxNOT_FOUND)
    {
        iCurSel = m_sketch->Append(hex.GetFullPath());
    }
    m_sketch->SetSelection(iCurSel);
    m_buttonStart->Enable(hex.IsOk() && hex.FileExists() && m_choiceBoard->GetSelection());
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
    wxFileName hex(GetSketch());
    m_buttonStart->Enable(hex.IsOk() && hex.FileExists() && m_choiceBoard->GetSelection());
}

wxString frameUploadSketch::GetSketch()
{
    int iCurSel = m_sketch->GetSelection();
    if (iCurSel == wxNOT_FOUND) return wxEmptyString;
    return m_sketch->GetString(iCurSel);
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
    int tab = port.Find(wxT('\t'));
    if (tab != wxNOT_FOUND) m_port = port.Left(tab);
    else m_port = port;
    arduinoDevices::iterator i = m_devices.find(m_port);
    if (i == m_devices.end()) return;
    arduinoDevice& device = i->second;

    if (!m_main->m_portName.CmpNoCase(m_port))
    {
        m_main->SetPort(wxEmptyString);
    }
    wxFileName f(wxStandardPaths::Get().GetExecutablePath());
    wxString folder = f.GetPath();
    wxString hex = GetSketch();
    int iBoard = m_choiceBoard->GetSelection();

    wxString command;
    if ((iBoard == 1) || (iBoard == 2))
    {
        wxString args;
        switch (iBoard)
        {
        case 1: args = wxT(" -patmega328p -carduino"); break;
        case 2: args = wxT("-patmega2560 -cwiring"); break;
        default: return;
        }
        command = wxString::Format(wxT("\"%s\\avrdude.exe\" -C\"%s\\avrdude.conf\" -v -V %s -P%s  -b115200 -D -Uflash:w:\"%s\":i"), folder, folder, args, m_port, hex);
    }
    else // R4 Minima or WiFi
    {
        wxString vid_pid;
        switch (iBoard)
        {
        case 3: vid_pid = wxT("0x2341:0x0069,:0x0369"); break;
        case 4: vid_pid = wxT("0x2341:0x1002,:0x006D"); break;
        }
        wxString devnum = device.dev_num;
        if (devnum.length())
        {
            command = wxString::Format(wxT("\"%s\\dfu-util.exe\" --device %s --devnum %s -D \"%s\" -a0 -Q"), folder, vid_pid, devnum, hex);
        }
        if(!command.length())
        {
            // assume there is only one Uno R4 connected ?
            command = wxString::Format(wxT("\"%s\\dfu-util.exe\" --device %s -D \"%s\" -a0 -Q"), folder, vid_pid, hex);
        }
    }

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
    if (!result)
    {
        m_closeTimer.Start(2000, true);
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
    if (!m_port.IsEmpty() && m_main->m_portName.IsEmpty())
    {
        m_main->SetPort(m_port);
    }
    EndModal(wxOK);
}
