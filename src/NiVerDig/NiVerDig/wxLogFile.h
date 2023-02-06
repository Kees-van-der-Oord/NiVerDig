#pragma once

class wxLogFile
{
public:
	wxLogFile()
	: m_file(NULL)
	{
		m_name[0] = 0;
		Open();
	}
	~wxLogFile()
	{
		Close();
	}
	
	bool Open()
	{
		Close();
		GetTempPath(_MAX_FNAME, m_name);
		GetTempFileName(m_name, L"nkdt", 0, m_name);
		m_file = _wfsopen(m_name,  L"wt", _SH_DENYWR);
		return m_file != NULL;
	}

	void Close()
	{
		if (!m_file) return;
		fclose(m_file);
		m_file = NULL;
		DeleteFile(m_name);
	}

	int  Log(const wchar_t* line, long send)
	{
		if(send != -1) fwprintf(m_file, send ? L"> " : L"< ");
		int r = fwprintf(m_file, L"%s", line);
		fflush(m_file);
		return r;
	}

	FILE*   m_file;
	wchar_t m_name[_MAX_FNAME];
};

class wxIndexTextFile
{
public:
	wxIndexTextFile(const wxString & file)
		: m_file(INVALID_HANDLE_VALUE)
		, m_index(INVALID_HANDLE_VALUE)
		, m_file_len(0)
		, m_line_count(0)
	{
		GetTempPath(_MAX_FNAME, m_index_name);
		GetTempFileName(m_index_name, L"nkdt", 0, m_index_name);
		m_index = CreateFile(m_index_name, GENERIC_READ| GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
		Open(file);
	}

	~wxIndexTextFile()
	{
		CloseHandle(m_index);
		DeleteFile(m_index_name);
	}

	bool Open(const wxString& file)
	{
		Close();
		m_file = CreateFile(file, GENERIC_READ, FILE_SHARE_WRITE | FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		return m_file != INVALID_HANDLE_VALUE;
	}

	void Close()
	{
		if (m_file == INVALID_HANDLE_VALUE) return;
		CloseHandle(m_file);
		m_file = INVALID_HANDLE_VALUE;
		m_line_count = m_file_len = 0;
		SetFilePointer(m_index, 0, 0, FILE_BEGIN);
		SetEndOfFile(m_index);
	}

	bool Update()
	{
		size_t l = 0;
		GetFileSizeEx(m_file, (LARGE_INTEGER*) &l);
		if (l <= m_file_len) return false;
		BYTE data[1024];
		while (m_file_len < l)
		{
			DWORD read = 0;
			ReadFile(m_file, &data, 1024, &read, NULL);
			if (!read) break;
			for (DWORD i = 0; i < read; ++i)
			{
				if (data[i] == '\n')
				{
					DWORD written = 0;
					size_t line_end = m_file_len + i + 1;
					WriteFile(m_index, &line_end, sizeof(size_t), &written, NULL);
					++m_line_count;
				}
			}
			m_file_len += read;
		}
		return true;
	}

	wxString GetText(size_t begin_line, size_t end_line)
	{
		DWORD read = 0;
		if (end_line >= m_line_count) end_line = m_line_count - 1;
		if (begin_line >= end_line) return wxEmptyString;
		size_t begin_offset = 0;
		if (begin_line != 0)
		{
			--begin_line;
			begin_line *= sizeof(size_t);
			SetFilePointerEx(m_index, *(LARGE_INTEGER*)(&begin_line), NULL, FILE_BEGIN);
			ReadFile(m_index, &begin_offset, sizeof(size_t), &read, NULL);
		}
		size_t end_offset = 0;
		if (end_line == (m_line_count * sizeof(size_t)))
		{
			end_offset = m_file_len;
		}
		else
		{
			end_line *= sizeof(size_t);
			SetFilePointerEx(m_index, *(LARGE_INTEGER*)(&end_line), NULL, FILE_BEGIN);
			ReadFile(m_index, &end_offset, sizeof(size_t), &read, NULL);
		}
		SetFilePointer(m_index, 0, NULL, FILE_END);
		if (end_offset <= begin_offset) return wxEmptyString;
		size_t count = end_offset - begin_offset;
		char* data = (char*)malloc(count+1);
		SetFilePointerEx(m_file, *(LARGE_INTEGER*)(&begin_offset), NULL, FILE_BEGIN);
		ReadFile(m_file, data, count, &read, NULL);
		data[count] = 0;
		wxString str(data);
		free(data);
		return str;
	}

	HANDLE m_file;
	HANDLE m_index;
	wchar_t m_index_name[_MAX_FNAME];
	size_t m_file_len;
	size_t m_line_count;
};