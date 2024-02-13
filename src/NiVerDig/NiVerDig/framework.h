// header.h : include file for standard system include files,
// or project specific include files
//

#pragma once

#include "targetver.h"
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers

#include <wx/wxprec.h>
#include <wx/app.h>
#include <wx/grid.h>
#include <wx/treectrl.h>
#include <wx/spinctrl.h>
#include <wx/artprov.h>
#include <wx/clipbrd.h>
#include <wx/image.h>
#include <wx/colordlg.h>
#include <wx/wxhtml.h>
#include <wx/imaglist.h>
#include <wx/dataobj.h>
#include <wx/dcclient.h>
#include <wx/bmpbuttn.h>
#include <wx/menu.h>
#include <wx/toolbar.h>
#include <wx/statusbr.h>
#include <wx/msgdlg.h>
#include <wx/textdlg.h>
#include <wx/aui/aui.h>
#include <wx/stdpaths.h>
#include <wx/dir.h>
#include <wx/cmdline.h>
#include <wx/tglbtn.h>
#include <wx/statbmp.h>
#include <wx/hyperlink.h>
#include <wx/fs_mem.h>
#include <wx/persist/toplevel.h>
#include <wx/fileconf.h>
#include <wx/display.h>
#include <wx/dcbuffer.h>
#include <wx/tokenzr.h>
#include <wx/arrstr.h>
#include <wx/thread.h>
#include <wx/treelist.h>
#include <wx/persist/splitter.h>
#include <wx/regex.h>
#include <wx/timer.h>
#include <wx/filedlg.h>
#include <wx/process.h>
#include <wx/txtstrm.h>
#include <wx/panel.h>
#include <wx/bitmap.h>
#include <wx/scrolwin.h>
#include <wx/timer.h>

#include <wx/msw/private.h>
#include <shellapi.h>
#include <winioctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <share.h>
#include <tchar.h>

#include <limits.h>
#include <map>
#include <set>
#include <vector>
#include <list>

#ifndef countof
#define countof(A) (sizeof(A)/sizeof((A)[0]))
#endif

wxString wxGetFileVersion(wxString filename);

inline size_t GetFilePointerEx(HANDLE hFile) {
    LARGE_INTEGER liOfs = { 0 };
    LARGE_INTEGER liNew = { 0 };
    SetFilePointerEx(hFile, liOfs, &liNew, FILE_CURRENT);
    return liNew.QuadPart;
}

#include "../../NkComPort/NkComPort/NkComPort.h"
#include "forms.h"
#include "wxComboBoxEx.h"
#include "wxManTogBtn.h"
#include "wxLogFile.h"

typedef unsigned char byte;
typedef unsigned short word;

inline size_t wxGetFileLength(HANDLE file)
{
    LARGE_INTEGER li = { 0,0 };
	GetFileSizeEx(file,&li);
    return li.QuadPart;
}

enum EMODE { MODE_CONTROL, MODE_VIEW, MODE_RECORD};
#include "NkDigTimerGraph.h"
#include "NkDigTimer.h"
#include "NkDigTimerUpload.h"

void GetAllPortsInfo(std::map<wxString, wxString>& ports);
wxString GetPortInfo(std::map<wxString, wxString>& ports, wxString port);

