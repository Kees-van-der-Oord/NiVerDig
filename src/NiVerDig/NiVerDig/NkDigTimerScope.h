#pragma once

#include <wx/thread.h>
#include <wx/msw/private.h>

class panelScope;

#pragma pack(push, r1, 1)   // n = 16, pushed to stack
struct serialSample
{
	char channel;
	byte state;
	unsigned long tick;  // in us
};

struct fileSample
{
	char   channel;
	byte   state;
	size_t timestamp;
};
#pragma pack(pop, r1)

class threadScope : public wxThread
{
public:
	threadScope(panelScope* scope)
		: wxThread(wxTHREAD_JOINABLE)
		, m_scope(scope)
		, m_stop(false)
	{
	}

	void* Entry();

	panelScope*   m_scope;
	int64_t       m_time; // us since 1970
	volatile bool m_stop;

};

struct scopePages
{
	scopePages()
	: m_file(INVALID_HANDLE_VALUE)
	, m_filesize(0)
	, m_map(NULL)
	, m_mem(NULL)
	, m_offset(0)
	, m_size(0)
	, m_begin(NULL)
	, m_end(NULL)
	, m_lastsample(0)
	{}

	~scopePages()
	{
		SetFile(INVALID_HANDLE_VALUE,0);
	}

	void SetFile(HANDLE file, size_t lastsample)
	{
		if (m_file != INVALID_HANDLE_VALUE)
		{
			Unmap();
		}
		m_file = file;
		m_offset = 0;
		m_size = 0;
		m_begin = NULL;
		m_end = NULL;
		m_lastsample = lastsample;
	}

	void SetLastSample(size_t lastsample)
	{
		m_lastsample = lastsample;
	}

	void Unmap()
	{
		if (m_mem)
		{
			UnmapViewOfFile(m_mem);
			m_mem = NULL;
			m_begin = NULL;
			m_end = NULL;
			m_size = 0;
		}
		if (m_map)
		{
			CloseHandle(m_map);
			m_map = NULL;
		}
	}

	fileSample* Map(size_t page, size_t count)
	{
		if (count < 1) count = 1;
		Unmap();
		if (m_lastsample)
		{
			m_filesize = m_lastsample;
		}
		else
		{
			GetFileSizeEx(m_file, (LARGE_INTEGER*)&m_filesize);
		}
		m_offset = m_mapPageSize * page;
		if (m_offset >= m_filesize) return NULL;
		m_size = m_mapPageSize * (count + 1); // allocate one page more to handle non-aligned samples
		if ((m_offset + m_size) > m_filesize) m_size = m_filesize - m_offset;
		m_map = CreateFileMapping(m_file, NULL, PAGE_READONLY, 0, 0, NULL);
		if (!m_map) return NULL;
		m_mem = MapViewOfFile(m_map, FILE_MAP_READ, ((DWORD*)&m_offset)[1], ((DWORD*)&m_offset)[0], m_size);
		if (!m_mem) return NULL;
		size_t index_first = (m_offset + sizeof(fileSample)  - 1) / sizeof(fileSample);
		size_t offset_first = index_first * sizeof(fileSample) - m_offset;
		m_begin = (fileSample*)((unsigned char*)m_mem + offset_first);
		size_t index_last = (m_offset + m_size) / sizeof(fileSample);
		m_end = m_begin + (index_last - index_first);
		return m_begin;
	}

	fileSample * MapNext(size_t count)
	{
		if (count < 2) count = 2;
		size_t filesize;
		if (m_lastsample)
		{
			filesize = m_lastsample;
		}
		else
		{
			GetFileSizeEx(m_file, (LARGE_INTEGER*)&filesize);
		}
		//OutputDebugString(wxString::Format(wxT("size %lld\n"), filesize));
		if ((m_offset + m_size) >= filesize)
		{
			return NULL; // no more data available
		}
		size_t next_offset = m_offset + ((unsigned char*)m_end - (unsigned char*)m_mem);
		size_t next_page = next_offset / m_mapPageSize;
		if (!Map(next_page, count)) return NULL; // mapping failed ???
		next_offset -= m_offset;
		return (fileSample*)((unsigned char*)m_mem + next_offset);
	}

	fileSample* MapPrev(size_t count)
	{
		if (count < 2) count = 2;
		if (m_offset == 0LL) return NULL;
		size_t move_back = (m_mapPageSize * (count - 1));
		if (move_back > m_offset) move_back = m_offset;
		size_t next_offset = m_offset - move_back;
		size_t next_page = next_offset / m_mapPageSize;
		size_t sample = m_offset + ((unsigned char*)m_begin - (unsigned char*)m_mem);
		if (!Map(next_page, count)) return NULL; // mapping failed ???
		sample -= m_offset + sizeof(fileSample);
		return (fileSample*)((unsigned char*)m_mem + sample);
	}

	HANDLE m_file;
	size_t m_filesize;
	HANDLE m_map;
	void* m_mem;
	size_t m_offset;
	size_t m_size;
	fileSample* m_begin;
	fileSample* m_end;
	static DWORD m_mapPageSize;
	size_t m_lastsample;
};

class scopeReader
{
public:
	scopeReader()
	: m_file(NULL)
	, m_sample(NULL)
	{
	}

	~scopeReader()
	{
		Reset();
	}

	void SetFile(HANDLE file, size_t lastsample) { m_file = file; m_pages.SetFile(file, lastsample); m_sample = NULL; }
	void SetLastSample(size_t lastsample) { m_pages.SetLastSample(lastsample); }
	fileSample* First(size_t first);
	void Reset();
	fileSample* Next();
	fileSample* Prev();

	HANDLE      m_file;
	scopePages  m_pages;
	fileSample* m_sample;   // pointer to current sample
};