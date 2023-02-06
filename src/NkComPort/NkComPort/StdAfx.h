// stdafx.h : include file for standard system include files,
//  or project specific include files that are used frequently, but
//      are changed infrequently
//

#if !defined(AFX_STDAFX_H__ACF168D8_7A9C_4B27_917D_CF4576D7FEC0__INCLUDED_)
#define AFX_STDAFX_H__ACF168D8_7A9C_4B27_917D_CF4576D7FEC0__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

// Insert your headers here
#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <malloc.h>
#include <commctrl.h>
#include <atlconv.h>

#ifndef countof
#define countof(A) (sizeof(A) / sizeof((A)[0]))
#endif

extern HMODULE g_hInstance;

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_STDAFX_H__ACF168D8_7A9C_4B27_917D_CF4576D7FEC0__INCLUDED_)
