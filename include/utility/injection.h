//
// Copyright 2020-2021 Andon "Kaldaien" Coleman
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//

#pragma once

#include <wtypes.h>
#include <cstdio>
#include <array>
#include <string>

// Singleton struct
struct SKIF_InjectionContext {

  UINT_PTR IDT_REFRESH_PENDING   = 0;

  char    whitelist[MAX_PATH * 16 * 2] = { };
  char    blacklist[MAX_PATH * 16 * 2] = { };

  DWORD   dwLastSignaled    = 0;

  bool    bHasServlet       = false;
  bool    bHasUpdatedFiles  = false;
  bool    bLogonTaskEnabled = false; // Obsolete
  
  bool    bAutoStartSKIF    = false;
  bool    bAutoStartService = false;
  bool    bStartMinimized   = false;

  bool    bAutoStartServiceOnly = false;

  bool    bCurrentState       = false;
  bool    bAckInj             = false; // Indicates if we're using auto-stop mode or not
  bool    bAckInjSignaled     = false; // Indicates that the auto-stop event was signaled
  bool    bTaskbarOverlayIcon = false;

  int     pid32             = 0,
          pid64             = 0;

  enum RunningState
  {
    Stopped  = 0,
    Started  = 1,
    Stopping = 2,
    Starting = 3
  } runState;

  struct pid_file_watch_s {
    std::wstring     wsPidFilename;
          FILE*      fPidFile;
          int*       pPid;
  };

#ifdef _WIN64
  std::array <pid_file_watch_s, 2> records;
#else
  std::array <pid_file_watch_s, 1> records;
#endif

  std::string SKVer32 = "";
  std::string SKVer64 = "";
  std::string SKSvc32 = "";
  std::string SKSvc64 = "";
  
  bool    isPending               (void);
  bool    _StartStopInject        (bool running_, bool autoStop = false, bool elevated = false);
  bool    _TestServletRunlevel    (bool forcedCheck); // Returns true ONLY if we transitioned over from a pending state
  void    _DanceOfTheDLLFiles     (void);
  void    _RefreshSKDLLVersions   (void);
  void    _GlobalInjectionCtl     (void);
  void    _StartAtLogonCtrl       (void);
  void    _SetTaskbarOverlay      (bool show);
  void    _InitializeJumpList     (void);
  bool    _TestUserList           (const char* wszExecutable, bool whitelist_);

  bool    WhitelistPath           (std::string fullPath);
  bool    BlacklistPath           (std::string fullPath);
  bool    WhitelistPattern        (std::string pattern);
  bool    BlacklistPattern        (std::string pattern);
  bool    SaveWhitelist           (void);
  bool    SaveBlacklist           (void);
  bool    LoadWhitelist           (void);
  bool    LoadBlacklist           (void);
  void    SetInjectAckEx          (bool newState);
  void    SetInjectExitAckEx      (bool newState);
  void    SetStopOnInjectionEx    (bool newState);  // Main toggle function
  void    ToggleStopOnInjection   (void);           // Used primarily to switch the registry value

  static SKIF_InjectionContext& GetInstance (void)
  {
      static SKIF_InjectionContext instance;
      return instance;
  }

  SKIF_InjectionContext (SKIF_InjectionContext const&) = delete; // Delete copy constructor
  SKIF_InjectionContext (SKIF_InjectionContext&&)      = delete; // Delete move constructor
  
private:
  SKIF_InjectionContext           (void);
  bool    AddUserListPath         (std::string fullPath,      bool whitelist_);
  bool    AddUserListPattern      (std::string pattern,       bool whitelist_);
  bool    SaveUserList            (bool whitelist_);
  bool    LoadUserList            (bool whitelist_);
};