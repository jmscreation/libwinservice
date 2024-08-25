#include "libwinservice_csd.h"
#include <iostream>
#include <vector>

SECURITY_ATTRIBUTES CreateSecurityAttribute() {
    DWORD dwRes, dwDisposition;
    PSID pEveryoneSID = NULL, pAdminSID = NULL;
    PACL pACL = NULL;
    PSECURITY_DESCRIPTOR pSD = NULL;
    EXPLICIT_ACCESS ea[2];
    SID_IDENTIFIER_AUTHORITY SIDAuthWorld = SECURITY_WORLD_SID_AUTHORITY;
    SID_IDENTIFIER_AUTHORITY SIDAuthNT = SECURITY_NT_AUTHORITY;

    SECURITY_ATTRIBUTES sa;
    ZeroMemory(&sa, sizeof(sa));

    // Create a well-known SID for the Everyone group.
    if(!AllocateAndInitializeSid(&SIDAuthWorld, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &pEveryoneSID)){
        std::cout << "AllocateAndInitializeSid Error " << GetLastError() << "\n";
        goto Error;
    }

    // Initialize an EXPLICIT_ACCESS structure for an ACE.
    // The ACE will allow Everyone read access.
    ZeroMemory(&ea, 2 * sizeof(EXPLICIT_ACCESS));
    ea[0].grfAccessPermissions = GENERIC_ALL;
    ea[0].grfAccessMode = SET_ACCESS;
    ea[0].grfInheritance= NO_INHERITANCE;
    ea[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[0].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ea[0].Trustee.ptstrName  = (LPTSTR) pEveryoneSID;

    // Create a SID for the BUILTIN\Administrators group.
    if(! AllocateAndInitializeSid(&SIDAuthNT, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &pAdminSID)){
        std::cout << "AllocateAndInitializeSid Error " << GetLastError() << "\n";
        goto Error;
    }

    // Initialize an EXPLICIT_ACCESS structure for an ACE.
    // The ACE will allow the Administrators group full access
    ea[1].grfAccessPermissions = GENERIC_ALL;
    ea[1].grfAccessMode = SET_ACCESS;
    ea[1].grfInheritance= NO_INHERITANCE;
    ea[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[1].Trustee.TrusteeType = TRUSTEE_IS_GROUP;
    ea[1].Trustee.ptstrName  = (LPTSTR) pAdminSID;

    // Create a new ACL that contains the new ACEs.
    dwRes = SetEntriesInAcl(2, ea, NULL, &pACL);
    if(dwRes != ERROR_SUCCESS){
        std::cout << "SetEntriesInAcl Error " << GetLastError() << "\n";
        goto Error;
    }

    // Initialize a security descriptor.  
    pSD = (PSECURITY_DESCRIPTOR) LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
    if(pSD == NULL){
        std::cout << "LocalAlloc Error " << GetLastError() << "\n";
        goto Error; 
    }

    if(!InitializeSecurityDescriptor(pSD, SECURITY_DESCRIPTOR_REVISION)){
        std::cout << "InitializeSecurityDescriptor Error " << GetLastError() << "\n";
        goto Error; 
    }
 
    // Add the ACL to the security descriptor. 
    if(!SetSecurityDescriptorDacl(pSD, TRUE, pACL, FALSE)){
        std::cout << "SetSecurityDescriptorDacl Error " << GetLastError() << "\n";
        goto Error;
    } 

    // Initialize a security attributes structure.
    sa.nLength = sizeof (SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = pSD;
    sa.bInheritHandle = FALSE;

    if(pEveryoneSID)
        FreeSid(pEveryoneSID);
    if(pAdminSID)
        FreeSid(pAdminSID);

    return sa;

    Error:

    if(pEveryoneSID)
        FreeSid(pEveryoneSID);
    if(pAdminSID)
        FreeSid(pAdminSID);
    if(pACL) 
        LocalFree(pACL);
    if(pSD) 
        LocalFree(pSD);

    return sa;
}


void FreeSecurityAttribute(PSECURITY_ATTRIBUTES sa) {
    PSECURITY_DESCRIPTOR pSD = sa->lpSecurityDescriptor;

    PACL pACL = NULL;
    BOOL valid, def;

    if(!GetSecurityDescriptorDacl(pSD, &valid, &pACL, &def)){
        std::cout << "GetSecurityDescriptorDacl failed: " << GetLastError() << "\n";
    }

    if(pACL) 
        LocalFree(pACL);
    if(pSD) 
        LocalFree(pSD);
}