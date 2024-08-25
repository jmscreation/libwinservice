#include "elevation.h"

bool IsElevated() {
    HANDLE handle = GetCurrentProcess();
    BOOL fRet = FALSE;
    HANDLE hToken = NULL;
    if( OpenProcessToken(handle, TOKEN_QUERY, &hToken) ) {
        TOKEN_ELEVATION Elevation;
        DWORD cbSize = sizeof( TOKEN_ELEVATION );
        if( GetTokenInformation( hToken, TokenElevation, &Elevation, sizeof( Elevation ), &cbSize ) ) {
            fRet = Elevation.TokenIsElevated;
        }
    }
    if( hToken ) {
        CloseHandle( hToken );
    }
    return fRet;
}
