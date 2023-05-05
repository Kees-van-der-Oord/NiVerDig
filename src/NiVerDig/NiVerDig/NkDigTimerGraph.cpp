#include "framework.h"

wxDEFINE_EVENT(NKDIGTIMERGRAPHEVENT, NkDigTimerGraphEvent);

wxBEGIN_EVENT_TABLE(NkDigTimerGraph, wxPanel)
EVT_PAINT(NkDigTimerGraph::OnPaint)
EVT_SIZE(NkDigTimerGraph::OnSize)
wxEND_EVENT_TABLE()

#define CHUNK_SIZE (4096 * 10)

DWORD GetSystemPageSize()
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return si.dwAllocationGranularity;
}

DWORD scopePages::m_mapPageSize = GetSystemPageSize();

fileSample * scopeReader::First(size_t first)
{
	if (!m_sample)
	{
		// first time: we have no clue: start to map the first pages and check if the
		// time_stamp is in there
		m_sample = m_pages.Map(0, 10);
		for (fileSample* sample = m_sample; sample ; sample = Next())
		{
			if (sample->timestamp >= first)
			{
				m_sample = sample;
				return m_sample;
			}
		}
		return NULL;
	}
	if (first > m_sample->timestamp)
	{
		// need to move forward
		for(fileSample * sample = m_sample; sample; sample = Next())
		{
			if (sample->timestamp >= first)
			{
				return m_sample;
			}
		}
		// no more data
		return NULL;
	}
	//else
	{
		// need to move backwards
		for (fileSample* sample = m_sample; sample; sample = Prev())
		{
			if (sample->timestamp < first)
			{
				return Next();
			}
		}
		// no more data
		return NULL;
	}
	return 	NULL;
}

fileSample* scopeReader::Next()
{
	fileSample* sample = m_sample + 1;
	if (sample >= m_pages.m_end)
	{
		sample = m_pages.MapNext(10);
	}
	if (sample) m_sample = sample;
	return sample;
}

fileSample* scopeReader::Prev()
{
	fileSample* sample = m_sample - 1;
	if (sample < m_pages.m_begin)
	{
		sample = m_pages.MapPrev(10);
	}
	if (sample) m_sample = sample;
	return sample;
}

void scopeReader::Reset()
{
	m_pages.Unmap();
	m_sample = NULL;
}

bool NkDigTimerGraph::Create(wxWindow* parent,
	wxWindowID winid,
	const wxPoint& pos,
	const wxSize& size,
	long style,
	const wxString& name)
{
	bool result = wxPanel::Create(parent, winid, pos, size, style, name);

	m_timer.Bind(wxEVT_TIMER, &NkDigTimerGraph::OnTimer, this);
	m_drawGrid = true;
	m_main = NULL;
	m_period = US_PER_SECOND; // start with a period of 1 second
	m_first = GetCurrentFileTime();
	m_last = m_first + m_period;
	m_triggerOn = false;
	m_triggerPolarity = triggerUp;
	m_armed = true;
	m_frozen = false;
	m_triggered = true;
	m_triggerChannel = 0;
	m_triggerMode = modeAuto;
	m_triggerDelay = m_period/1000 * 2; // in ms
	m_triggerTimeout = 0;
	m_hscale = 1.0;

	return result;
}

void NkDigTimerGraph::Init(frameMain* main, HANDLE file) 
{ 
	m_main = main;
	m_lines.resize(m_main->m_pins.items.size());
	for (size_t i = 0; i < m_lines.size(); ++i)
	{
		m_lines[i].type = m_main->m_pins.items[i].type;
	}
	m_reader.SetFile(file);
}

void NkDigTimerGraph::Reset()
{
	m_reader.Reset();
	m_lines.reset();
	m_first = 0;
	m_last = m_first + m_period;
	m_current = 0;
	m_hscale = 1.0;
}

void NkDigTimerGraph::OnSize(wxSizeEvent& event)
{
	m_wndSize = event.GetSize();
	if ((m_wndSize.x <= 0) || (m_wndSize.y <= 0)) return;
	m_bitmap.Create(m_wndSize);

	m_txtSize = GetTextExtent(wxT("24:00:00.000"));
	m_graph.x = 0;
	m_graph.width = m_wndSize.x;
	m_graph.y = 0;
	m_graph.height = m_wndSize.y;
	m_graphArea.x = m_graph.x + (m_txtSize.x * 3) / 2 + 10;
	m_graphArea.width = m_graph.width - (m_graphArea.x + m_txtSize.y);
	m_graphArea.y = m_txtSize.y;
	m_graphArea.height = m_graph.height - (m_graphArea.y + m_txtSize.y + 10);

	if (m_lines.size())
	{
		int line_height = m_graphArea.height / m_lines.size();
		if (line_height < 20) line_height = 20;
		if (line_height > 50) line_height = 50;
		int adc_height = line_height;
		int extra = m_graphArea.height - m_lines.size() * line_height;
		if (extra > 0)
		{
			int adc_count = 0;
			for (size_t i = 0; i < m_lines.size(); ++i)
			{
				CPinLine& line = m_lines[i];
				if (line.type == SItem::eAdcPin)
				{
					++adc_count;
				}
			}
			if (adc_count)
			{
				extra /= adc_count;
				adc_height += extra;
				if (adc_height > (256 + 8))
				{
					adc_height = 256 + 8;
				}
			}
		}
		int pos = m_graphArea.GetTop();
		for (size_t i = 0; i < m_lines.size(); ++i)
		{
			CPinLine& line = m_lines[i];
			line.label = m_main->m_pins.items[i].values[1];
			line.top = pos;
			pos += line.type == SItem::eAdcPin ? adc_height : line_height;
			line.bottom = pos;
		}
		m_graphArea.height = pos - m_graphArea.y;
	}

	m_drawGrid = true;
}

void NkDigTimerGraph::OnPaint(wxPaintEvent& WXUNUSED(evt))
{
	wxPaintDC dc(this);
	wxBufferedDC bdc(&dc, m_bitmap);
	HDC hdc = (HDC)bdc.GetHandle();

	if (m_drawGrid)
	{
		m_drawGrid = false;

		// erase the background
		bdc.SetBrush(*wxWHITE_BRUSH);
		bdc.SetPen(*wxWHITE_PEN);
		bdc.DrawRectangle(m_wndSize);

		// draw the grid
		bdc.SetPen(*wxLIGHT_GREY_PEN);
		double xstep = m_graphArea.GetWidth() / 10.;
		wxPoint pnt1(m_graphArea.GetRightTop());
		wxPoint pnt2(m_graphArea.GetRightBottom());
		for (int i = 0; i <= 10; ++i)
		{
			pnt1.x = pnt2.x = int(m_graphArea.x + i * xstep + 0.5);
			bdc.DrawLine(pnt1, pnt2);
		}
		pnt1 = m_graphArea.GetLeftTop();
		pnt2 = m_graphArea.GetRightTop();
		for (size_t i = 0; i < m_lines.size(); ++i)
		{
			pnt1.y = pnt2.y = m_lines[i].top;
			bdc.DrawLine(pnt1, pnt2);
		}
		bdc.DrawLine(m_graphArea.GetLeftBottom(), m_graphArea.GetRightBottom());

		// draw the labels
		bdc.SetPen(*wxBLACK_PEN);
		RECT textArea = { m_graph.x + 10,0, m_graphArea.x - 10,0 };
		for (auto& line : m_lines)
		{
			textArea.top = line.top;
			textArea.bottom = line.bottom;
			DrawText(hdc, line.label, line.label.length(), &textArea, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		}

		// draw horizontal scale
		bool absTime = false;
		bdc.SetPen(*wxBLACK_PEN);

		size_t microseconds = (m_last - m_first) / 10ULL;
		long graph_area_bottom = m_graphArea.GetBottom();
		long graph_area_right = m_graphArea.GetRight();
		if (microseconds) // don't draw labels if the range is < 1 us
		{
			int max_labels = m_graphArea.width / (m_txtSize.y * 2);
			if (max_labels <= 2) max_labels = 2;

			size_t first_us = m_first / 10ULL;
			size_t last_us = m_last / 10ULL;
			size_t start_t = first_us / US_PER_SECOND;              // start_t is in seconds 
			size_t start_us = first_us - (start_t * US_PER_SECOND); // us to add to start_t
			size_t step = 1;                                     // step is in microseconds
			struct tm* tm = NULL;
			if (absTime)
			{
				tm = localtime((time_t*)&start_t);
				if (!tm) return;
			}
			size_t us_per_label = microseconds / max_labels;
			if (us_per_label > US_PER_DAY)
			{
				// tick every week
				step = 7LL * US_PER_DAY;
				if (absTime)
				{
					start_t -= tm->tm_sec;
					start_t -= tm->tm_min * 60;
					start_t -= tm->tm_hour * 60 * 60;
					start_t -= tm->tm_wday * 24 * 60 * 60;
				}
			}
			else if (us_per_label > 8LL * US_PER_HOUR)
			{
				// tick every day
				step = US_PER_DAY;
				if (absTime)
				{
					start_t -= tm->tm_sec;
					start_t -= tm->tm_min * 60;
					start_t -= tm->tm_hour * 60 * 60;
				}
			}
			else if (us_per_label > 4LL * US_PER_HOUR)
			{
				// tick every 8 hours
				step = 8LL * US_PER_HOUR;
				if (absTime)
				{
					start_t -= tm->tm_sec;
					start_t -= tm->tm_min * 60;
					start_t -= (tm->tm_hour % 8) * 60 * 60;
				}
			}
			else if (us_per_label > US_PER_HOUR)
			{
				// tick every 4 hours
				step = 4LL * US_PER_HOUR;
				if (absTime)
				{
					start_t -= tm->tm_sec;
					start_t -= tm->tm_min * 60;
					start_t -= (tm->tm_hour % 4) * 60 * 60;
				}
			}
			else if (us_per_label > 30LL * US_PER_MINUTE)
			{
				// tick every hour
				step = US_PER_HOUR;
				if (absTime)
				{
					start_t -= tm->tm_sec;
					start_t -= tm->tm_min * 60;
				}
			}
			else if (us_per_label > 10LL * US_PER_MINUTE)
			{
				// tick every half hour
				step = 30LL * US_PER_MINUTE;
				if (absTime)
				{
					start_t -= tm->tm_sec;
					start_t -= (tm->tm_min % 30) * 60;
				}
			}
			else if (us_per_label > 5LL * US_PER_MINUTE)
			{
				// tick every 10 minutes
				step = 10LL * US_PER_MINUTE;
				if (absTime)
				{
					start_t -= tm->tm_sec;
					start_t -= (tm->tm_min % 10) * 60;
				}
			}
			else if (us_per_label > US_PER_MINUTE)
			{
				// tick every 5 minutes
				step = 5LL * US_PER_MINUTE;
				if (absTime)
				{
					start_t -= (tm->tm_min % 5) * 60;
				}
			}
			else if (us_per_label > 30LL * US_PER_SECOND)
			{
				// tick every minute
				step = US_PER_MINUTE;
				if (absTime)
				{
					start_t -= tm->tm_sec;
				}
			}
			else if (us_per_label > 10LL * US_PER_SECOND)
			{
				// tick every 30 seconds
				step = 30LL * US_PER_SECOND;
				if (absTime)
				{
					start_t -= tm->tm_sec % 30;
				}
			}
			else if (us_per_label > 5LL * US_PER_SECOND)
			{
				// tick every 10 seconds
				step = 10LL * US_PER_SECOND;
				if (absTime)
				{
					start_t -= tm->tm_sec % 10;
				}
			}
			else if (us_per_label > 2LL * US_PER_SECOND)
			{
				// tick every 5 seconds
				step = 5LL * US_PER_SECOND;
				if (absTime)
				{
					start_t -= tm->tm_sec % 5;
				}
			}
			else if (us_per_label > US_PER_SECOND)
			{
				// tick every 2 seconds
				step = 2LL * US_PER_SECOND;
				if (absTime)
				{
					start_t -= tm->tm_sec % 2;
				}
			}
			else if (us_per_label > 500LL * US_PER_MS)
			{
				// tick every seconds
				step = US_PER_SECOND;
			}
			else if (us_per_label > 200LL * US_PER_MS)
			{
				// step every 0.5 s
				step = 500L * US_PER_MS;
				if (absTime)
				{
					start_us -= start_us % (500LL * US_PER_MS);
				}
			}
			else if (us_per_label > 100LL * US_PER_MS)
			{
				// step every 0.2 s
				step = 200L * US_PER_MS;
				if (absTime)
				{
					start_us -= start_us % (200LL * US_PER_MS);
				}
			}
			else if (us_per_label > 50LL * US_PER_MS)
			{
				// step every 0.1 s
				step = 100L * US_PER_MS;
				if (absTime)
				{
					start_us -= start_us % (100LL * US_PER_MS);
				}
			}
			else if (us_per_label > 20LL * US_PER_MS)
			{
				// step every 0.05 s
				step = 50L * US_PER_MS;
				if (absTime)
				{
					start_us -= start_us % (50LL * US_PER_MS);
				}
			}
			else if (us_per_label > 10LL * US_PER_MS)
			{
				// step every 0.02 s
				step = 20L * US_PER_MS;
				if (absTime)
				{
					start_us -= start_us % (20LL * US_PER_MS);
				}
			}
			else if (us_per_label > 5LL * US_PER_MS)
			{
				// step every 0.01 s
				step = 10L * US_PER_MS;
				if (absTime)
				{
					start_us -= start_us % (10LL * US_PER_MS);
				}
			}
			else if (us_per_label > 2LL * US_PER_MS)
			{
				// step every 0.005 s
				step = 5L * US_PER_MS;
				if (absTime)
				{
					start_us -= start_us % (5LL * US_PER_MS);
				}
			}
			else if (us_per_label > 1LL * US_PER_MS)
			{
				// step every 0.002 s
				step = 2L * US_PER_MS;
				if (absTime)
				{
					start_us -= start_us % (2LL * US_PER_MS);
				}
			}
			else if (us_per_label > 500)
			{
				// step every 0.001 s
				step = US_PER_MS;
				if (absTime)
				{
					start_us -= start_us % (1LL * US_PER_MS);
				}
			}
			else if (us_per_label > 200)
			{
				// step every 0.0005 s
				step = 500;
				if (absTime)
				{
					start_us -= start_us % 500;
				}
			}
			else if (us_per_label > 100)
			{
				// step every 0.0002 s
				step = 200;
				if (absTime)
				{
					start_us -= start_us % 200;
				}
			}
			else if (us_per_label > 50)
			{
				// step every 0.0001 s
				step = 100;
				if (absTime)
				{
					start_us -= start_us % 100;
				}
			}
			else if (us_per_label > 20)
			{
				// step every 0.00005 s
				step = 50;
				if (absTime)
				{
					start_us -= start_us % 50;
				}
			}
			else if (us_per_label > 10)
			{
				// step every 0.00002 s
				step = 20;
				if (absTime)
				{
					start_us -= start_us % 20;
				}
			}
			else if (us_per_label > 5)
			{
				// step every 0.00001 s
				step = 10;
				if (absTime)
				{
					start_us -= start_us % 10;
				}
			}
			else if (us_per_label > 2)
			{
				// step every 0.000005 s
				step = 5;
				if (absTime)
				{
					start_us -= start_us % 5;
				}
			}
			else if (us_per_label > 1)
			{
				// step every 0.000002 s
				step = 2;
				if (absTime)
				{
					start_us -= start_us % 2;
				}
			}
			else
			{
				step = 1;
			}

			// draw the labels
			m_hscale = double(m_graphArea.width) / (last_us - first_us);
			RECT text_area;
			text_area.top = m_graphArea.GetBottom() + 10;
			text_area.bottom = text_area.top + m_txtSize.y;
			if (absTime)
			{
				// back to us
				start_t = start_t * US_PER_SECOND + start_us;
			}
			if (absTime)
			{
				for (size_t t = start_t; t <= last_us; t += step)
				{
					size_t tRel = t - first_us;
					long x = long(m_graphArea.x + double(tRel) * m_hscale + 0.5);
					if ((x > m_graphArea.x) && (x < graph_area_right))
					{
						MoveToEx(hdc, x, graph_area_bottom, NULL);
						LineTo(hdc, x, graph_area_bottom + 10);
						wxString text;
						time_t seconds = t / US_PER_SECOND;
						struct tm* tm = localtime(&seconds);
						if (!tm) break;
						if (step >= US_PER_MINUTE)
						{
							text = wxString::Format(wxT("%02d:%02d"), tm->tm_hour, tm->tm_min);
						}
						else if (step >= US_PER_SECOND)
						{
							text = wxString::Format(wxT("%02d:%02d:%02d"), tm->tm_hour, tm->tm_min, tm->tm_sec);
						}
						else if (step >= US_PER_MS)
						{
							text = wxString::Format(wxT("%02d:%02d:%02d.%03lld"), tm->tm_hour, tm->tm_min, tm->tm_sec, (size_t(t) / US_PER_MS) % MS_PER_SECOND);
						}
						else
						{
							text = wxString::Format(wxT("%02d:%02d:%02d.%06lld"), tm->tm_hour, tm->tm_min, tm->tm_sec, size_t(t) % US_PER_SECOND);
						}
						text_area.left = x - m_txtSize.x;
						text_area.right = x + m_txtSize.x;
						DrawText(hdc, text, text.length(), &text_area, DT_CENTER | DT_VCENTER);
					}
				}
			}
			else
			{
				for (size_t t = 0; t <= microseconds; t += step)
				{
					long x = long(m_graphArea.x + double(t) * m_hscale + 0.5);
					if ((x >= m_graphArea.x) && (x <= (graph_area_right + 1)))
					{
						MoveToEx(hdc, x, graph_area_bottom, NULL);
						LineTo(hdc, x, graph_area_bottom + 10);
						wxString text;
						size_t hours = t / US_PER_HOUR;
						size_t minutes = t / US_PER_MINUTE;
						size_t seconds = t / US_PER_SECOND;
						size_t ms = (t % US_PER_SECOND) / US_PER_MS;
						if (step < US_PER_MS)
						{
							text = wxString::Format(wxT("%6lld"), t);
						}
						else if (step < US_PER_SECOND)
						{
							text = wxString::Format(wxT("%3lld"), t / US_PER_MS);
						}
						else if (step < US_PER_MINUTE)
						{
							text = wxString::Format(wxT("%02lld"), seconds);
						}
						else if (step < US_PER_HOUR)
						{
							text = wxString::Format(wxT("%02lld"), minutes);
						}
						else //if (step < US_PER_DAY)
						{
							text = wxString::Format(wxT("%02lld"), hours);
						}
						text_area.left = x - m_txtSize.x;
						text_area.right = x + m_txtSize.x;
						DrawText(hdc, text, text.length(), &text_area, DT_CENTER | DT_VCENTER);
					}
				}
				wxString caption;
				if (step < US_PER_MS)
				{
					caption = wxT("[microseconds]");
				}
				else if (step < US_PER_SECOND)
				{
					caption = wxT("[milliseconds]");
				}
				else if (step < US_PER_MINUTE)
				{
					caption = wxT("[seconds]");
				}
				else if (step < US_PER_HOUR)
				{
					caption = wxT("[minutes]");
				}
				else //if (step < US_PER_DAY)
				{
					caption = wxT("[hours]");
				}
				text_area.left = m_graphArea.x;
				text_area.right = graph_area_right;
				text_area.top += m_txtSize.y + 10;
				text_area.bottom = text_area.top + m_txtSize.y;
				DrawText(hdc, caption, caption.length(), &text_area, DT_CENTER | DT_VCENTER);

				if (m_first)
				{
					wxString str = FormatLocalFileTime(m_first);
					DrawText(hdc, str, str.length(), &text_area, DT_LEFT | DT_VCENTER);
					str = FormatLocalFileTime(m_last);
					DrawText(hdc, str, str.length(), &text_area, DT_RIGHT | DT_VCENTER);
				}
			}
		}
		MoveToEx(hdc, m_graphArea.x, graph_area_bottom, NULL);
		LineTo(hdc, graph_area_right, graph_area_bottom);
		// move back to 100ns scaling
		m_hscale /= 10ULL;
	}
	// paint the data until now
	bdc.SetPen(*wxBLUE_PEN);
	for (fileSample* s = m_reader.Next(); s; s = m_reader.Next())
	{
		if (!m_first)
		{
			m_first = s->timestamp;
			m_last = m_first + m_period;
		}
		size_t line_index = s->channel;
		if (line_index < m_lines.size())
		{
			auto& line = m_lines[line_index];
			size_t t = s->timestamp;
			if ((line.value != NOT_INITIALIZED) && (t >= m_first) && (line.last < m_last))
			{
				if (t > m_last) t = m_last;
				int x1 = m_graphArea.x;
				if (line.last > m_first) x1 += (line.last - m_first) * m_hscale;
				int x2 = m_graphArea.x + (t - m_first) * m_hscale;
				int y1;
				if (line.type == SItem::eAdcPin)
				{
					int pos = ( int(line.value) * (line.bottom - line.top - 8)) / 256;
					y1 = line.bottom - 4 - pos;
				}
				else
				{
					y1 = line.value ? line.top + 4 : line.bottom - 4;
				}
				MoveToEx(hdc, x1, y1, NULL);
				if (line.type != SItem::eAdcPin)
				{
					LineTo(hdc, x2, y1);
				}
				if (s->timestamp < m_last)
				{
					int y2;
					if (line.type == SItem::eAdcPin)
					{
						int pos = (int(s->state) * (line.bottom - line.top - 8)) / 256;
						y2 = line.bottom - 4 - pos;
					}
					else
					{
						y2 = s->state ? line.top + 4 : line.bottom - 4;
					}
					LineTo(hdc, x2, y2);
				}
			}
			line.last = s->timestamp;
			line.value = s->state;
		}
		m_current = s->timestamp;
	}
	size_t last = m_current;
	if (last >= m_last)
	{
		last = m_last;
	}
	// draw the line of the current state up to last.
	// to do: for analog lines: the next data point after 'last' and draw a diagonal line ??
	int x2 = m_graphArea.x + (last - m_first) * m_hscale;
	for (auto& line : m_lines)
	{
		if ((line.last < last)/* && (line.type != SItem::eAdcPin)*/)
		{
			int x1 = m_graphArea.x;
			if (line.last > m_first) x1 += (line.last - m_first) * m_hscale;
			int y = line.value ? line.top + 4 : line.bottom - 4;
			MoveToEx(hdc, x1, y, NULL);
			LineTo(hdc, x2, y);
			line.last = m_current;
		}
	}
}

void NkDigTimerGraph::OnTimer(wxTimerEvent& event)
{
	NkComPort_WriteA(m_main->m_port, "?", 1);
	if (m_frozen)
	{
		// to do: process new data and remember new trigger point ...
		return;
	}
	if (m_triggered)
	{
		if (m_current > m_last)
		{
			if (!m_triggerOn)
			{
				m_first = m_current;
				m_last = m_first + m_period;
				m_drawGrid = true;
			}
			else
			{
				m_triggered = false;
				switch (m_triggerMode)
				{
				case modeAuto: 
					m_triggerTimeout = GetTickCount64(); break;
				case modeNormal: 
					break;
				case modeSingle: 
					m_armed = false;
					GetParent()->GetEventHandler()->QueueEvent(new NkDigTimerGraphEvent(this, 0));
					break;
				}
			}
		}
	}
	else
	{
		// check in the data if a trigger event occured
		for (fileSample* s = m_reader.Next(); s; s = m_reader.Next())
		{
			m_current = s->timestamp;
			size_t channel = s->channel;
			if ((m_triggerMode == modeSingle) && !m_armed) continue;
			if (channel < m_lines.size())
			{
				m_lines[channel].value = s->state;
				m_lines[channel].last = s->timestamp;
			}
			if ((s->channel == m_triggerChannel) && (s->state == m_triggerPolarity))
			{
				m_triggered = true;
				WindBack(m_first = s->timestamp - m_period / 10);
				break;
			}
		}
		if ((m_triggerMode == modeAuto) && ((GetTickCount64() - m_triggerTimeout) > m_triggerDelay))
		{
			m_triggered = true;
			m_first = m_current;
			m_last = m_first + m_period;
			m_drawGrid = true;
		}
	}
	if (m_triggered)
	{
		Refresh(false);
	}
}

void NkDigTimerGraph::WindTo(size_t first)
{
	if (first < m_current)
	{
		WindBack(first);
	}
	else
	{
		WindFore(first);
	}
}

void NkDigTimerGraph::WindBack(size_t first)
{
	m_first = first;
	m_last = m_first + m_period;
	fileSample* s;
	for (s = m_reader.m_sample; s; s = m_reader.Prev())
	{
		m_current = s->timestamp;
		if (s->timestamp < m_first)
		{
			break;
		}
	}
	for (auto& line : m_lines)
	{
		line.last = m_current;
		line.value = 0xFFFF;
	}
	size_t count = m_lines.size();
	for (; s; s = m_reader.Prev())
	{
		size_t channel = s->channel;
		if (~channel < m_lines.size()) channel = ~channel; // no real event but tick sync event
		if (channel < m_lines.size())
		{
			if (m_lines[channel].value == -1)
			{
				m_lines[channel].value = s->state;
				m_lines[channel].last = s->timestamp;
				--count;
				if (!count)
				{
					break;
				}
			}
		}
	}
	m_drawGrid = true;
}

void NkDigTimerGraph::WindFore(size_t first)
{
	m_first = first;
	m_drawGrid = true;
}

void NkDigTimerGraph::Start(bool on) 
{
	if (!on)
	{
		m_timer.Stop();
		return;
	}
	m_reader.Reset();
	size_t period = m_last - m_first;
	m_current = 0;
	m_first = 0;
	m_last = m_first + m_period;
	m_lines.reset();
	m_drawGrid = true;
	m_armed = true;
	m_triggered = !m_triggerOn;
	m_timer.Start(100);
}

void NkDigTimerGraph::SetPeriod(size_t period, bool keep_center)
{
	if (!keep_center)
	{
		m_first = m_current;
	}
	else
	{
		m_frozen = true;
		WindTo(m_first + (int64_t(m_period) - int64_t(period)) / 4);
	}
	m_period = period;
	m_last = m_first + m_period;
	m_drawGrid = true;
	m_triggerDelay = period / 1000.; // in ms
	Refresh(false);
}

void NkDigTimerGraph::SetRange(size_t first, size_t last)
{
	m_first = first;
	if (last)
	{
		m_last = last;
		m_period = last - first;
	}
	else
	{
		m_last = m_first + m_period;
	}
	if (last < m_current)
	{
		m_frozen = true;
	}
	WindTo(m_first);
	m_drawGrid = true;
	m_triggerDelay = m_period / 1000.; // in ms
	Refresh(false);
}

void NkDigTimerGraph::SetTriggerOn(bool trigger)
{
	m_triggerOn = trigger;
	m_triggered = !trigger;
}

void NkDigTimerGraph::SetArm(bool arm)
{
	m_frozen = !arm;
}

void NkDigTimerGraph::SetTriggerPolarity(NkDigTimerGraph::ETriggerPolarity polarity)
{
	m_triggerPolarity = polarity;
}

void NkDigTimerGraph::SetTriggerChannel(size_t channel)
{
	m_triggerChannel = channel;
}

void NkDigTimerGraph::SetTriggerMode(NkDigTimerGraph::ETriggerMode mode)
{
	m_frozen = false;
	m_armed = true;
	m_triggerMode = mode;
	if (m_triggerOn)
	{
		m_triggered = !m_triggerOn;
	}
}

void NkDigTimerGraph::Move(double amount)
{
	m_frozen = true;
	int64_t shift = int64_t(m_period / 10) * int64_t(amount * 10 + 0.5);
	WindTo(m_first + shift);
	Refresh(false);
}

