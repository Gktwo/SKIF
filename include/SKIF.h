//
// Copyright 2019-2022 Andon "Kaldaien" Coleman
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

#include <combaseapi.h>
#include <comdef.h>
#include <atlbase.h>

#include <string>
#include <string_view>

#ifndef PLOG_ENABLE_WCHAR_INPUT
#define PLOG_ENABLE_WCHAR_INPUT 1
#endif

#include <plog/Log.h>
#include "plog/Initializers/RollingFileInitializer.h"
#include "plog/Appenders/ConsoleAppender.h"

// Constants - Timers / Window Messages / HotKeys / Windows

const     UINT_PTR       cIDT_REFRESH_PENDING   =             1338;
const     UINT_PTR        IDT_REFRESH_GAMES     =             1340;
const     UINT_PTR        IDT_REFRESH_TOOLTIP   =             1341;
const     UINT_PTR        IDT_REFRESH_UPDATER   =             1342;
const     UINT_PTR        IDT_REFRESH_NOTIFY    =             1343;

constexpr UINT           WM_SKIF_MINIMIZE       = WM_USER +  0x512;
constexpr UINT           WM_SKIF_RESTORE        = WM_USER +  0x513;
constexpr UINT           WM_SKIF_START          = WM_USER + 0x1024;
constexpr UINT           WM_SKIF_TEMPSTART      = WM_USER + 0x1025;
constexpr UINT           WM_SKIF_TEMPSTARTEXIT  = WM_USER + 0x1026;
constexpr UINT           WM_SKIF_LAUNCHER       = WM_USER + 0x1027;
constexpr UINT           WM_SKIF_REFRESHGAMES   = WM_USER + 0x1028;
constexpr UINT           WM_SKIF_STOP           = WM_USER + 0x2048;
constexpr UINT           WM_SKIF_GAMEPAD        = WM_USER + 0x2049;
constexpr UINT           WM_SKIF_COVER          = WM_USER + 0x2050;
constexpr UINT           WM_SKIF_UPDATER        = WM_USER + 0x2051;

constexpr  int           SKIF_HotKey_HDR        = 1337; // Win + Ctrl + Shift + H
constexpr  int           SKIF_HotKey_SVC        = 1338; // Win + Ctrl + Shift + Insert

constexpr const  char*   SKIF_LOG_SEPARATOR     = "----------------------------";
constexpr const wchar_t* SKIF_WindowClass       = L"SKIF_RootWindow";
constexpr const wchar_t* SKIF_NotifyIcoClass    = L"SKIF_NotificationIcon";

// Enums

enum PopupState {
  PopupState_Closed,
  PopupState_Open,
  PopupState_Opened
};

enum UITab {
  UITab_None,
  UITab_Library,
  UITab_Monitor,
  UITab_Settings,
  UITab_About,
  UITab_SmallMode,
  UITab_COUNT      // Total number of elements in enum (technically against Microsoft's enum design guidelines, but whatever)
};

enum UIMode {
  UIMode_Normal,
  UIMode_VRR_Compatibility,
  UIMode_Safe_Mode,
  UIMode_COUNT     // Total number of elements in enum (technically against Microsoft's enum design guidelines, but whatever)
};

// Structs

struct SKIF_Signals { // Used for command line arguments
  BOOL Start                = FALSE;
  BOOL Temporary            = FALSE;
  BOOL Stop                 = FALSE;
  BOOL Quit                 = FALSE;
  BOOL Minimize             = FALSE;
//BOOL Restore              =  TRUE; // Only executed once
  BOOL AddSKIFGame          = FALSE;
  BOOL Launcher             = FALSE;

  // Helper variables
  HWND _RunningInstance     = NULL;
  BOOL _ElevatedService     = FALSE;
  std::wstring _GamePath    = L"";
  std::wstring _GameArgs    = L"";
  std::wstring _GameWorkDir = L"";
};

// External declarations

extern PopupState  ServiceMenu;       // Library / Service Mode: used to show an options menu for the service when right clicking
extern PopupState  AddGamePopup;      // Library: show an  add  custom game prompt
extern PopupState  RemoveGamePopup;   // Library: show a remove custom game prompt
extern PopupState  ModifyGamePopup;   // Library: show a modify custom game prompt
extern PopupState  ConfirmPopup;      // Library: show a confirm prompt with text set through confirmPopupText
extern PopupState  UpdatePromptPopup; // App Mode: show an update prompt
extern PopupState  HistoryPopup;      // Monitor / About: show a changelog popup

extern UITab       SKIF_Tab_Selected; // Current selected tab
extern UITab       SKIF_Tab_ChangeTo; // Tab we want to change to

extern std::pair<UITab, std::vector<HANDLE>> vWatchHandles[UITab_COUNT];

extern HMODULE     hModSpecialK;     // Monitor: Used to dynamically load and unload the Special K DLL file when switching back and forth to the tab
extern HWND        SKIF_ImGui_hWnd;  // Main ImGui platform window (aka the main window of SKIF)
extern HWND        SKIF_Notify_hWnd; // Notification area icon "window" that also doubles as a handler for the stuff previously tied to the now removed SKIF_hWnd 0x0 hidden window

extern float       SKIF_ImGui_GlobalDPIScale;
extern float       SKIF_ImGui_GlobalDPIScale_Last;
extern float       fBottomDist;

extern std::string SKIF_StatusBarText;
extern std::string SKIF_StatusBarHelp;

extern bool        RepopulateGames;
extern bool        RefreshSettingsTab;