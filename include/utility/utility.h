#pragma once

#include <wtypes.h>
#include <string>
#include <WinInet.h>
#include <atlbase.h>
#include <Tlhelp32.h>

#pragma comment (lib, "wininet.lib")

// Generic Utilities

std::string  SKIF_Util_ToLower               (std::string_view  input);
std::wstring SKIF_Util_ToLowerW              (std::wstring_view input);
std::string  SKIF_Util_ToUpper               (std::string_view  input);
std::wstring SKIF_Util_ToUpperW              (std::wstring_view  input);
std::wstring SKIF_Util_GetErrorAsWStr        (DWORD error = GetLastError ( ));
void         SKIF_Util_GetErrorAsMsgBox      (std::wstring winTitle = L"Error detected", std::wstring preMsg = L"", DWORD error = GetLastError());
DWORD        SKIF_Util_timeGetTime           (void);
std::wstring SKIF_Util_timeGetTimeAsWStr     (const std::wstring& format = L"H:M:s.m");
int          SKIF_Util_CompareVersionStrings (std:: string string1, std:: string string2);
int          SKIF_Util_CompareVersionStrings (std::wstring string1, std::wstring string2);

// Filenames

std::string  SKIF_Util_StripInvalidFilenameChars   (std:: string name);
std::wstring SKIF_Util_StripInvalidFilenameChars   (std::wstring name);
std::string  SKIF_Util_ReplaceInvalidFilenameChars (std:: string name,    char replacement);
std::wstring SKIF_Util_ReplaceInvalidFilenameChars (std::wstring name, wchar_t replacement);


// ShellExecute

HINSTANCE    SKIF_Util_ExplorePath                 (const std::wstring_view& path);
HINSTANCE    SKIF_Util_OpenURI                     (const std::wstring_view& path, int nShow = SW_SHOWNORMAL, LPCWSTR verb = L"OPEN", LPCWSTR parameters = NULL, LPCWSTR directory = NULL);


// Windows

int             SKIF_Util_GetBinaryType               (const LPCTSTR pszPathToBinary);
BOOL WINAPI     SKIF_Util_CompactWorkingSet           (void);
HRESULT         SKIF_Util_SetThreadDescription        (HANDLE hThread, PCWSTR lpThreadDescription);
bool            SKIF_Util_IsWindows8Point1OrGreater   (void);
bool            SKIF_Util_IsWindows10OrGreater        (void);
bool            SKIF_Util_IsWindows10v1709OrGreater   (void);
bool            SKIF_Util_IsWindows10v1903OrGreater   (void);
bool            SKIF_Util_IsWindows11orGreater        (void);
bool            SKIF_Util_IsWindowsVersionOrGreater   (DWORD dwMajorVersion, DWORD dwMinorVersion, DWORD dwBuildNumber);
bool            SKIF_Util_IsProcessAdmin              (DWORD PID);
bool            SKIF_Util_IsProcessX86                (HANDLE process);
PROCESSENTRY32W SKIF_Util_FindProcessByName           (const wchar_t* wszName);
bool            SKIF_Util_SaveExtractExeIcon          (std::wstring exePath, std::wstring targetPath);
bool            SKIF_Util_GetControlledFolderAccess   (void);
int             SKIF_Util_RegisterApp                 (bool force   = false);
bool            SKIF_Util_IsMPOsDisabledInRegistry    (bool refresh = false);
void            SKIF_Util_GetMonitorHzPeriod          (HWND hwnd, DWORD dwFlags, DWORD& dwPeriod);

// High Dynamic Range (HDR)

bool            SKIF_Util_IsHDRSupported              (bool refresh = false);
bool            SKIF_Util_IsHDRActive                 (bool refresh = false);
int             SKIF_Util_GetSDRWhiteLevelForHMONITOR (HMONITOR hMonitor);
bool            SKIF_Util_EnableHDROutput             (void);
bool            SKIF_Util_RegisterHotKeyHDRToggle     (void);
bool            SKIF_Util_UnregisterHotKeyHDRToggle   (void);
bool            SKIF_Util_GetHotKeyStateHDRToggle     (void);
bool            SKIF_Util_RegisterHotKeySVCTemp       (void);
bool            SKIF_Util_UnregisterHotKeySVCTemp     (void);
bool            SKIF_Util_GetHotKeyStateSVCTemp       (void);

// Web

struct skif_get_web_uri_t {
  wchar_t wszHostName [INTERNET_MAX_HOST_NAME_LENGTH] = { };
  wchar_t wszHostPath [INTERNET_MAX_PATH_LENGTH]      = { };
  wchar_t wszLocalPath[MAX_PATH + 2]                  = { };
  LPCWSTR method                                      = L"GET";
  bool         https                                  = false;
  std::string  body;
  std::wstring header;
};

DWORD WINAPI SKIF_Util_GetWebUri              (skif_get_web_uri_t* get);
DWORD        SKIF_Util_GetWebResource         (std::wstring url, std::wstring_view destination, std::wstring method = L"GET", std::wstring header = L"", std::string body = "");


// Directory Watch

struct SKIF_DirectoryWatch
{
  SKIF_DirectoryWatch  (void) { };
  SKIF_DirectoryWatch  (std::wstring_view wstrPath,
                                     bool bGlobalWait    = false,
                                     bool bWaitAllTabs   = false,
                                     BOOL bWatchSubtree  = FALSE,
                                    DWORD dwNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME);
  ~SKIF_DirectoryWatch (void);

  bool isSignaled      (void);

  bool isSignaled      (std::wstring_view wstrPath,
                                     bool bGlobalWait    = false,
                                     bool bWaitAllTabs   = false,
                                     BOOL bWatchSubtree  = FALSE,
                                    DWORD dwNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME);
  
  void reset           (void);

  HANDLE _hChangeNotification = INVALID_HANDLE_VALUE;
  bool   _bGlobalWait         = false;
  bool   _bWaitAllTabs        = false;
  std::wstring _path          = L""; // Debug stuff only

private:
  void registerNotify  (std::wstring_view wstrPath,
                                     bool bGlobalWait    = false,
                                     bool bWaitAllTabs   = false,
                                     BOOL bWatchSubtree  = FALSE,
                                    DWORD dwNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME);
};


// Registry Watch

struct SKIF_RegistryWatch {
   SKIF_RegistryWatch (HKEY hRootKey,
             const wchar_t* wszSubKey,
             const wchar_t* wszEventName,
                       BOOL bWatchSubtree  = TRUE,
                      DWORD dwNotifyFilter = REG_NOTIFY_CHANGE_LAST_SET,
                       bool bGlobalWait    = false,
                       bool bWOW6432Key    = false);

  ~SKIF_RegistryWatch (void);

  void registerNotify (void);
  void reset          (void);
  bool isSignaled     (void);

  struct {
    HKEY         root        = { };
    std::wstring sub_key;
    BOOL         watch_subtree;
    DWORD        filter_mask;
    BOOL         wow64_32key;
  } _init;

  CRegKey _hKeyBase;
  CHandle _hEvent;
  bool    _bGlobalWait = false;
};


// Shortcuts (*.lnk)

void         SKIF_Util_ResolveShortcut       (HWND hwnd, LPCWSTR lpszLinkFile, LPWSTR lpszTarget, LPWSTR lpszArguments, int iPathBufferSize);
bool         SKIF_Util_CreateShortcut        (LPCWSTR lpszPathLink, LPCWSTR lpszTarget, LPCWSTR lpszArgs = L"\0", LPCWSTR lpszWorkDir = L"\0", LPCWSTR lpszDesc = L"\0", LPCWSTR lpszIconLocation = L"\0", int iIcon = 0);
