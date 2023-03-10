#include "framework.h"

class panelControl : public formControl
{
public:
	enum {ID_PIN=1000, ID_TASK=2000 };
	panelControl(frameMain* main)
		: formControl(main->m_mainPanel)
		, m_main(main)
		, m_halt(NULL)
	{
		UpdatePins();
		UpdateTasks();
		Layout();
	}

	void UpdatePins()
	{
		m_pinSizer->Clear(true);

		if (!NkComPort_IsConnected(m_main->m_port))
		{
			wxStaticText * text = new wxStaticText(m_panelPins, wxID_ANY, wxT("not connected"), wxDefaultPosition, wxDefaultSize, 0);
			text->Wrap(-1);
			m_pinSizer->Add(text, 0, wxALL, 5);
			return;
		}

		m_main->ParsePins();

		wxFileName f(wxStandardPaths::Get().GetExecutablePath());
		wxString bin(f.GetPath() + wxT("/"));
		SItems& pins(m_main->m_pins);
		for (size_t ipin = 0; ipin < pins.items.size(); ++ipin)
		{
			SItem& pin = pins.items[ipin];

			wxBoxSizer* vsizer = new wxBoxSizer(wxVERTICAL); // for the two rows: first text, second buttons

			// text row
			wxBoxSizer* hsizer = new wxBoxSizer(wxHORIZONTAL);
			hsizer->Add(0, 0, 1, wxEXPAND, 0);
			pin.label = new wxStaticText(m_panelPins, wxID_ANY, pins.v(ipin, L"name"), wxDefaultPosition, wxDefaultSize, 0);
			pin.label->Wrap(-1);
			hsizer->Add(pin.label, 0, wxLEFT | wxRIGHT, 5);
			hsizer->Add(0, 0, 1, wxEXPAND, 5);
			vsizer->Add(hsizer, 0, wxEXPAND, 0);

			// button row
			hsizer = new wxBoxSizer(wxHORIZONTAL);
			hsizer->Add(0, 0, 1, wxEXPAND, 0);
			wxControl * control = NULL;
			if (pin.type == SItem::ePwmPin)
			{
				wxComboBoxEx * cb = new wxComboBoxEx(m_panelPins,ID_PIN + ipin, wxEmptyString, wxDefaultPosition, wxSize(48,-1), 0, NULL, wxCB_READONLY);
				size_t field_index = pins.fields.find(wxT("init"));
				cb->Connect(wxEVT_COMMAND_COMBOBOX_SELECTED, wxCommandEventHandler(panelControl::m_OnCombobox), new itemData(ipin, field_index), this);
				if (field_index != -1)
				{
					SField field = pins.GetFieldInfo(ipin,field_index);
					for (int64_t state = field.lower; state <= field.higher; ++state)
					{
						int i = cb->Append(wxString::Format(wxT("%lld"), state));
						if (state == pin.state)
						{
							cb->SetSelection(i);
						}
					}
				}
				control = cb;
			}
			else
			{
				wxManualToggleButton* button = new wxManualToggleButton(m_panelPins, ID_PIN + ipin, wxNullBitmap, wxDefaultPosition, wxSize(32, 32), wxBU_AUTODRAW | wxBORDER_NONE);
				if ((pin.type >= SItem::eInputPin) && (pin.type <= SItem::ePullupPin))
				{
					button->SetBitmap(wxBitmap(bin + wxT("res/BlackCircle.png"), wxBITMAP_TYPE_ANY));
					button->SetBitmapPressed(wxBitmap(bin + wxT("res/GreenCircle.png"), wxBITMAP_TYPE_ANY));
				}
				else if (pin.type == SItem::eOutputPin)
				{
					button->SetBitmap(wxBitmap(bin + wxT("res/BlackRect.png"), wxBITMAP_TYPE_ANY));
					button->SetBitmapPressed(wxBitmap(bin + wxT("res/GreenRect.png"), wxBITMAP_TYPE_ANY));
				}
				button->Connect(wxEVT_TOGGLEBUTTON, wxCommandEventHandler(panelControl::m_pinToggle), NULL, this);
				button->SetValue(pin.state);
				control = button;
			}
			if(control)	hsizer->Add(control, 0, wxALL, 5);
			pin.control = control;
			hsizer->Add(0, 0, 1, wxEXPAND, 5);
			vsizer->Add(hsizer, 0, wxEXPAND, 0);

			control->SetBackgroundColour(m_main->GetBackgroundColour());

			m_pinSizer->Add(vsizer, 0, 0, 5);
		}
	}

	void UpdateTasks()
	{
		m_taskSizer->Clear(true);

		if (!NkComPort_IsConnected(m_main->m_port))
		{
			return;
		}

		wxFileName f(wxStandardPaths::Get().GetExecutablePath());
		wxString bin(f.GetPath() + wxT("/"));

		m_main->ParseTasks();

		m_halt = new wxBitmapToggleButton(m_panelTasks, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(48, 48), wxBU_AUTODRAW | wxBORDER_NONE);
		m_halt->SetBitmap(wxBitmap(bin + wxT("res/TaskStopAll.png"), wxBITMAP_TYPE_ANY));
		m_halt->SetBitmapPressed(wxBitmap(bin + wxT("res/TaskStopAllDown.png"), wxBITMAP_TYPE_ANY));
		m_halt->SetBackgroundColour(m_main->GetBackgroundColour());
		m_taskSizer->Add(m_halt, 0, 0, 5);
		m_halt->Connect(wxEVT_TOGGLEBUTTON, wxCommandEventHandler(panelControl::m_buttonHalt), NULL, this);
		m_halt->SetValue(m_main->GetHalt());

		SItems& tasks(m_main->m_tasks);
		for (size_t itask = 0; itask < tasks.items.size(); ++itask)
		{
			SItem& task = tasks.items[itask];

			wxBoxSizer* vsizer = new wxBoxSizer(wxVERTICAL); // for the two rows: first text, second buttons

			// text row
			wxBoxSizer* hsizer = new wxBoxSizer(wxHORIZONTAL);
			hsizer->Add(0, 0, 1, wxEXPAND, 0);
			task.label = new wxStaticText(m_panelTasks, wxID_ANY, tasks.v(itask, L"name"), wxDefaultPosition, wxDefaultSize, 0);
			task.label->Wrap(-1);
			hsizer->Add(task.label, 0, wxLEFT | wxRIGHT, 5);
			hsizer->Add(0, 0, 1, wxEXPAND, 0);
			vsizer->Add(hsizer, 0, wxEXPAND, 0);

			// button row
			hsizer = new wxBoxSizer(wxHORIZONTAL);
			hsizer->Add(0, 0, 1, wxEXPAND, 0);
			wxManualToggleButton* control = new wxManualToggleButton(m_panelTasks, ID_TASK + itask, wxNullBitmap, wxDefaultPosition, wxSize(55, 32), wxBU_AUTODRAW | wxBORDER_NONE);
			hsizer->Add(control, 0, wxALL, 5);
			hsizer->Add(0, 0, 1, wxEXPAND, 0);
			vsizer->Add(hsizer, 0, wxEXPAND, 0);

			task.control = control;
			control->SetBitmap(wxBitmap(bin + wxT("res/TaskStateIdle.png"), wxBITMAP_TYPE_ANY));
			control->SetBitmapPressed(wxBitmap(bin + wxT("res/TaskStateArmed.png"), wxBITMAP_TYPE_ANY));
			control->SetBitmap3rd(wxBitmap(bin + wxT("res/TaskStateFired.png"), wxBITMAP_TYPE_ANY));
			control->SetBackgroundColour(m_main->GetBackgroundColour());
			control->Connect(wxEVT_LEFT_UP, wxMouseEventHandler(panelControl::m_taskOnLeftUp), NULL, this);

			control->SetValue(task.state);

			m_taskSizer->Add(vsizer, 0, 0, 5);
		}
	}

	frameMain* m_main;

	void m_pinToggle(wxCommandEvent& event)
	{
		//event.Skip();
		wxManualToggleButton* button = dynamic_cast<wxManualToggleButton*>(event.GetEventObject());
		if (!button) return;
		SItems& pins(m_main->m_pins);
		size_t ipin = size_t(event.GetId()) - ID_PIN;
		if (ipin >= pins.items.size()) return;
		SItem & pin(pins.items[ipin]);
		if (pin.type == SItem::eOutputPin)
		{
			m_main->SetPin(ipin, !pin.state);
		}
		button->SetValue(pin.state);
	}

	void m_taskOnLeftUp(wxMouseEvent& event) 
	{
		event.Skip(); 
		wxManualToggleButton* button = dynamic_cast<wxManualToggleButton*>(event.GetEventObject());
		if (!button) return;
		SItems& tasks(m_main->m_tasks);
		size_t itask = button->GetId() - ID_TASK;
		if (itask >= tasks.items.size()) return;
		wxRect rc = button->GetRect();
		wxPoint pnt = event.GetPosition();
		long state = (pnt.x * 3 + 2) / rc.GetWidth();
		if (state > 2) state = 2;
		if (state < 0) state = 0;
		m_main->SetTask(itask, state);
		SItem& task(tasks.items[itask]);
		button->SetValue(task.state);
	}

	void m_buttonHalt(wxCommandEvent& event)
	{
		event.Skip();
		m_main->SetHalt(m_halt->GetValue());
	}

	void m_OnCombobox(wxCommandEvent& event)
	{
		event.Skip();
		itemData* id = dynamic_cast<itemData*>(event.GetEventUserData());
		if (!id) return;
		SItems& pins(m_main->m_pins);
		if (id->field_index > pins.fields.size()) return;
		if (id->item_index > pins.items.size()) return;

		SItem& pin = pins.items[id->item_index];
		SField field = pins.GetFieldInfo(id->item_index, id->field_index);

		wxComboBoxEx* cb = dynamic_cast<wxComboBoxEx*>(event.GetEventObject());
		if (!cb) return;

		if (pin.type == SItem::ePwmPin)
		{
			int64_t value = int64_t(cb->GetSelection()) + field.lower;
			m_main->SetPin(id->item_index, value);
			cb->SetSelection(int(pin.state + field.lower));
		}
	}

	wxBitmapToggleButton* m_halt;
};

wxPanel* CreateControlPanel(frameMain* parent)
{
	return new panelControl(parent);
}