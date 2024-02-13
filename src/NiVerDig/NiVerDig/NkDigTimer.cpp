// NkDigTimer.cpp : Defines the entry point for the application.
//

#include "framework.h"

static const wxCmdLineEntryDesc g_cmdLineDesc[] =
{
    { wxCMD_LINE_OPTION, "r", "record", "<filename>",      wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL},
    { wxCMD_LINE_OPTION, "s", "show",   "minimized|maximized|normal", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL},
    { wxCMD_LINE_PARAM, NULL, NULL,    "recorded file to view", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL},
    //{ wxCMD_LINE_PARAM, NULL, NULL, "recording", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM},
    { wxCMD_LINE_NONE }
};

class appMain : public wxApp
{
public:

    appMain()
	: wxApp()
	, m_mode(MODE_CONTROL)
	, m_showCmd(-1)
    {}

    void OnInitCmdLine(wxCmdLineParser& parser)
    {
        parser.SetDesc(g_cmdLineDesc);
        // must refuse '/' as parameter starter or cannot use "/path" style paths
        parser.SetSwitchChars(wxT("-"));
    }

    bool OnCmdLineParsed(wxCmdLineParser& parser)
    {
        if (parser.Found(wxT("record"), &m_file))
        {
            m_mode = MODE_RECORD;
        }
        wxString showCommand;
        if (parser.Found(wxT("show"), &showCommand))
        {
            if (!showCommand.Left(2).CompareTo(wxT("mi"), wxString::ignoreCase))
            {
                m_showCmd = SW_MINIMIZE;
            }
            else if (!showCommand.Left(2).CompareTo(wxT("ma"), wxString::ignoreCase))
            {
                m_showCmd = SW_MAXIMIZE;
            }
            else if (!showCommand.Left(2).CompareTo(wxT("no"), wxString::ignoreCase))
            {
                m_showCmd = SW_NORMAL;
            }
        }
        if(parser.GetParamCount() >= 1)
        {
            m_file = parser.GetParam(0);
            m_mode = MODE_VIEW;
        }
        return true;
    }

    bool OnInit() wxOVERRIDE;

    EMODE m_mode;
    wxString m_file;
    long     m_showCmd;
};

wxDECLARE_APP(appMain);
wxIMPLEMENT_APP(appMain);

enum EPorts { EPORTMENU = 10000, EPORTMENULAST = EPORTMENU + 1000, EUPLOADHEX };


enum EPanels 
{ 
    ENOPANEL = -1, 
    ECONTROLPANEL = ID_TOOLCONTROL, 
    EPINSPANEL = ID_TOOLPINS, 
    ETASKSPANEL = ID_TOOLTASKS,
    ESCOPEPANEL = ID_TOOLSCOPE,
    ECONSOLEPANEL = ID_TOOLCONSOLE
};

wxBEGIN_EVENT_TABLE(frameMain, formMain)
EVT_MENU_RANGE(EPORTMENU, EPORTMENULAST, OnMenuCommandPort)
EVT_MENU(EUPLOADHEX, OnMenuCommandUpload)
wxEND_EVENT_TABLE()

frameMain::frameMain(wxWindow* parent,
    wxWindowID id,
    const wxString& title,
    const wxPoint& pos,
    const wxSize& size,
    long style)
    : formMain(parent, id, title, pos, size, style)
    , m_profileName(wxStandardPaths::Get().GetDocumentsDir() + wxT("/.NkNiVerDig"))
    , m_port(NULL)
    , m_panel(NULL)
    , m_epanel(ENOPANEL)
    , m_halt(false)
    , m_log()
    , m_ilog(m_log.m_name)
    , dev_notify(NULL)
{
    wxGetApp().SetTopWindow(this);
    SetIcon(wxIcon(wxT("IDI_AAAAPPLICATION"), wxBITMAP_TYPE_ICO_RESOURCE, 64, 64));

    // persistence
    m_profile = new wxFileConfig(wxT("NiVerDig"), wxT("Nikon"), m_profileName);
    RestoreSettings();

    // command line arguments
    if (wxGetApp().m_mode == MODE_VIEW)
    {
        m_toolBar->Hide();
        ShowPanel(ESCOPEPANEL, true);
    }
    if (wxGetApp().m_mode == MODE_RECORD)
    {
        ShowPanel(ESCOPEPANEL, true);
    }
}

frameMain::~frameMain()
{
    StoreSettings();
    NkComPort_Close(&m_port);
}

void frameMain::StoreSettings()
{
    bool maximized = IsMaximized();
    m_profile->Write(wxT("Window/Maximized"), maximized);
    bool minimized = IsIconized();
    m_profile->Write(wxT("Window/Minimized"), minimized);
    if (!maximized && !minimized)
    {
        wxRect rcWnd = this->GetRect();
        m_profile->Write(wxT("Window/Top"), rcWnd.y);
        m_profile->Write(wxT("Window/Left"), rcWnd.x);
        m_profile->Write(wxT("Window/Width"), rcWnd.width);
        m_profile->Write(wxT("Window/Height"), rcWnd.height);
    }

    m_profile->Write(wxT("Port/Name"), m_lastPortName);
    m_profile->Flush();
    wxFileName file(m_profileName);
    SetFileAttributesW(file.GetFullPath().c_str(), FILE_ATTRIBUTE_HIDDEN);
}

inline void fitRectOnScreen(wxRect& rect)
{
    wxRect rcScreen;
    int n = wxDisplay::GetCount();
    for (int i = 0; i < n; i++) {
        wxDisplay display(i);
        wxRect rcDisplay = display.GetClientArea();
        rcScreen.Union(rcDisplay);
    }
    if (rect.x < rcScreen.x) rect.x = rcScreen.x;
    if (rect.y < rcScreen.y) rect.y = rcScreen.y;
    if (rect.width > rcScreen.width) rect.width = rcScreen.width;
    if (rect.height > rcScreen.height) rect.height = rcScreen.height;
    int overshoot = (rect.x + rect.width) - (rcScreen.x + rcScreen.width);
    if (overshoot > 0) rect.x -= overshoot;
    overshoot = (rect.y + rect.height) - (rcScreen.y + rcScreen.height);
    if (overshoot > 0) rect.y -= overshoot;
}

void frameMain::RestoreSettings()
{
    wxRect rcWnd;
    rcWnd.x = m_profile->Read(wxT("Window/Left"), -100000);
    rcWnd.y = m_profile->Read(wxT("Window/Top"), -100000);
    rcWnd.width = m_profile->Read(wxT("Window/Width"), -100000);
    rcWnd.height = m_profile->Read(wxT("Window/Height"), -100000);

    if ((rcWnd.x != -100000) && (rcWnd.y != -100000) && (rcWnd.width != -100000) && (rcWnd.height != -100000))
    {
        fitRectOnScreen(rcWnd);
        this->SetPosition(rcWnd.GetTopLeft());
        this->SetSize(rcWnd.GetSize());
    }
    bool maximized = m_profile->Read(wxT("Window/Maximized"), 0L);
    long minimized = m_profile->Read(wxT("Window/Minimized"), 0L);
    if (maximized)
    {
        Maximize();
    }
    else if (minimized)
    {
        Iconize(true);
    }

    if (wxGetApp().m_mode != MODE_VIEW)
    {
        m_lastPortName = m_profile->Read(wxT("Port/Name"));
        SetPort(m_lastPortName);
    }
    else
    {
        SetTitle(wxT("Viewer"), wxEmptyString);
    }
}

void GetAllPortsInfo(std::map<wxString, wxString> & ports)
{
    ports.clear();
    wchar_t buf[4096];
    NkComPort_ListPortsEx(buf, 4096);
    for (wchar_t* p = buf; *p; )
    {
        size_t tab = wcscspn(p, L"\t\n");
        size_t nl = wcscspn(p, L"\n");
        wxString port = wxString(p, tab);
        wxString info;
        if (tab < nl)
        {
            if (p[tab] == L'\t') ++tab;
            info = wxString(p + tab, nl - tab);
        }
        // some ports are listed twice ?? re-register them if the info field was empty
        wxString tmp = ports[port];
        if (!tmp.length())
        {
            ports[port] = info;
        }
        p += nl;
        if (*p) ++p;
    }
}

wxString GetPortInfo(std::map<wxString, wxString>& ports, wxString port)
{
    port.MakeLower();
    wxString info;
    if (ports.count(port))
    {
        return ports[port];
    }
    GetAllPortsInfo(ports);
    if (ports.count(port))
    {
        return ports[port];
    }
    return wxEmptyString;
}

void frameMain::formMainOnClose(wxCloseEvent& event)
{
    if (event.CanVeto())
    {
        if (!ShowPanel(-1, false))
        {
            event.Veto();
            return;
        }
    }
    event.Skip();
}

#if _MSC_VER <= 1500
const GUID GUID_DEVINTERFACE_COMPORT = { 0x86E0D1E0,0x8089,0x11D0,{0x9C,0xE4,0x08,0x00,0x3E,0x30,0x1F,0x73} };
#endif

typedef struct _DEV_BROADCAST_HDR {
    DWORD dbch_size;
    DWORD dbch_devicetype;
    DWORD dbch_reserved;
} DEV_BROADCAST_HDR, * PDEV_BROADCAST_HDR;
#define DBT_DEVNODES_CHANGED     0x0007
#define DBT_DEVICEARRIVAL        0x8000
#define DBT_DEVICEREMOVECOMPLETE 0x8004

#define DBT_DEVTYP_PORT            0x00000003
typedef struct _DEV_BROADCAST_PORT_W {
    DWORD dbcc_size;
    DWORD dbcc_devicetype;
    DWORD dbcc_reserved;
    wchar_t dbcc_name[1];
} DEV_BROADCAST_PORT_W, * PDEV_BROADCAST_PORT_W;
#define DEV_BROADCAST_PORT DEV_BROADCAST_PORT_W
#define PDEV_BROADCAST_PORT PDEV_BROADCAST_PORT_W

#define DBT_DEVTYP_DEVICEINTERFACE 0x00000005
typedef struct _DEV_BROADCAST_DEVICEINTERFACE_W {
    DWORD dbcc_size;
    DWORD dbcc_devicetype;
    DWORD dbcc_reserved;
    GUID  dbcc_classguid;
    wchar_t dbcc_name[1];
} DEV_BROADCAST_DEVICEINTERFACE_W, * PDEV_BROADCAST_DEVICEINTERFACE_W;
#define DEV_BROADCAST_DEVICEINTERFACE DEV_BROADCAST_DEVICEINTERFACE_W
#define PDEV_BROADCAST_DEVICEINTERFACE PDEV_BROADCAST_DEVICEINTERFACE_W

void frameMain::RegisterDeviceEventNotifications()
{
    DEV_BROADCAST_DEVICEINTERFACE filter = { sizeof(DEV_BROADCAST_DEVICEINTERFACE) };
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    filter.dbcc_classguid = GUID_DEVINTERFACE_COMPORT;
    dev_notify = RegisterDeviceNotification(this->GetHWND(), &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
}

LRESULT frameMain::OnDeviceChange(WPARAM wParam, LPARAM lParam)
{
    switch (wParam)
    {
    case DBT_DEVICEARRIVAL:
    case DBT_DEVICEREMOVECOMPLETE:
        break;
    default:
        return 0;
    }
    PDEV_BROADCAST_HDR lphdr = (PDEV_BROADCAST_HDR)lParam;
    if (!lphdr || (lphdr->dbch_devicetype != DBT_DEVTYP_PORT)) return 0;
    PDEV_BROADCAST_PORT lpprt = (PDEV_BROADCAST_PORT)lphdr;
    const wchar_t* name = lpprt->dbcc_name;
    if (wcscmp(name, m_lastPortName)) return 0;

    if (IsConnected() && (wParam == DBT_DEVICEREMOVECOMPLETE))
    {
        SetPort(wxEmptyString);
    }
    else if (!IsConnected() && (wParam == DBT_DEVICEARRIVAL))
    {
        SetPort(name);
    }
    
    return 0;
}

void frameMain::SetStatus(wxString str)
{
    m_statusBar->SetStatusText(str);
}

void frameMain::m_toolPortOnAuiToolBarToolDropDown(wxAuiToolBarEvent& evt)
{
    //if (!evt.IsDropDownClicked()) return;
    wxAuiToolBar* tb = static_cast<wxAuiToolBar*>(evt.GetEventObject());
    tb->SetToolSticky(evt.GetId(), true);

    // create the popup menu
    wxMenu menuPopup;
    int index = 0;
    GetAllPortsInfo(m_ports);
    m_portIds.clear();
    for (auto & i: m_ports)
    {
        wxString port = i.first;
        wxString info = FindKnownVidPid(i.second);
        wxString str = port + wxT(" ") + info;
        wxMenuItem* item = new wxMenuItem(&menuPopup, EPORTMENU + index, str, wxEmptyString, wxITEM_CHECK);
        menuPopup.Append(item);
        if (port == m_portName)
        {
            item->Check(true);
        }
        m_portIds.push_back(port);
        ++index;
    }
    menuPopup.Append(new wxMenuItem(&menuPopup, EUPLOADHEX, wxT("Upload NiVerDig sketch to Uno/Mega"), wxEmptyString, wxITEM_NORMAL));

    // line up our menu with the button
    wxRect rect = tb->GetToolRect(evt.GetId());
    wxPoint pt = tb->ClientToScreen(rect.GetBottomLeft());
    pt = ScreenToClient(pt);

    PopupMenu(&menuPopup, pt);

    // make sure the button is "un-stuck"
    tb->SetToolSticky(evt.GetId(), false);
}

void frameMain::OnMenuCommandPort(wxCommandEvent& event)
{
    nkDigTimPanel* dtp = dynamic_cast<nkDigTimPanel*>(m_panel);
    if (dtp && !dtp->CanClosePanel(this, true))
    {
        return;
    }
    int id = event.GetId() - EPORTMENU;
    if ((id < 0) || (id >= m_ports.size())) return;
    SetPort(m_portIds[id]);
}

void frameMain::OnMenuCommandUpload (wxCommandEvent& evt)
{
    frameUploadSketch dlg(this);
    dlg.ShowModal();
}


// 2341 Arduino SA
#define ARDUINO_VID 0x2341
// 2A03 Arduino Org
#define ARDUINO_VID2 0x2A03

SVID_PID vid_pid[]
{
    {ARDUINO_VID, 0x0001, L"Uno (CDC ACM)"},
    {ARDUINO_VID, 0x0010, L"Mega 2560 (CDC ACM)"},
    {ARDUINO_VID, 0x0036, L"Leonardo Bootloader"},
    {ARDUINO_VID, 0x0037, L"Micro"},
    {ARDUINO_VID, 0x003b, L"Serial Adapter (CDC ACM)"},
    {ARDUINO_VID, 0x003d, L"Due Programming Port"},
    {ARDUINO_VID, 0x003e, L"Due"},
    {ARDUINO_VID, 0x003f, L"Mega ADK (CDC ACM)"},
    {ARDUINO_VID, 0x0042, L"Mega 2560 R3"},
    {ARDUINO_VID, 0x0043, L"Uno R3"},
    {ARDUINO_VID, 0x0044, L"Mega ADK R3"},
    {ARDUINO_VID, 0x0045, L"Serial R3 (CDC ACM)"},
    {ARDUINO_VID, 0x0049, L"ISP"},
    {ARDUINO_VID, 0x004D, L"Zero"},
    {ARDUINO_VID, 0x0058, L"Nano Every"},
    {ARDUINO_VID, 0x0068, L"Portenta C33"},
    {ARDUINO_VID, 0x006D, L"Uno R4 WiFi DFU"},
    {ARDUINO_VID, 0x0069, L"Uno R4 Minima"},
    {ARDUINO_VID, 0x0242, L"Genuino Mega2560-R3"},
    {ARDUINO_VID, 0x0243, L"Genuino Uno-R3"},
    {ARDUINO_VID, 0x0369, L"Uno R4 Minima DFU"},
    {ARDUINO_VID, 0x1002, L"Uno R4 WiFi"},
    {ARDUINO_VID, 0x8036, L"Leonardo (CDC ACM, HID)"},
    {ARDUINO_VID, 0x8037, L"Micro"},
    {ARDUINO_VID, 0x8038, L"Robot Control Board (CDC ACM, HID)"},
    {ARDUINO_VID, 0x8039, L"Robot Motor Board (CDC ACM, HID)"},
    {ARDUINO_VID, 0x804D, L"Zero"}
};

SVID_PID* FindKnownVidPid(unsigned long vid, unsigned long pid)
{
    if (vid == ARDUINO_VID2) vid = ARDUINO_VID;
    for (size_t i = 0; i < countof(vid_pid); ++i)
    {
        if ((vid_pid[i].vid == vid) && (vid_pid[i].pid == pid))
        {
            return vid_pid + i;
        }
    }
    return NULL;
}

wxString FindKnownVidPid(wxString info, arduinoDevice * device)
{
    wxRegEx re(wxT("(.+)?\tVID:([0-9a-fA-F]+) PID:([0-9a-fA-F]+)( REV:([0-9a-fA-F]+))?( SN:.+)?"));
    if (re.Matches(info))
    {
        unsigned long vid = 0, pid = 0;
        re.GetMatch(info, 2).ToULong(&vid,16);
        re.GetMatch(info, 3).ToULong(&pid,16);
        SVID_PID * vid_pid = FindKnownVidPid(vid, pid);
        if(vid_pid)
        {
            wxString desc = wxT("Arduino ") + wxString(vid_pid->desc);
            wxString rev = re.GetMatch(info, 4);
            if (rev.length())
            {
                desc += wxT(" ") + rev;
            }
            wxString sn = re.GetMatch(info, 6);
            if (sn.length())
            {
                desc += wxT("\t") + sn;
            }
            if (device)
            {
                device->vid_pid = wxString::Format(wxT("%04X:%04X"), vid, pid);
                device->sn = sn.Mid(4);
                device->name = vid_pid->desc;
            }
            return desc;
        }
    }
    return info;
}

void frameMain::ClosePort()
{
    NkComPort_Close(&m_port);
    SetTitle(wxT("not connected"), wxEmptyString);
}

void frameMain::SetPort(wxString port)
{
    wxBusyCursor wait;
    ClosePort();
    ShowPanel(ECONTROLPANEL, true, false);
    m_statusBar->SetStatusText(wxT("not connected"));
    m_portName.Clear();
    m_toolPort->SetLabel(wxT("Port ..."));
    if (!port.length())
    {
        ShowPanel(ECONTROLPANEL, true);
        return;
    }

    //int baud_rates[] = {0, 1843200, 921600, 460800, 230400, 115200, 57600, 38400, 19200, 9600 };
    int baud_rates[] = {0, 2000000, 1000000, 500000, 250000, 115200, 57600, 38400, 19200, 9600 }; // up to 20% speed difference is still ok ???
    int baud_index = 0;
    wchar_t answer[1024] = { 0 };
    while (1)
    {
        wxString p = port;
        if (!baud_index)
        {
            p += m_profile->Read(wxString::Format(wxT("Port/%s"), port));
        }
        else
        {
            p += wxString::Format(wxT(":%ld"), baud_rates[baud_index]);
        }

        m_log.Log(wxString::Format(wxT("opening %s %s\n"), p,GetPortInfo(m_ports, port)), -1);
        if (!NkComPort_Open(&m_port, p))
        {
            m_statusBar->SetStatusText(wxT("could not open ") + port);
            return;
        }

        NkComPort_SetBuffers(m_port, 16192, 1024);

        long count = 0;
        wxLongLong waitUntil;

        WriteLine(wxT("?\n"));
        count = ReadLine(answer, countof(answer) - 1, 2000); // the Arduino Uno waits 1 second for program upload
        if ((count > 0) && !wcsncmp(answer, wxT("NiVerDig"), 8))
        {
            if (baud_index)
            {
                wchar_t baud[64] = {0};
                NkComPort_GetConnectionDetails(m_port,baud,64);
                m_profile->Write(wxString::Format(wxT("Port/%s"), port), wxString(baud + wcscspn(baud, wxT(":"))));
            }
            break;
        }
        
        NkComPort_Close(&m_port);
        ++baud_index;
        if(baud_index >= countof(baud_rates))
        {
            ShowPanel(ECONTROLPANEL, true);
            return;
        }
    }

    SetTitle(port, answer);
    m_portName = port;
    m_lastPortName = port;
    m_toolPort->SetLabel(port);
    m_statusBar->SetStatusText(wxT("connected to ") + port);
    m_toolBar->Realize();
/*
    WriteLine(wxT("?\n"));
    while (0 < (count = NkComPort_ReadLine(m_port, answer, countof(answer)-1, 100)))
    {
        // Log
    }
*/
    ReadAll();
    WriteLine(wxT("halt\n"));
    ReadLine(answer, 1024, 200);
    swscanf_s(answer, wxT("halt=%ld"), &m_halt);

    ShowPanel(ECONTROLPANEL, true);
}

void frameMain::SetTitle(wxString port, wxString answer)
{
    formMain::SetTitle(wxT("NiVerDig ") + wxGetFileVersion(wxEmptyString) + wxT(" ") + port + wxT(" ") + answer);
}

bool frameMain::IsConnected()
{
    return NkComPort_IsConnected(m_port);
}

long frameMain::ReadLine(wchar_t* answer, long size, unsigned long timeout)
{
    long count = NkComPort_ReadLine(m_port, answer, size, timeout);
    if (count <= 0) return count;
    wchar_t* cr = wcspbrk(answer, L"\r");
    if (cr) { cr[0] = L'\n'; cr[1] = 0; }
    m_statusBar->SetStatusText(answer);
    m_log.Log(answer,false);
    return count;
}

void frameMain::ReadAll()
{
    wchar_t answer[1024];
    while (ReadLine(answer, 1024, 100) > 0)
    {
    }
}

long frameMain::WriteLine(const wchar_t* line)
{
    m_statusBar->SetStatusText(line);
    if (wcslen(line) > 127)
    {
        wxMessageBox(line, wxT("command exceeds maximal length"));
        return 0;
    }
    long r = NkComPort_WriteLine(m_port, line);
    m_log.Log(line,true);
    return r;
}

void frameMain::m_toolControlOnToolClicked(wxCommandEvent& event) 
{ 
    event.Skip();
    ShowPanel(ECONTROLPANEL, false);
}

void frameMain::m_toolPinsOnToolClicked(wxCommandEvent& event) 
{ 
    event.Skip(); 
    ShowPanel(EPINSPANEL, false);
}

void frameMain::m_toolTasksOnToolClicked(wxCommandEvent& event) 
{ 
    event.Skip(); 
    ShowPanel(ETASKSPANEL, false);
}

void frameMain::m_toolScopeOnToolClicked(wxCommandEvent& event) 
{ 
    event.Skip(); 
    ShowPanel(ESCOPEPANEL, false);
}

void frameMain::m_toolConsoleOnToolClicked(wxCommandEvent& event)
{
    event.Skip();
    ShowPanel(ECONSOLEPANEL, false);
}

void frameMain::m_toolHelpOnToolClicked(wxCommandEvent& event)
{
    event.Skip();
    wxFileName pdf(::wxStandardPaths::Get().GetExecutablePath());
    pdf.SetName(wxT("NiVerDig"));
    pdf.SetExt(wxT("pdf"));
    ShellExecute(NULL, L"open", pdf.GetFullPath(), NULL, NULL, SW_SHOWNORMAL);
}

bool frameMain::ShowPanel(int panel, bool refresh, bool allow_veto)
{
    if (!refresh && (m_epanel == panel)) return true;

    if ((m_epanel != panel) && m_panel)
    {
        nkDigTimPanel* dtp = dynamic_cast<nkDigTimPanel*>(m_panel);
        if (dtp && !dtp->CanClosePanel(this, allow_veto))
        {
            m_toolBar->ToggleTool(m_epanel, true);
            m_toolBar->Refresh();
            return false;
        }
    }
    m_epanel = ENOPANEL;
    m_mainSizer->Clear(true);
    m_panel = NULL;

    switch (panel)
    {
    case ECONTROLPANEL: m_panel = CreateControlPanel(this); break;
    case EPINSPANEL: m_panel = CreateSetupPanel(this, m_pins);  break;
    case ETASKSPANEL:m_panel = CreateSetupPanel(this, m_tasks);   break;
    case ESCOPEPANEL:m_panel = CreateScopePanel(this, wxGetApp().m_file, wxGetApp().m_mode); wxGetApp().m_mode = MODE_CONTROL; break;
    case ECONSOLEPANEL:m_panel = CreateConsolePanel(this);   break;
    }
    if (m_panel)
    {
        m_mainSizer->Add(m_panel, 1, wxEXPAND | wxALL, 5);
    }
    m_epanel = panel;
    m_toolBar->ToggleTool(m_epanel, true);
    m_toolBar->Refresh();

    Layout();
    return true;
}

void frameMain::ParseItems(wxString command, SItems& items)
{
    items.clear();
    items.command = command;

    wxRegEx reRange(wxT(" *<(.+)> *: *\\[(-?\\d+) +to +(\\d+)\\]"), wxRE_EXTENDED);
    wxRegEx reEnum(wxT(" *<(.+)> *: *\\((.+)\\)"), wxRE_EXTENDED);
    wxRegEx reString(wxT(" *<(.+)> *: *(.*)"), wxRE_EXTENDED);
    wxRegEx reMode(wxT("<(.+?)> *(\\[(\\d+) +to +(\\d+)\\])? *(\\((.+)\\))? ?"), wxRE_EXTENDED);
    wxRegEx trimQuotes(wxT("'(.*)'"));
    wxRegEx reIndex(wxT("\\[(\\d+)\\]"));

    wchar_t answer[4096];
    long count;
    WriteLine(command + wxT(" ?\n"));
    while (0 < (count = ReadLine(answer, countof(answer) - 1, 200)))
    {
        wchar_t* cr = wcspbrk(answer, wxT("\r\n"));
        if (cr) *cr = 0;

        // field definition and item definition start with 2 spaces
        if (count < 3) continue;
        if (wcsncmp(answer, wxT(" "), 1)) continue;

        // field definition starts with a <
        if (answer[1] == wxT('<'))
        {
            // simple range
            if (reRange.Matches(answer))
            {
                SField field; wxString tmp;
                field.type = SField::eRange;
                field.name = reRange.GetMatch(answer, 1);
                if (field.name == wxT("index")) field.type = SField::eStatic;
                tmp = reRange.GetMatch(answer, 2);
                tmp.ToLongLong(&field.lower);
                tmp = reRange.GetMatch(answer, 3);
                tmp.ToLongLong(&field.higher);
                items.fields.push_back(field);
                continue;
            }

            // simple enum
            if (reEnum.Matches(answer))
            {
                SField field; wxString tmp;
                field.type = SField::eEnum;
                field.name = reEnum.GetMatch(answer, 1);
                tmp = reEnum.GetMatch(answer, 2);
                field.values = wxSplit(tmp, wxT('|'));
                items.fields.push_back(field);
                continue;
            }

            if (reString.Matches(answer))
            {
                SField field; wxString tmp;  size_t start; size_t len;
                field.name = reString.GetMatch(answer, 1);
                reString.GetMatch(&start, &len, 2);

                // simple string
                if (answer[start] != wxT('<'))
                {
                    field.type = SField::eString;
                    wxString descr(answer + start, len);
                    if(reIndex.Matches(descr))
                    { 
                        reIndex.GetMatch(descr, 1).ToLongLong(&field.higher);
                    }
                    items.fields.push_back(field);
                    continue;
                }

                // enum/range depending on mode
                field.type = SField::eEnum;
                wchar_t* p = answer + start;
                while(1)
                {
                    size_t e = wcscspn(p, wxT(")"));
                    if (p[e] == wxT(')')) ++e;
                    wchar_t c = p[e];
                    p[e] = 0;
                    bool match = reMode.Matches(p);
                    p[e] = c;
                    if (!match) break;
                    size_t mc = reMode.GetMatchCount();
                    reMode.GetMatch(&start, &len, 0);
                    wchar_t* nextp = p + start + len;
                    SField mode;
                    mode.name = reMode.GetMatch(p, 1);
                    reMode.GetMatch(&start, &len, 2);
                    if ((start != wxString::npos) && (p[start] == wxT('[')))
                    {
                        mode.type = SField::eRange;
                        tmp = reMode.GetMatch(p, 3);
                        tmp.ToLongLong(&mode.lower);
                        tmp = reMode.GetMatch(p, 4);
                        tmp.ToLongLong(&mode.higher);
                    }
                    reMode.GetMatch(&start, &len, 5);
                    if ((start != wxString::npos) && (p[start] == wxT('(')))
                    {
                        mode.type = SField::eEnum;
                        mode.values = wxSplit(reMode.GetMatch(p, 6), wxT('|'));
                    }
                    field.modes[mode.name] = mode;
                    p = nextp;
                }
                if (field.modes.size())
                {
                    wxString first_mode = field.modes.begin()->first;
                    // try to find if this mode was already defined
                    field.mode_field = -1;
                    for (auto f = items.fields.begin(); (field.mode_field == -1) && (f != items.fields.end()); ++f)
                    {
                        if (f->modes.size())
                        {
                            for (auto m : f->modes)
                            {
                                if (m.first == first_mode)
                                {
                                    field.mode_field = f - items.fields.begin();
                                    break;
                                }
                            }
                        }
                        else
                        {
                            for (auto& v : f->values)
                            {
                                if (v == first_mode)
                                {
                                    field.mode_field = f - items.fields.begin();
                                    break;
                                }
                            }
                        }
                    }
                    if (field.mode_field == -1)
                    {
                        // not found: this field defines it, so add all modes to the combobox
                        for (auto& mode : field.modes)
                        {
                            field.values.insert(field.values.end(), mode.second.values.begin(), mode.second.values.end());
                        }
                    }
                }
                items.fields.push_back(field);
                continue;
            }
        }

        // item definition starts with a digit
        if (iswdigit(answer[1]))
        {
            SItem item;
            item.values = wxSplit(answer, wxT('\t'));
            item.values.resize(items.fields.size());
            for (auto& v : item.values)
            {
                if (trimQuotes.Matches(v))
                {
                    v = trimQuotes.GetMatch(v, 1);
                }
                else
                {
                    v = v.Trim(false);
                    v = v.Trim(true);
                }
            }
            items.items.push_back(item);
        }
    }
}

const wchar_t* g_types[] = { L"none", L"output",L"input",L"pullup",L"pwm", L"adc" };

SItem::EItemType SItem::find_type(const wxString& name)
{
    for (size_t i = 0; i < countof(g_types); ++i)
    {
        if (!wcscmp(name, g_types[i]))
        {
            return SItem::EItemType(i);
        }
    }
    return SItem::EItemType(0);
}

void frameMain::ParsePins()
{
    wchar_t answer[64];
    wxRegEx pinState(wxT("pin\\[\\d+\\]=(\\d+)"), wxRE_EXTENDED);
    ParseItems(wxT("dpin"), m_pins);
    size_t mode_field = m_pins.fields.find(wxT("mode"));
    for (size_t ipin = 0; ipin < m_pins.items.size(); ++ipin)
    {
        SItem& pin = m_pins.items[ipin];
        pin.type = pin.find_type(m_pins.v(ipin, mode_field));
        WriteLine(wxString::Format(wxT("pin %lld\n"), ipin + 1));
        ReadLine(answer, countof(answer), 200);
        if (pinState.Matches(answer))
        {
            pinState.GetMatch(answer, 1).ToLongLong(&pin.state);
        }
    }
}

int64_t frameMain::SetPin(size_t ipin, size_t value)
{
    if (ipin >= m_pins.items.size()) return 0;
    SItem& pin = m_pins.items[ipin];
    wchar_t answer[64];
    wxRegEx pinState(wxT("pin\\[\\d+\\]=(\\d+)"), wxRE_EXTENDED);
    WriteLine(wxString::Format(wxT("pin %lld %lld\n"), ipin + 1, value));
    ReadLine(answer, countof(answer), 200);
    if (pinState.Matches(answer))
    {
        pinState.GetMatch(answer, 1).ToLongLong(&pin.state);
    }
    return pin.state;
}

void frameMain::ParseTasks()
{
    wchar_t answer[64];
    wxRegEx taskState(wxT("task\\[\\d+\\]=(\\d+)"), wxRE_EXTENDED);
    ParseItems(wxT("dtask"), m_tasks);
    size_t mode_index = m_tasks.fields.find(wxT("mode"));
    for (size_t itask = 0; itask < m_tasks.items.size(); ++itask)
    {
        SItem& task = m_tasks.items[itask];
        task.type = task.find_type(m_tasks.v(itask, mode_index));
        WriteLine(wxString::Format(wxT("task %lld\n"), itask + 1));
        ReadLine(answer, countof(answer), 200);
        if (taskState.Matches(answer))
        {
            int64_t state = 0;
            taskState.GetMatch(answer, 1).ToLongLong(&state);
            if (state == 2) state = 0; // 'finished' is the same as 'idle'
            if (state == 3) state = 2; // 'fired'
            task.state = state;
        }
    }
}

int64_t frameMain::SetTask(size_t itask, size_t value)
{
    if (itask >= m_tasks.items.size()) return 0;
    SItem& task = m_tasks.items[itask];
    wchar_t answer[64];
    wxRegEx taskState(wxT("task\\[\\d+\\]=(\\d+)"), wxRE_EXTENDED);
    if (value == 2) value = 3; // fired
    WriteLine(wxString::Format(wxT("task %lld %lld\n"), itask + 1, value));
    ReadLine(answer, countof(answer), 200);
    if (taskState.Matches(answer))
    {
        int64_t state = 0;
        taskState.GetMatch(answer, 1).ToLongLong(&state);
        if (state == 2) state = 0; // finished -> idle
        if (state == 3) state = 2; // fired
        task.state = state;
    }
    return task.state;
}

void frameMain::SetHalt(bool halt)
{
    WriteLine(wxString::Format(wxT("halt %d\n"),halt));
    m_halt = halt;
}

bool frameMain::GetHalt()
{
    return m_halt;
}

void frameMain::SendItems(SItems& new_items)
{
    wchar_t answer[2048];
    SItems* org_items = NULL;
    if (new_items.command == wxT("dpin")) org_items = &m_pins;
    else if (new_items.command == wxT("dtask")) org_items = &m_tasks;
    if (!org_items) return;

    for(int64_t diff = int64_t(org_items->items.size()) - int64_t(new_items.items.size()); diff > 0; --diff)
    {
        WriteLine(new_items.command + wxT(" -"));
        ReadLine(answer, 2048, 500); // pin/task deleted
        //while (ReadLine(answer, 2048, 100) > 0) {}
    }

    int halt = m_halt;

    // first set the names of the items, so they can be referred to in the second pass
    for (size_t item_index = 0; item_index < new_items.items.size(); ++item_index)
    {
        SItem& item = new_items.items[item_index];
        if (!item.changed) continue;
        SetHalt(true);

        if (item.values.size() > 1)
        {
            wxString line = new_items.command + wxT("\t") + item.values[0] + wxT("\tname\t") + item.values[1] + wxT("\n");
            WriteLine(line);
            ReadLine(answer, 2048, 500); // header row
            ReadLine(answer, 2048, 500); // status row
            //while (ReadLine(answer, 2048, 100) > 0) {}
        }
    }
    // now send the full item definitions
    for (size_t item_index = 0; item_index < new_items.items.size(); ++item_index)
    {
        SItem& item = new_items.items[item_index];
        if (!item.changed) continue;
        SetHalt(true);

        wxString line = new_items.command;
        for (size_t field_index = 0; field_index < new_items.fields.size(); ++field_index)
        {
            SField field = new_items.GetFieldInfo(item_index, field_index);
            line += wxT("\t");
            line += item.values[field_index];
        }
        line += wxT("\n");
        WriteLine(line);
        ReadLine(answer, 2048, 500); // line with header
        ReadLine(answer, 2048, 500); // line new status
        //while (ReadLine(answer, 2048, 100) > 0) {}
    }

    if (new_items.command == wxT("dpin")) ParsePins();
    else if (new_items.command == wxT("dtask")) ParseTasks();

    if (!halt) SetHalt(false);
}

void frameMain::WriteTasks(SItems& new_items)
{
    SendItems(new_items);
    WriteLine(wxT("write\n"));
    ReadAll();
}

bool frameMain::SaveItems(SItems& items)
{
    wxString message;
    wxString filter;
    wxString ext;
    if (items.command == wxT("dpin"))
    {
        message = wxT("Save pin definition");
        filter = wxT("digital timer pin files (*.nkdtp)|*.nkdtp");
        ext = wxT("nkdtp");
    }
    else
    {
        message = wxT("Save task definition");
        filter = wxT("digital timer task files (*.nkdtt)|*.nkdtt");
        ext = wxT("nkdtt");
    }

    wxString filename;
    while(1)
    {
        wxFileDialog dlg(this, message, wxT(""), filename, filter, wxFD_SAVE);
        if (dlg.ShowModal() == wxID_CANCEL) return false;

        wxFileName name(dlg.GetPath());
        if (name.GetExt() != ext)
        {
            name = name.GetFullPath() + wxT(".") + ext;
        }
        filename = name.GetFullPath();
        if (name.Exists())
        {
            int answer = wxMessageBox(wxString::Format(wxT("%s exists: overwrite ?"), name.GetName()), message, wxYES | wxNO | wxCANCEL);
            if (answer == wxCANCEL) return false;
            if (answer == wxNO) continue;
            wxRemoveFile(filename);
        }

        wxTextFile f(filename);
        if (!f.Create())
        {
            wxMessageBox(wxT("error creating the file"));
            continue;
        }

        f.AddLine(wxString::Format(wxT("%s *"), items.command));
        for (auto& item : items.items)
        {
            wxString line(items.command);
            line += "\t";
            line += wxJoin(item.values, wxT('\t'));
            f.AddLine(line);
        }
        f.Write();
        f.Close();
        return true;
    }
}

bool frameMain::LoadItems(SItems& items)
{
    wxString message;
    wxString filter;
    if (items.command == wxT("dpin"))
    {
        message = wxT("Load pin definition");
        filter = wxT("digital timer pin files (*.nkdtp)|*.nkdtp");
        items.fields = m_pins.fields;
    }
    else
    {
        message = wxT("Load task definition");
        filter = wxT("digital timer task files (*.nkdtt)|*.nkdtt");
        items.fields = m_tasks.fields;
    }

    while (1)
    {
        wxFileDialog  dlg(this, message, wxT(""), wxT(""), filter, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg.ShowModal() == wxID_CANCEL) return false;

        wxTextFile f(dlg.GetPath());
        if (!f.Open())
        {
            wxMessageBox(wxT("error creating the file"));
            continue;
        }

        wxString delAll = wxString::Format(wxT("%s *"), items.command);
        wxString line = f.GetFirstLine();
        for (wxString line = f.GetFirstLine(); !f.Eof(); line = f.GetNextLine())
        {
            if (line == delAll)
            {
                items.items.clear();
                continue;
            }
            SItem item;
            item.values = wxSplit(line, wxT('\t'));
            if (!item.values.size()) continue;
            item.values.erase(item.values.begin());
            items.items.push_back(item);
        }
        f.Close();

        if (items.command == wxT("dpin"))
        {
            size_t mode_field = items.fields.find(wxT("mode"));
            for (size_t ipin = 0; ipin < items.items.size(); ++ipin)
            {
                SItem& pin = items.items[ipin];
                pin.type = pin.find_type(items.v(ipin, mode_field));
            }
        }
        break;
    }
    return true;
}

bool appMain::OnInit()
{
    m_mode = MODE_CONTROL;
    if (!wxApp::OnInit())
        return false;

    wxInitAllImageHandlers();
    // change the current directory to the binary folder so the res/*.png can be loaded
    wxFileName f(wxStandardPaths::Get().GetExecutablePath());
    SetCurrentDirectory(f.GetPath());

    // this will make that the GetConfigDir() returns %PROGRAMDATA%\Nikon\NiVerDig
    SetVendorName(wxT("Nikon"));
    wxStandardPathsBase::Get().UseAppInfo(wxStandardPathsBase::AppInfo_VendorName | wxStandardPathsBase::AppInfo_AppName);

    wxFrame* frame = new frameMain(NULL,
        wxID_ANY,
        "NiVerDig: Versatile Digital Controller and Scope",
        wxDefaultPosition,
        wxWindow::FromDIP(wxSize(800, 600), NULL));

    if (m_showCmd != -1)
    {
        frame->MSWSetShowCommand(m_showCmd);
    }
    frame->Show();
    
    return true;
}

