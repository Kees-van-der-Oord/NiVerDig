#include "framework.h"

class panelConsole : public formConsole, public nkDigTimPanel
{
public:
	panelConsole(frameMain* main)
		: formConsole(main->m_mainPanel)
		, m_main(main)
		, m_scroll_scale(1)
		, m_visible_lines(1)
		, m_begin_sel(0) 
		, m_end_sel(0)
		, m_top_line(-1)
	{
		m_command->SetFocus();

		unsigned int tab = 48;
		::SendMessage(m_logEdit->GetHWND(), EM_SETTABSTOPS, 1, (LPARAM)&tab);

		m_logScroll->Bind(wxEVT_SCROLL_TOP, &panelConsole::OnScrollTop, this);
		m_logScroll->Bind(wxEVT_SCROLL_BOTTOM, &panelConsole::OnScrollBottom, this);
		m_logScroll->Bind(wxEVT_SCROLL_LINEUP, &panelConsole::OnScrollLineUp, this);
		m_logScroll->Bind(wxEVT_SCROLL_LINEDOWN, &panelConsole::OnScrollLineDown, this);
		m_logScroll->Bind(wxEVT_SCROLL_PAGEUP, &panelConsole::OnScrollPageUp, this);
		m_logScroll->Bind(wxEVT_SCROLL_PAGEDOWN, &panelConsole::OnScrollPageDown, this);
		m_logScroll->Bind(wxEVT_SCROLL_THUMBTRACK, &panelConsole::OnScrollThumbTrack, this);

		Update(true);

		m_timer.Bind(wxEVT_TIMER, &panelConsole::OnTimer, this);
		m_timer.Start(100);

	}

	void OnTimer(wxTimerEvent& event)
	{
		m_main->ReadAll();
		Update(false);
	}

	void m_logEditOnSize(wxSizeEvent& event)
	{ 
		event.Skip(); 
		Update(true);
	}

	void Update(bool forceUpdateEdit)
	{
		bool changed = m_main->m_ilog.Update();
		if (changed || forceUpdateEdit)
		{
			UpdateEdit();
		}
	}

	void UpdateEdit()
	{
		wxSize size = m_logEdit->GetClientSize();
		//m_visible_lines = m_logEdit->XYToPosition(0, size.y);
		wxClientDC dc(m_logEdit);
		wxSize txtSize = GetTextExtent(wxT("A"));
		m_visible_lines = size_t(size.y / txtSize.y) - 1;
		if (!m_visible_lines) return;

		// scrollbar
		size_t line_count = m_main->m_ilog.m_line_count;
		if (!line_count) return;
		m_scroll_scale = ((line_count - 1) / 0x7FFF) + 1;
		int pageSize = m_visible_lines / m_scroll_scale;
		if (pageSize < 1) pageSize = 1;
		int64_t top_line = m_top_line;
		if (top_line == -1) top_line = line_count - m_visible_lines;
		if (top_line < 0) top_line = 0;
		m_logScroll->SetScrollbar(top_line / m_scroll_scale, pageSize, int(line_count / m_scroll_scale), pageSize, true);

		// edit
		wxString text = m_main->m_ilog.GetText(top_line, top_line + m_visible_lines);
		m_logEdit->ChangeValue(text);
		WPARAM len = text.length();
		::SendMessage(m_logEdit->GetHWND(), EM_SETSEL, len, len);
		::SendMessage(m_logEdit->GetHWND(), EM_SCROLLCARET, 0, 0);
	}

	void m_commandOnText(wxCommandEvent& event)
	{ 
		event.Skip(); 
		wxString command = m_command->GetValue();
		int64_t pos = command.find(wxT('\n'));
		if (pos == wxString::npos) return;
		if (pos != (command.length() - 1))
		{
			command.erase(pos);
			command += wxT('\n');
		}
		m_command->ChangeValue(wxEmptyString);

		m_main->WriteLine(command);
		m_main->ReadAll();
	}

	bool CanClosePanel(wxFrame * mainFrame)
	{
		m_main->WriteLine(wxT("verbose 0\n"));
		return true;
	}

	void OnScrollTop(wxScrollEvent& evt)
	{
		SetCursorLine(0);
	}

	void OnScrollBottom(wxScrollEvent& evt)
	{
		SetCursorLine(-1);
	}

	void OnScrollLineUp(wxScrollEvent& evt)
	{
		size_t top_line = m_top_line - 1;
		if (top_line > m_top_line) top_line = 0;
		SetCursorLine(top_line);
	}

	void OnScrollLineDown(wxScrollEvent& evt)
	{
		SetCursorLine(m_top_line + 1);
	}

	void OnScrollPageUp(wxScrollEvent& evt)
	{
		size_t top_line = m_top_line - m_visible_lines;
		if (top_line > m_top_line) top_line = 0;
		SetCursorLine(top_line);
	}

	void OnScrollPageDown(wxScrollEvent& evt)
	{
		SetCursorLine(m_top_line + m_visible_lines);
	}

	void OnScrollThumbTrack(wxScrollEvent& evt)
	{
		SetCursorLine(evt.GetPosition() * m_scroll_scale);
	}

	void SetCursorLine(size_t top_line)
	{
		if ((top_line + m_visible_lines) > m_main->m_ilog.m_line_count)
		{
			top_line = -1;
		}
		m_top_line = top_line;
		UpdateEdit();
	}

	void m_buttonSaveOnButtonClick(wxCommandEvent& event) 
	{ 
		event.Skip(); 
		wxString filename;
		wxString ext = wxT("log");
		wxString message = wxT("Save Log");
		while (1)
		{
			wxFileDialog dlg(this, message, wxT(""), filename, wxT("digital timer log files (*.log)|*.log"), wxFD_SAVE);
			if (dlg.ShowModal() == wxID_CANCEL) return;

			wxFileName name(dlg.GetPath());
			if (name.GetExt() != ext)
			{
				name = name.GetFullPath() + wxT(".") + ext;
			}
			filename = name.GetFullPath();
			if (name.Exists())
			{
				int answer = wxMessageBox(wxString::Format(wxT("%s exists: overwrite ?"), name.GetName()), message, wxYES | wxNO | wxCANCEL);
				if (answer == wxCANCEL) return;
				if (answer == wxNO) continue;
				wxRemoveFile(filename);
			}
			if (!wxCopyFile(m_main->m_log.m_name, filename, true))
			{
				int answer = wxMessageBox(wxString::Format(wxT("saving to % failed: try again ?"), name.GetName()), message, wxYES | wxNO | wxCANCEL);
				if (answer != wxYES) return;
				continue;
			}
			return;
		}
	}

	frameMain* m_main;
	wxTimer    m_timer;
	size_t     m_scroll_scale;
	size_t     m_visible_lines;
	size_t     m_begin_sel;
	size_t     m_end_sel;
	size_t     m_top_line;

};

wxPanel* CreateConsolePanel(frameMain* parent)
{
	return new panelConsole(parent);
}