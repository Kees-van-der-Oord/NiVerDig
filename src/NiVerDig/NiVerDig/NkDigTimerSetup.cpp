#include "framework.h"

void NkAddAllCombinations(wxArrayString& dst, wxArrayString& src, wxString base, size_t level)
{
	if (base.length()) base += wxT(" ");
	else dst.push_back(wxEmptyString);
	for (size_t i = level; i < src.size(); ++i)
	{
		wxString newBase = base + src[i];
		dst.push_back(newBase);
		NkAddAllCombinations(dst, src, newBase, i + 1);
	}
}

class panelSetup : public formSetup, public nkDigTimPanel
{
public:
	panelSetup(frameMain* main, SItems& items)
		: formSetup(main->m_mainPanel)
		, m_main(main)
		, m_original_items(items)
		, m_items(items)
	{
		if (m_items.command == wxT("dpin"))
		{
			m_buttonWrite->Hide();
		}
		CreateControls();
	}

	void CreateControls()
	{
		Freeze();

		m_panelSetup->DestroyChildren();

		size_t field_count = m_items.fields.size();

		wxFlexGridSizer* vsizer;
		vsizer = new wxFlexGridSizer(0, field_count, 0, 0);
		vsizer->SetFlexibleDirection(wxBOTH);
		vsizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

		// assume we are the only form that can change the pins: not necessary to reload pin defs ...
		for (auto& field : m_items.fields)
		{
			wxStaticText* label = new wxStaticText(m_panelSetup, wxID_ANY, field.name, wxDefaultPosition, wxDefaultSize, 0);
			label->Wrap(-1);
			label->SetFont(wxFont(wxNORMAL_FONT->GetPointSize(), wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD, false, wxEmptyString));
			vsizer->Add(label, 0, wxALL, 5);
		}

		for (size_t item_index = 0; item_index < m_items.items.size(); ++item_index)
		{
			auto& item = m_items.items[item_index];
			item.sizers.resize(field_count);
			item.controls.resize(field_count);

			for (size_t field_index = 0; field_index < field_count; ++field_index)
			{

				item.sizers[field_index] = new wxBoxSizer(wxHORIZONTAL);
				CreateItemFieldControl(item_index, field_index);
				vsizer->Add(item.sizers[field_index], 0, 0, 0);
			}
		}

		m_panelSetup->SetSizer(vsizer);
		m_panelSetup->Layout();
		Layout();
		Thaw();
		SetChanged(false);
		UpdatePlusMin();
	}

	void CreateItemFieldControl(size_t item_index, size_t field_index)
	{
		SItem& item = m_items.items[item_index];
		SField field = m_items.GetFieldInfo(item_index,field_index);

		item.sizers[field_index]->Clear(true);
		wxControl* control = NULL;
		// index is always static
		SField::EFieldType type = field.type;

		wxString value;
		if (field_index < item.values.size())
		{
			value = item.values[field_index];
		}
		switch (type)
		{
		case SField::eNone:
			control = new wxStaticText(m_panelSetup, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(16, -1), 0);
			break;
		case SField::eStatic:
			control = new wxStaticText(m_panelSetup, wxID_ANY, value, wxDefaultPosition, wxSize(16, -1), 0);
			break;
		case SField::eString:
			control = new wxTextCtrl(m_panelSetup, wxID_ANY, value, wxDefaultPosition, wxSize(100, -1), 0);
			control->Connect(wxEVT_TEXT, wxCommandEventHandler(panelSetup::m_textChange), new itemData(item_index, field_index), this);
			::SendMessage(control->GetHWND(), EM_SETLIMITTEXT, (WPARAM)(field.higher ? field.higher: 10), 0);
			break;
		case SField::eRange:
		{
			int64_t span = field.higher - field.lower + 1;
			if (span < 256)
			{
				wxComboBox* cb = new wxComboBoxEx(m_panelSetup, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(100, -1), 0, NULL, wxCB_READONLY);
				cb->Connect(wxEVT_COMMAND_COMBOBOX_SELECTED, wxCommandEventHandler(panelSetup::m_OnCombobox), new itemData(item_index, field_index), this);
				//cb->SetBackgroundColour(m_main->GetBackgroundColour());
				long curVal = -1;
				bool isNum = value.ToLong(&curVal);
				for (int64_t value = field.lower; value <= field.higher; ++value)
				{
					wxString str = wxString::Format(wxT("%lld"), value);
					int i = cb->Append(str);
					if (str == item.values[field_index])
					{
						curVal = i;
					}
				}
				if (curVal == -1) curVal = 0;
				cb->SetSelection(curVal);
				control = cb;
			}
			else
			{
				control = new wxTextCtrl(m_panelSetup, wxID_ANY, value, wxDefaultPosition, wxSize(100, -1), 0);
				control->Connect(wxEVT_TEXT, wxCommandEventHandler(panelSetup::m_textChange), new itemData(item_index, field_index), this);
			}
		}
		break;
		case SField::eEnum:
		{
			if (field.values.size())
			{
				wxArrayString values;
				size_t width = 100;
				bool inputpins = field.values[0] == wxT("input pin-index or pin-name");
				bool outputpins = field.values[0] == wxT("output pin-index or pin-name");
				bool adcpins = field.values[0] == wxT("adc pin-index or pin-name");
				if (field.name == wxT("options"))
				{
					wxArrayString vs(wxSplit(field.values[0], wxT(' ')));
					NkAddAllCombinations(values, vs, wxEmptyString, 0);
					width = 300;
				}
				else if(inputpins || outputpins || adcpins)
				{
					size_t name_index = m_main->m_pins.fields.find(wxT("name"));
					SItems* items = NULL;
					if (name_index != -1)
					{
						if (m_items.command == wxT("dpin"))
						{
							// use the names of the dialog
							items = &m_items;
						}
						else
						{
							items = &m_main->m_pins;
						}
						for (size_t i = 0; i < items->items.size(); ++i)
						{
							SItem& item(items->items[i]);
							if ((inputpins && ((item.type == SItem::eInputPin) || (item.type == SItem::ePullupPin))) ||
								(outputpins && ((item.type == SItem::eOutputPin) || (item.type == SItem::ePwmPin))) ||
								(adcpins && (item.type == SItem::eAdcPin)))
							{
								values.push_back(item.values[name_index]);
							}
						}
					}
				}
				else if (field.values[0] == wxT("task-index or task-name"))
				{
					size_t name_index = m_main->m_tasks.fields.find(wxT("name"));
					SItems* items = NULL;
					if (name_index != -1)
					{
						if (m_items.command == wxT("dtask"))
						{
							// use the names of the dialog
							items = &m_items;
						}
						else
						{
							items = &m_main->m_tasks;
						}
						for (size_t i = 0; i < items->items.size(); ++i)
						{
							values.push_back(items->items[i].values[name_index]);
						}
					}
				}
				else
				{
					values = field.values;
				}
				wxComboBox* cb = new wxComboBoxEx(m_panelSetup, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(width, -1), 0, NULL, wxCB_READONLY);
				cb->Connect(wxEVT_COMMAND_COMBOBOX_SELECTED, wxCommandEventHandler(panelSetup::m_OnCombobox), new itemData(item_index, field_index), this);
				//cb->SetBackgroundColour(m_main->GetBackgroundColour());
				long curVal = -1;
				bool isNum = value.ToLong(&curVal);
				for (size_t value_index = 0; value_index < values.size(); ++value_index)
				{
					cb->Append(values[value_index]);
					if (!isNum && (values[value_index] == value))
					{
						curVal = value_index;
					}
				}
				if (values.size() && (curVal == -1))
				{
					curVal = 0;
					if (field_index < item.values.size())
					{
						item.values[field_index] = values[0];
					}
				}
				cb->SetSelection(curVal);
				control = cb;
			}
			else
			{
			control = new wxStaticText(m_panelSetup, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(16, -1), 0);
			}
		}
		break;
		}
		if (control)
		{
			item.sizers[field_index]->Add(control, 0, 0, 0);
		}
		item.controls[field_index] = control;
	}

	void SetChanged(bool changed)
	{
		m_changed = changed;
		m_buttonSend->Enable(changed);
		m_buttonCancel->Enable(changed);
	}

	void UpdatePlusMin()
	{
		m_buttonMin->Enable(m_items.items.size());
		m_buttonPlus->Enable(m_items.fields.size() && (int64_t(m_items.items.size()) < m_items.fields[0].higher));
	}

	void m_buttonPlusOnButtonClick(wxCommandEvent& event)
	{
		event.Skip();
		size_t item_index = m_items.items.size();
		m_items.items.push_back(SItem());
		SItem& item(m_items.items[item_index]);
		if (item_index < m_original_items.items.size())
		{
			item = m_original_items.items[item_index];
		}
		else
		{
			item.values.resize(m_items.fields.size());
			for (size_t field_index = 0; field_index < m_items.fields.size(); ++field_index)
			{
				SField field = m_items.GetFieldInfo(item_index, field_index);
				switch (field.type)
				{
				case SField::eStatic:
					item.values[field_index] = wxString::Format(wxT("%lld"), item_index + 1);
					break;
				case SField::eString:
				case SField::eEnum:
				case SField::eRange:
					if (field_index == 1)
					{
						item.values[field_index] = wxString::Format(wxT("%s %lld"), m_items.command.Mid(1), item_index + 1);
						break;
					}
					if (field.values.size())
					{
						item.values[field_index] = field.values[0];
						break;
					}
					if (field.lower != field.higher)
					{
						item.values[field_index] = wxString::Format(wxT("%lld"), field.lower);
						break;
					}
					if (field.name == wxT("delay")) item.values[field_index] = wxT("0s");
					if (field.name == wxT("up")) item.values[field_index] = wxT("1s");
					if (field.name == wxT("down")) item.values[field_index] = wxT("1s");
					break;
				}
			}
		}
		CreateControls();
		UpdateChanged();
	}

	void m_buttonMinOnButtonClick(wxCommandEvent& event)
	{
		event.Skip();
		m_items.items.erase(m_items.items.end() - 1);
		CreateControls();
		UpdateChanged();
	}

	void m_buttonCancelOnButtonClick(wxCommandEvent& event)
	{
		event.Skip();
		m_items = m_original_items;
		CreateControls();
	}

	void UpdateChanged()
	{
		m_changed = false;
		for (size_t item_index = 0; item_index < m_items.items.size(); ++item_index)
		{
			SItem& item = m_items.items[item_index];
			item.changed = true;
			while (1)
			{
				if (item_index >= m_original_items.items.size()) break;
				SItem& org_item = m_original_items.items[item_index];
				bool changed = false;
				for (size_t field_index = 0; field_index < m_items.fields.size(); ++field_index)
				{
					SField field = m_items.GetFieldInfo(item_index, field_index);
					if (field.type > SField::eStatic)
					{
						if (item.values[field_index] != org_item.values[field_index])
						{
							changed = true;
							break;
						}
					}
				}
				if (changed) break;
				item.changed = false;
				break;
			}
			if (item.changed) m_changed = true;
		}
		if (m_items.items.size() != m_original_items.items.size()) m_changed = true;
		SetChanged(m_changed);
	}


	void m_buttonSendOnButtonClick(wxCommandEvent& event)
	{
		event.Skip();
		Send();
	}

	void m_buttonWriteOnButtonClick(wxCommandEvent& event) 
	{ 
		event.Skip();
		UpdateChanged();
		m_main->WriteTasks(m_items);
	}

	void m_buttonSaveOnButtonClick(wxCommandEvent& event)
	{
		event.Skip();
		Save();
	}

	bool Save()
	{
		UpdateChanged();
		return m_main->SaveItems(m_items);
	}

	void m_buttonLoadOnButtonClick(wxCommandEvent& event)
	{
		event.Skip();
		if (m_main->LoadItems(m_items))
		{
			CreateControls();
			UpdateChanged();
		}
	}

	void Send()
	{
		wxBusyCursor wait;

		UpdateChanged();
		if (!m_changed) return;
		m_main->SendItems(m_items);
		m_items = m_original_items;
		CreateControls();
	}

	void m_textChange(wxCommandEvent& event)
	{
		event.Skip();

		itemData* id = dynamic_cast<itemData*>(event.GetEventUserData());
		if (!id) return;
		if (id->field_index > m_items.fields.size()) return;
		if (id->item_index > m_items.items.size()) return;

		wxTextCtrl* tc = dynamic_cast<wxTextCtrl*>(event.GetEventObject());
		if (!tc) return;

		SItem& item = m_items.items[id->item_index];
		wxString text = tc->GetValue();
		item.values[id->field_index] = text;

		if (m_items.command == wxT("dtask"))
		{
			if (id->field_index == 1)
			{
				// name of the task changed: update all comboboxes that rever to a task name
				for (size_t item_index = 0; item_index < m_items.items.size(); ++item_index)
				{
					for (size_t field_index = 1; field_index < m_items.fields.size(); ++field_index)
					{
						SField field = m_items.GetFieldInfo(item_index, field_index);
						if (field.values.size() && (field.values[0] == wxT("task-index or task-name")))
						{
							wxComboBoxEx* cb = dynamic_cast<wxComboBoxEx*>(m_items.items[item_index].controls[field_index]);
							if (cb)
							{
								int curSel = cb->GetSelection();
								cb->Delete(id->item_index);
								cb->Insert(text, id->item_index);
								cb->SetSelection(curSel);
							}
						}
					}
				}
			}
		}
		UpdateChanged();
	}

	void m_OnCombobox(wxCommandEvent& event) 
	{
		event.Skip();
		itemData* id = dynamic_cast<itemData*>(event.GetEventUserData());
		if (!id) return;
		if (id->field_index > m_items.fields.size()) return;
		if (id->item_index > m_items.items.size()) return;

		SItem& item = m_items.items[id->item_index];
		SField field = m_items.GetFieldInfo(id->item_index, id->field_index);

		wxComboBoxEx* cb = dynamic_cast<wxComboBoxEx*>(event.GetEventObject());
		if (!cb) return;

		if ((field.type = SField::eRange) && !field.values.size())
		{
			item.values[id->field_index] = wxString::Format(wxT("%lld"), cb->GetSelection() + field.lower);
		}
		else
		{
			item.values[id->field_index] = cb->GetValue();
		}
		for (size_t field_index = id->field_index + 1; field_index < m_items.fields.size(); ++field_index)
		{
			if (id->field_index == m_items.fields[field_index].mode_field)
			{
				CreateItemFieldControl(id->item_index, field_index);
			}
			Layout();
		}
		UpdateChanged();
	}

	bool CanClosePanel(wxFrame * mainFrame, bool allow_veto)
	{
		UpdateChanged();
		if (m_changed)
		{
			if (m_main->IsConnected())
			{
				int answer = wxMessageBox(wxT("Send the changes to the device ?"), wxT(""), wxYES | wxNO | wxCANCEL);
				if (answer == wxCANCEL) return false;
				if (answer == wxYES) Send();
			}
			else
			{
				int answer = wxMessageBox(wxT("Save the changes to file ?"), wxT(""), wxYES | wxNO | wxCANCEL);
				if (answer == wxCANCEL) return false;
				return m_main->SaveItems(m_items);
			}
		}
		return true;
	}
	frameMain* m_main;
	SItems &   m_original_items;
	SItems     m_items;
	bool       m_changed;

};

wxPanel* CreateSetupPanel(frameMain* parent, SItems& items)
{
	return new panelSetup(parent,items);
}
