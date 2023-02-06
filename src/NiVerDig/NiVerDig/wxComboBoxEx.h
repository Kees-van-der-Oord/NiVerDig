#pragma once

// wxComboBoxEx: draws a white background
class wxComboBoxEx : public wxComboBox
{
public:

	wxComboBoxEx() {}

	wxComboBoxEx(wxWindow* parent, wxWindowID id,
		const wxString& value = wxEmptyString,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		int n = 0, const wxString choices[] = NULL,
		long style = 0,
		const wxValidator& validator = wxDefaultValidator,
		const wxString& name = wxASCII_STR(wxComboBoxNameStr))
		: wxComboBox(parent, id, value, pos, size, n, choices, style, validator, name)
	{
	}

	wxComboBoxEx(wxWindow* parent, wxWindowID id,
		const wxString& value,
		const wxPoint& pos,
		const wxSize& size,
		const wxArrayString& choices,
		long style = 0,
		const wxValidator& validator = wxDefaultValidator,
		const wxString& name = wxASCII_STR(wxComboBoxNameStr))
		: wxComboBox(parent, id, value, pos, size, choices, style, validator, name)
	{
	}

	WXLRESULT MSWWindowProc(WXUINT message, WXWPARAM wParam, WXLPARAM lParam)
	{
		switch (message)
		{
		case WM_PAINT:
		{
			HWND hwnd = GetHWND();
			DWORD style = GetWindowLongPtr(hwnd, GWL_STYLE);
			if (!(style & CBS_DROPDOWNLIST))
				break;

			RECT rc;
			::GetClientRect(hwnd, &rc);

			PAINTSTRUCT ps;
			auto hdc = BeginPaint(hwnd, &ps);
			auto bkcolor = RGB(255, 255, 255);
			auto brush = CreateSolidBrush(bkcolor);
			auto pen = CreatePen(PS_SOLID, 1, RGB(128, 128, 128));
			auto oldbrush = SelectObject(hdc, brush);
			auto oldpen = SelectObject(hdc, pen);
			SelectObject(hdc, (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0));
			SetBkColor(hdc, bkcolor);
			SetTextColor(hdc, RGB(0, 0, 0));

			Rectangle(hdc, 0, 0, rc.right, rc.bottom);

			RECT rcDropDown = rc;
			rcDropDown.left = rc.right - (rc.bottom - rc.top);
			rc.right = rcDropDown.left;
			InflateRect(&rcDropDown, -2, -2);
			DrawFrameControl(hdc, &rcDropDown, DFC_SCROLL, DFCS_SCROLLDOWN);

			if (GetFocus() == hwnd)
			{
				RECT temp = rc;
				InflateRect(&temp, -2, -2);
				DrawFocusRect(hdc, &temp);
			}

			int index = SendMessage(hwnd, CB_GETCURSEL, 0, 0);
			if (index >= 0)
			{
				size_t buflen = SendMessage(hwnd, CB_GETLBTEXTLEN, index, 0);
				TCHAR* buf = new TCHAR[(buflen + 1)];
				SendMessage(hwnd, CB_GETLBTEXT, index, (LPARAM)buf);
				rc.left += 5;
				DrawText(hdc, buf, -1, &rc, DT_EDITCONTROL | DT_LEFT | DT_VCENTER | DT_SINGLELINE);
				delete[]buf;
			}

			SelectObject(hdc, oldpen);
			SelectObject(hdc, oldbrush);
			DeleteObject(brush);
			DeleteObject(pen);

			EndPaint(hwnd, &ps);
			return 0;
		}
		}
		return wxComboBox::MSWWindowProc(message, wParam, lParam);
	}
};

