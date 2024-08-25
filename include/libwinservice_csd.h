#pragma once

#ifdef UNICODE
#undef UNICODE
#endif

#include <windows.h>
#include <aclapi.h>
#include <tchar.h>

extern SECURITY_ATTRIBUTES CreateSecurityAttribute();
extern void FreeSecurityAttribute(PSECURITY_ATTRIBUTES sa);