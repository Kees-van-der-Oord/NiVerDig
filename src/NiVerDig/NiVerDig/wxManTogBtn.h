#pragma once

// toggle button that does not toggle automatic and supports 3 bitmaps

class wxManualToggleButton : public wxBitmapToggleButton
{
public:
    // construction/destruction
    wxManualToggleButton() {}
    wxManualToggleButton(wxWindow* parent,
        wxWindowID id,
        const wxBitmapBundle& label,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = 0,
        const wxValidator& validator = wxDefaultValidator,
        const wxString& name = wxASCII_STR(wxCheckBoxNameStr))
        : wxBitmapToggleButton(parent, id, label, pos, size, style, validator, name)
        , m_stateEx(0)
    {
    }

    bool MSWCommand(WXUINT param, WXWORD WXUNUSED(id))
    {
        if (param != BN_CLICKED && param != BN_DBLCLK)
            return false;

        // first update the value so that user event handler gets the new
        // toggle button value

        // ownerdrawn buttons don't manage their state themselves unlike usual
        // auto checkboxes so do it ourselves in any case
        //m_state = !m_state; /* kees: commented out: don't switch the state automatically */

        wxCommandEvent event(wxEVT_TOGGLEBUTTON, m_windowId);
        event.SetInt(GetValue());
        event.SetEventObject(this);
        ProcessCommand(event);
        return true;
    }

    long m_stateEx;  // also support 3rd state
    wxBitmapBundle m_bm3rd;
    wxBitmapBundle m_bmPressed;

    void SetBitmapPressed(const wxBitmapBundle& bitmap)
    {
        DoSetBitmap(bitmap, State_Pressed);
        m_bmPressed = bitmap;
    }

    void SetBitmap3rd(const wxBitmapBundle& bitmap)
    {
        m_bm3rd = bitmap;
    }

    void SetValue(long val)
    {
        m_stateEx = val;
        wxBitmapToggleButton::SetValue(val ? true : false);
        if (val)
        {
            wxBitmapToggleButton::SetBitmapPressed(val == 2 ? m_bm3rd : m_bmPressed);
        }
    }

    long GetValue()
    {
        return m_stateEx;
    }

};
