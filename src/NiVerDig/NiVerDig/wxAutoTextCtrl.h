#pragma once

#include <wx/textctrl.h>

class wxAutoTextCtrl : public wxTextCtrl
{
public:
    wxAutoTextCtrl() { }
    wxAutoTextCtrl(wxWindow* parent, wxWindowID id,
        const wxString& value = wxEmptyString,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = 0,
        const wxValidator& validator = wxDefaultValidator,
        const wxString& name = wxASCII_STR(wxTextCtrlNameStr))
    {
        Create(parent, id, value, pos, size, style, validator, name);
    }

    virtual WXDWORD MSWGetStyle(long flags, WXDWORD* exstyle = NULL) const wxOVERRIDE
    {
        WXDWORD style = wxTextCtrl::MSWGetStyle(flags, exstyle);
        // wxTextCtrl::MSWGetStyle adds WS_VSCROLL when wxTE_MULTILINE is specified: not needed for single-line text control that can accept ENTER
#ifndef WS_VSCROLL
#define WS_VSCROLL 0x00200000L
#endif
        style &= ~WS_VSCROLL; 
        return style;
    }

};
