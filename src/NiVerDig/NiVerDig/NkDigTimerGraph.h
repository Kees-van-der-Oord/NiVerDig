#pragma once

#include <wx/panel.h>
#include <wx/bitmap.h>
#include <wx/scrolwin.h>
#include <wx/timer.h>
#include "NkDigTimerScope.h"


class  NkDigTimerGraphEvent;
wxDECLARE_EVENT(NKDIGTIMERGRAPHEVENT, NkDigTimerGraphEvent);
typedef void (wxEvtHandler::* NkDigTimerGraphEventFunction)(NkDigTimerGraphEvent&);
#define NkDigTimerGraphEventHandler(func) wxEVENT_HANDLER_CAST(NkDigTimerGraphEventFunction, func)
#define EVT_NKDIGTIMERGRAPHEVENT(id, func) \
 	wx__DECLARE_EVT1(NKDIGTIMERGRAPHEVENT, id, NkDigTimerGraphEventHandler(func))

class NkDigTimerGraph;

class NkDigTimerGraphEvent : public wxCommandEvent
{
public:
	NkDigTimerGraphEvent(NkDigTimerGraph* source, int id = 0, wxEventType commandType = NKDIGTIMERGRAPHEVENT)
		: wxCommandEvent(commandType, id)
		, m_source(source)
	{ }

	// You *must* copy here the data to be transported
	NkDigTimerGraphEvent(const NkDigTimerGraphEvent& event)
		: wxCommandEvent(event)
		, m_source(event.m_source)
	{
	}

	// Required for sending with wxPostEvent()
	wxEvent* Clone() const { return new NkDigTimerGraphEvent(*this); }

	NkDigTimerGraph* m_source;
};

#define US_PER_MS 1000ULL
#define MS_PER_SECOND 1000ULL
#define US_PER_SECOND (MS_PER_SECOND * US_PER_MS)
#define US_PER_MINUTE (60ULL * US_PER_SECOND)
#define US_PER_HOUR (60ULL * US_PER_MINUTE)
#define US_PER_DAY (24ULL * US_PER_HOUR)

class frameMain;

class NkDigTimerGraph : public wxPanel
{
public:
    NkDigTimerGraph() { }

    NkDigTimerGraph(wxWindow* parent,
        wxWindowID winid = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL | wxNO_BORDER,
        const wxString& name = wxASCII_STR(wxPanelNameStr))
    {
        Create(parent, winid, pos, size, style, name);
    }

	bool Create(wxWindow* parent,
		wxWindowID winid,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxScrolledWindowStyle,
		const wxString& name = wxASCII_STR(wxPanelNameStr));

	void Init(frameMain* main, HANDLE file);
	void Reset();
	void Start(bool on);
	void SetPeriod(size_t period, bool keep_center);
	void SetRange(size_t first, size_t last);
	void SetTriggerOn(bool on);
	void SetArm(bool on);
	enum ETriggerPolarity{ triggerDown, triggerUp};
	void SetTriggerPolarity(ETriggerPolarity polarity);
	void SetTriggerChannel(size_t channel);
	enum ETriggerMode { modeAuto, modeNormal, modeSingle};
	void SetTriggerMode(ETriggerMode mode);
	void Move(double amount);
	void WindTo(size_t first);
	void WindBack(size_t first);
	void WindFore(size_t first);

	void OnSize(wxSizeEvent& event);
	void OnPaint(wxPaintEvent& WXUNUSED(evt));
	void OnTimer(wxTimerEvent& event);

	class CPinLine
	{
	public:
		wxString      label;
		int           top;
		int           bottom;
		unsigned char value;
//		size_t        first;
		size_t        last;
	};
	class CPinLines : public std::vector<CPinLine> 
	{
	public:
		void reset()
		{
			for (auto& line : *this)
			{
				line.value = -1;
				//line.first = 0;
				line.last = 0;
			}
		}
	};
	

	frameMain*     m_main;
	wxSize         m_wndSize;
    wxBitmap       m_bitmap;
	wxRect         m_graph;
	wxRect         m_graphArea;
	wxSize         m_txtSize;
	size_t         m_period; // full scale in us (10* us/div)
	size_t         m_first;  // microseconds
	size_t         m_last;   // microseconds
	size_t         m_current; // timestamp of last read data
	double         m_hscale;
	wxTimer        m_timer;

	// trigger
	bool             m_triggerOn;
	ETriggerPolarity m_triggerPolarity;
	bool             m_frozen;
	bool             m_armed; // waiting for a trigger
	bool             m_triggered; // trigger seen
	size_t           m_triggerChannel;
	ETriggerMode     m_triggerMode;
	size_t           m_triggerDelay;
	size_t           m_triggerTimeout;

	bool           m_drawGrid;
	CPinLines      m_lines;

	scopeReader   m_reader;

private:
    wxDECLARE_EVENT_TABLE();

};

