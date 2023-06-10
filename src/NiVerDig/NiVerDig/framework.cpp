#include "framework.h"

wxString wxGetFileVersion(wxString filename)
{
    if (!filename.length())
    {
		filename = wxStandardPaths::Get().GetExecutablePath();
    }

    wxString ver;
    wxChar* pc = const_cast<wxChar*>((const wxChar*)filename.t_str());

    DWORD dummy;
    DWORD sizeVerInfo = ::GetFileVersionInfoSize(pc, &dummy);
    if ( sizeVerInfo )
    {
        wxCharBuffer buf(sizeVerInfo);
        if ( ::GetFileVersionInfo(pc, 0, sizeVerInfo, buf.data()) )
        {
            void *pVer;
            UINT sizeInfo;
            if ( ::VerQueryValue(buf.data(),
                                    const_cast<wxChar *>(wxT("\\")),
                                    &pVer,
                                    &sizeInfo) )
            {
                VS_FIXEDFILEINFO *info = (VS_FIXEDFILEINFO *)pVer;
                ver.Printf(wxT("%d.%d.%d.%d"),
                            HIWORD(info->dwFileVersionMS),
                            LOWORD(info->dwFileVersionMS),
                            HIWORD(info->dwFileVersionLS),
                            LOWORD(info->dwFileVersionLS));
            }
        }
    }

    return ver;
}

