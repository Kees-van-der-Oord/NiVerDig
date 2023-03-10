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
    SItem(wxString index, wxString name) : label(NULL), control(NULL), type(0), state(0), changed(false) { values.push_back(index), values.push_back(name); }
};

struct SItems
{
    wxString           command;
    SFields            fields;
    std::vector<SItem> items;
    void clear()
    {
        fields.clear();
        items.clear();
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
    bool LoadFromFile(HANDLE data)
    {
        clear();
        fields.push_back(SField(wxT("index"), SField::eString));
        fields.push_back(SField(wxT("name"), SField::eString));
        fileSample sample = { 0 };
        for (size_t i = 0; ReadFile(data, &sample, sizeof(sample), NULL, NULL); ++i)
        {
            if (sample.channel != i) break;
            wxString index = wxString::Format(wxT("%lld"), i + 1);
            items.push_back(SItem(index, index));
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
    NKCOMPORT*    m_port;

    wxPanel*      m_panel;
    int           m_epanel;

    SItems        m_pins;
    SItems        m_tasks;
    long          m_halt;

    wxLogFile       m_log;
    wxIndexTextFile m_ilog;

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
    void ParseStateChange(wchar_t* line);
    void SendItems(SItems& items);
    bool SaveItems(SItems& items);
    void WriteTasks(SItems& items);
    bool LoadItems(SItems& items);
    void SetStatus(wxString str);
    void SetPort(wxString port);

private:
    std::map<wxString,wxString> m_ports;
    std::vector<wxString> m_portIds;
    wxTimer m_timer;

    wxDECLARE_EVENT_TABLE();
    void OnMenuCommandPort(wxCommandEvent& evt);
    void OnMenuCommandUpload(wxCommandEvent& evt);
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

    void ShowPanel(int panel, bool refresh);
    void OnTimer(wxTimerEvent& event);
};

size_t GetCurrentTimeInMs();
size_t GetCurrentFileTime();
wxString FormatLocalFileTime(size_t t);
wxString LogFormatLocalFileTimeUs(size_t t);

wxPanel* CreateControlPanel(frameMain * parent);
wxPanel* CreateSetupPanel(frameMain* parent, SItems& items);
wxPanel* CreateConsolePanel(frameMain* parent);
wxPanel* CreateScopePanel(frameMain* parent, wxString file);

class nkDigTimPanel
{
public:
    virtual bool CanClosePanel() = 0;
};