#pragma once

#include "resource.h"


struct SField
{
    typedef enum {eNone, eStatic, eString, eRange, eEnum } EFieldType;
    SField() : type(eNone), lower(0), higher(0), mode_field(-1) {}
    SField(wxString n, EFieldType t) : name(n), type(t) {}

    wxString      name;
    EFieldType    type;
    wxString      value;  // eString
    int64_t       lower;  // eRange
    int64_t       higher; // eRange
    wxArrayString values; // eEnum
    size_t        mode_field; // field index that determines the mode of this field
    std::map<wxString, SField> modes; // allowed lower/higher/values for different modes
};

struct SFields : std::vector<SField>
{
    size_t find(const wxString& name)
    {
        for (size_t i = 0; i < size(); ++i)
        {
            if (at(i).name == name)
            {
                return i;
            }
        }
        return -1;
    }
};

struct SItem
{
    typedef enum { eNoneType, eOutputPin, eInputPin, ePullupPin, ePwmPin, eAdcPin} EItemType;
    static EItemType find_type(const wxString& name);
    size_t        type;
    int64_t       state;
    wxArrayString values; // for each field the value

    // for the control page
    wxStaticText* label;
    wxControl*    control;

    // for the setup page
    std::vector<wxSizer*> sizers;
    std::vector<wxControl*> controls;
    bool changed;

    SItem() : label(NULL), control(NULL), type(0), state(0), changed(false) {}
    SItem(wxString index, wxString name, EItemType type) : label(NULL), control(NULL), type(type), state(0), changed(false) { values.push_back(index), values.push_back(name); }
};

struct SItems
{
    wxString           command;
    SFields            fields;
    std::vector<SItem> items;
    long               dataBits;
    void clear()
    {
        fields.clear();
        items.clear();
        dataBits = 8;
    }
    wxString v(size_t index, const wxString& field)
    {
        if (index >= items.size()) return wxEmptyString;
        for (size_t f = 0; f < fields.size(); ++f)
        {
            if (fields[f].name == field)
            {
                SItem& item = items[index];
                if (f >= item.values.size()) return wxEmptyString;
                return item.values[f];
            }
        }
        return wxEmptyString;
    }
    wxString v(size_t index, size_t field)
    {
        if (index >= items.size()) return wxEmptyString;
        SItem& item = items[index];
        if (field >= item.values.size()) return wxEmptyString;
        return item.values[field];
    }
    wxString findMode(size_t item_index, size_t field_index)
    {
        size_t mode_field = fields[field_index].mode_field;
        if (mode_field == -1) return wxEmptyString;
        wxString mode = items[item_index].values[mode_field];
        for (auto i : fields[mode_field].modes)
        {
            SField& f = i.second;
            for (size_t j = 0; j < f.values.size(); ++j)
            {
                if (f.values[j] == mode)
                {
                    return i.first;
                }
            }
        }
        return mode;
    }
    SField GetFieldInfo(size_t item_index, size_t field_index)
    {
        SField field;
        size_t mode_field = fields[field_index].mode_field;
        if ((mode_field == -1) || (field_index <= mode_field) || !fields[field_index].modes.size())
        {
            field = fields[field_index];
            return field;
        }
        wxString mode = findMode(item_index, field_index);
        if (!fields[field_index].modes.count(mode)) return field;
        return fields[field_index].modes[mode];
    }
    bool LoadFromFile(HANDLE data, size_t& lastsample)
    {
        clear();
        fields.push_back(SField(wxT("index"), SField::eString));
        fields.push_back(SField(wxT("name"), SField::eString));

        // try to read the channel info from the appendix
        while (1)
        {
            LARGE_INTEGER len;
            LARGE_INTEGER null;
            len.QuadPart = null.QuadPart = 0;
            SetFilePointerEx(data, null, &len, FILE_END);
            if (len.QuadPart < 8) break;
            LARGE_INTEGER pos;
            LARGE_INTEGER pos2;
            SetFilePointer(data, -8, 0, FILE_END);
            DWORD read = 0;
            ReadFile(data, &pos.QuadPart, 8, &read, NULL);
            if (!read) break;
            SetFilePointerEx(data, pos, &pos2, FILE_BEGIN);
            if (pos.QuadPart != pos2.QuadPart) break;
            size_t buf_len = len.QuadPart - pos.QuadPart;
            if (buf_len >= 8192) break;
            wchar_t buf[4096];
            ReadFile(data, buf, buf_len, &read, NULL);
            if (read != buf_len) break;
            buf[buf_len / 2] = 0;
            wxString str(buf);
            wxArrayString lines = wxSplit(str, wxT('\n'));
            for (size_t i = 0; i < lines.size(); ++i)
            {
                wxArrayString fields = wxSplit(lines[i], wxT('\t'));
                if (fields.size() < 1) break;
                if (fields[0] == wxT("pin"))
                {
                    if (fields.size() != 4) break;
                    wxLongLong_t index = -1;
                    fields[1].ToLongLong(&index);
                    if (index != items.size()) break;
                    SItem::EItemType type;
                    fields[3].ToLong((long*)&type);
                    items.push_back(SItem(fields[1], fields[2], type));
                }
                else if (fields[0] == wxT("bits"))
                {
                    if (fields.size() != 2) break;
                    if(fields[1].ToLong(&dataBits))
                    {
                        if (dataBits < 8) dataBits = 8;
                        if (dataBits > 16) dataBits = 16;
                    }
                }
            }
            lastsample = pos.QuadPart;
            break;
        }
        if (!items.size())
        {
            // read the channels from the first samples
            SetFilePointer(data, 0, NULL, FILE_BEGIN);
            fileSample<byte> sample = { 0 };
            for (size_t i = 0; ReadFile(data, &sample, sizeof(sample), NULL, NULL); ++i)
            {
                if (sample.channel != i) break;
                wxString index = wxString::Format(wxT("%lld"), i + 1);
                items.push_back(SItem(index, index, SItem::eInputPin));
            }
        }
        SetFilePointer(data, 0, NULL, FILE_BEGIN);
        return items.size() > 0;
    }
};

class itemData : public wxObject
{
public:
    size_t item_index;
    size_t field_index;
    itemData(size_t ii, size_t fi) : item_index(ii), field_index(fi) {}
};

class frameMain : public formMain
{
public:
    frameMain(wxWindow* parent,
        wxWindowID id,
        const wxString& title,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxDEFAULT_FRAME_STYLE | wxSUNKEN_BORDER);
    ~frameMain();

    wxString      m_profileName;
    wxFileConfig* m_profile;
    wxString      m_portName;
    wxString      m_lastPortName;
    NKCOMPORT*    m_port;

    wxPanel*      m_panel;
    int           m_epanel;

    SItems        m_pins;
    SItems        m_tasks;
    long          m_halt;
    long          m_adc_res;

    wxLogFile       m_log;
    wxIndexTextFile m_ilog;

    HDEVNOTIFY      dev_notify;

    WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam)
    {
        if (message == WM_DEVICECHANGE) return OnDeviceChange(wParam, lParam);
        return formMain::MSWWindowProc(message, wParam, lParam);
    }
    void SetTitle(wxString port, wxString answer);
    bool IsConnected();
    void ParseItems(wxString command, SItems& items);
    void ParsePins();
    int64_t SetPin(size_t pin, size_t value);
    void ParseTasks();
    int64_t SetTask(size_t task, size_t value);
    void SetHalt (bool halt);
    bool GetHalt();
    long ReadLine(wchar_t * answer, long size, unsigned long timeout);
    void ReadAll();
    long WriteLine(const wchar_t* line);
    void SendItems(SItems& items);
    bool SaveItems(SItems& items);
    void WriteTasks(SItems& items);
    bool LoadItems(SItems& items);
    void SetStatus(wxString str);
    void SetPort(wxString port);
    void ClosePort(void);

    void MSWSetShowCommand(WXUINT showCmd)
    {
        m_showCmd = showCmd;
    }

private:
    std::map<wxString,wxString> m_ports;
    std::vector<wxString> m_portIds;

    wxDECLARE_EVENT_TABLE();
    void OnMenuCommandPort(wxCommandEvent& evt);
    void OnMenuCommandUpload(wxCommandEvent& evt);
    LRESULT OnDeviceChange(WPARAM wParam, LPARAM lParam);
    void m_toolPortOnAuiToolBarToolDropDown(wxAuiToolBarEvent& event);
    void m_toolControlOnToolClicked(wxCommandEvent& event);
    void m_toolPinsOnToolClicked(wxCommandEvent& event);
    void m_toolTasksOnToolClicked(wxCommandEvent& event);
    void m_toolScopeOnToolClicked(wxCommandEvent& event);
    void m_toolConsoleOnToolClicked(wxCommandEvent& event);
    void m_toolHelpOnToolClicked(wxCommandEvent& event);
    void formMainOnClose(wxCloseEvent& event);

    void StoreSettings();
    void RestoreSettings();

    bool ShowPanel(int panel, bool refresh, bool allow_veto = true);
    void RegisterDeviceEventNotifications();
};

size_t GetCurrentTimeInMs();
size_t GetCurrentFileTime();
wxString FormatLocalFileTime(size_t t);
wxString LogFormatLocalFileTimeUs(size_t t);

wxPanel* CreateControlPanel(frameMain * parent);
wxPanel* CreateSetupPanel(frameMain* parent, SItems& items);
wxPanel* CreateConsolePanel(frameMain* parent);
wxPanel* CreateScopePanel(frameMain* parent, wxString file, EMODE mode);

class nkDigTimPanel
{
public:
    virtual bool CanClosePanel(wxFrame * frame, bool allow_veto) = 0;
};