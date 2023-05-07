﻿//
// Copyright 2020 - 2022 Andon "Kaldaien" Coleman
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

#include <strsafe.h>
#include <wtypes.h>
#include <dxgi1_5.h>

#include <gsl/gsl_util>

#include <sk_utility/command.h>

#include <SKIF.h>
#include <SKIF_utility.h>
#include <SKIF_imgui.h>

#include <stores/Steam/steam_library.h>

#include "../version.h"
#include <injection.h>
#include <font_awesome.h>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_win32.h>
#include <imgui/imgui_internal.h>
#include <dxgi1_6.h>
#include <xinput.h>

#include <fsutil.h>
#include <psapi.h>

#include <codecvt>
#include <fstream>
#include <random>

#include <filesystem>
#include <concurrent_queue.h>

#include "imgui/d3d11/imgui_impl_dx11.h"
#include <d3d11.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#include <unordered_set>

#include <fonts/fa_regular_400.ttf.h>
#include <fonts/fa_brands_400.ttf.h>
#include <fonts/fa_solid_900.ttf.h>

#include <stores/Steam/app_record.h>

#include <wtypes.h>
#include <WinInet.h>

#include <gsl/gsl>
#include <comdef.h>

#include <sstream>
#include <cwctype>

#include <unordered_set>
#include <json.hpp>

#include <dwmapi.h>
#include <ui_tabs/about.h>
#include <ui_tabs/settings.h>

#include "TextFlow.hpp"

// Registry Settings
#include <registry.h>

static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance( );

#pragma comment (lib, "wininet.lib")

const int SKIF_STEAM_APPID = 1157970;
bool RecreateSwapChains    = false;
bool RepositionSKIF        = false;
bool tinyDPIFonts          = false;
bool invalidateFonts       = false;
bool failedLoadFonts       = false;
bool failedLoadFontsPrompt = false;
DWORD invalidatedFonts     = 0;
bool startedMinimized      = false;
bool SKIF_UpdateReady      = false;
bool showUpdatePrompt      = false;
bool changedUpdateChannel  = false;
bool msgDontRedraw         = false;
bool coverFadeActive       = false;
bool SKIF_Shutdown         = false;
int  startupFadeIn         = 0;

// Custom Global Key States used for moving SKIF around using WinKey + Arrows
bool KeyWinKey = false;
int  SnapKeys  = 0;     // 2 = Left, 4 = Up, 8 = Right, 16 = Down

// Fonts
bool SKIF_bFontChineseSimplified   = false,
     SKIF_bFontChineseAll          = false,
     SKIF_bFontCyrillic            = false,
     SKIF_bFontJapanese            = false,
     SKIF_bFontKorean              = false,
     SKIF_bFontThai                = false,
     SKIF_bFontVietnamese          = false;

// This is used in conjunction with _registry.bMinimizeOnGameLaunch to suppress the "Please start game" notification
bool SKIF_bSuppressServiceNotification = false;

// Graphics options set during runtime
bool SKIF_bCanFlip                 = false, // Flip Sequential               Windows 7 (2013 Platform Update), or Windows 8+
     SKIF_bCanWaitSwapchain        = false, // Waitable Swapchain            Windows 8.1+
     SKIF_bCanFlipDiscard          = false, // Flip Discard                  Windows 10+
     SKIF_bCanAllowTearing         = false, // DWM Tearing                   Windows 10+
     SKIF_bCanHDR                  = false, // High Dynamic Range            Windows 10 1709+ (Build 16299)
     SKIF_bHDREnabled              = false; // HDR Enabled

// Holds swapchain wait handles
std::vector<HANDLE> vSwapchainWaitHandles;

// GOG Galaxy stuff
std::wstring GOGGalaxy_Path        = L"";
std::wstring GOGGalaxy_Folder      = L"";
std::wstring GOGGalaxy_UserID      = L"";
bool GOGGalaxy_Installed           = false;

DWORD    RepopulateGamesWasSet     = 0;
bool     RepopulateGames           = false,
         RefreshSettingsTab        = false;
uint32_t SelectNewSKIFGame         = 0;

bool  HoverTipActive               = false;
DWORD HoverTipDuration             = 0;

// Notification icon stuff
#define SKIF_NOTIFY_ICON                    0x1330 // 4912
#define SKIF_NOTIFY_EXIT                    0x1331 // 4913
#define SKIF_NOTIFY_START                   0x1332 // 4914
#define SKIF_NOTIFY_STOP                    0x1333 // 4915
#define SKIF_NOTIFY_STARTWITHSTOP           0x1334 // 4916
#define WM_SKIF_NOTIFY_ICON      (WM_USER + 0x150) // 1360
bool SKIF_isTrayed = false;
NOTIFYICONDATA niData;
HMENU hMenu;

// Hotkeys
int SKIF_HotKey_HDR = 1337; // Win + Ctrl + Shift + H

// Cmd line argument stuff
struct SKIF_Signals {
  BOOL Stop          = FALSE;
  BOOL Start         = FALSE;
  BOOL Temporary     = FALSE;
  BOOL Quit          = FALSE;
  BOOL Minimize      = FALSE;
  BOOL Restore       =  TRUE;
  BOOL Launcher      = FALSE;
  BOOL AddSKIFGame   = FALSE;

  BOOL _Disowned     = FALSE;
} _Signal;

extern        SK_ICommandProcessor*
  __stdcall SK_GetCommandProcessor (void);

PopupState UpdatePromptPopup = PopupState::Closed;
PopupState HistoryPopup      = PopupState::Closed;
UITab SKIF_Tab_Selected      = UITab_Library,
      SKIF_Tab_ChangeTo      = UITab_None;

HMODULE hModSKIF     = nullptr;
HMODULE hModSpecialK = nullptr;

// Texture related locks to prevent driver crashes
concurrency::concurrent_queue <CComPtr <IUnknown>> SKIF_ResourcesToFree;

/* 2023-04-05: I'm pretty sure this block is unnecessary // Aemony
using  GetSystemMetricsForDpi_pfn = int (WINAPI *)(int, UINT);
static GetSystemMetricsForDpi_pfn
       GetSystemMetricsForDpi = nullptr;

#define SK_BORDERLESS ( WS_VISIBLE | WS_POPUP | WS_MINIMIZEBOX | \
                        WS_SYSMENU )

#define SK_BORDERLESS_EX      ( WS_EX_APPWINDOW | WS_EX_NOACTIVATE )
//#define SK_BORDERLESS_WIN8_EX ( SK_BORDERLESS_EX | WS_EX_NOREDIRECTIONBITMAP ) // We don't support Win8.0 or older

#define SK_FULLSCREEN_X(dpi) (GetSystemMetricsForDpi != nullptr) ? GetSystemMetricsForDpi (SM_CXFULLSCREEN, (dpi)) : GetSystemMetrics (SM_CXFULLSCREEN)
#define SK_FULLSCREEN_Y(dpi) (GetSystemMetricsForDpi != nullptr) ? GetSystemMetricsForDpi (SM_CYFULLSCREEN, (dpi)) : GetSystemMetrics (SM_CYFULLSCREEN)
*/

#define GCL_HICON           (-14)

///* 2023-04-05: I'm pretty sure this block is unnecessary // Aemony
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0 // From Windows SDK 8.1+ headers
#endif


HRESULT
WINAPI
SK_DWM_GetCompositionTimingInfo (DWM_TIMING_INFO *pTimingInfo)
{
  static HMODULE hModDwmApi =
    LoadLibraryW (L"dwmapi.dll");

  typedef HRESULT (WINAPI *DwmGetCompositionTimingInfo_pfn)(
                   HWND             hwnd,
                   DWM_TIMING_INFO *pTimingInfo);

  static                   DwmGetCompositionTimingInfo_pfn
                           DwmGetCompositionTimingInfo =
         reinterpret_cast <DwmGetCompositionTimingInfo_pfn> (
      GetProcAddress ( hModDwmApi,
                          "DwmGetCompositionTimingInfo" )   );

  pTimingInfo->cbSize =
    sizeof (DWM_TIMING_INFO);

  return
    DwmGetCompositionTimingInfo ( 0, pTimingInfo );
}
//*/

float fAspect     = 16.0f / 9.0f;
float fBottomDist = 0.0f;

ID3D11Device*           g_pd3dDevice           = nullptr;
ID3D11DeviceContext*    g_pd3dDeviceContext    = nullptr;
//IDXGISwapChain*         g_pSwapChain           = nullptr;
ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
BOOL                    bOccluded              =   FALSE;

// Forward declarations of helper functions
bool CreateDeviceD3D           (HWND hWnd);
void CleanupDeviceD3D          (void);
//void CreateRenderTarget        (void);
//void CleanupRenderTarget       (void);
//void ResizeSwapChain           (HWND hWnd, int width, int height);
LRESULT WINAPI
     SKIF_WndProc              (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI
     SKIF_Notify_WndProc       (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

class SKIF_AutoWndClass {
public:
   SKIF_AutoWndClass (WNDCLASSEX wc) : wc_ (wc) { };
  ~SKIF_AutoWndClass (void)
  {
    UnregisterClass ( wc_.lpszClassName,
                      wc_.hInstance );
  }

private:
  WNDCLASSEX wc_;
};

bool    bExitOnInjection = false; // Used to exit SKIF on a successful injection if it's used merely as a launcher
CHandle hInjectAck (0);           // Signalled when a game finishes injection
//CHandle hSwapWait  (0);           // Signalled by a waitable swapchain

//int __width  = 0;
//int __height = 0;

// Holds current global DPI scaling, 1.0f == 100%, 1.5f == 150%.
// Can go below 1.0f if SKIF is shown on a smaller screen with less than 1000px in height.
float SKIF_ImGui_GlobalDPIScale      = 1.0f;
// Holds last frame's DPI scaling
float SKIF_ImGui_GlobalDPIScale_Last = 1.0f;

std::string SKIF_StatusBarText = "";
std::string SKIF_StatusBarHelp = "";
HWND        SKIF_hWnd          =  0;
HWND        SKIF_ImGui_hWnd    =  0;
HWND        SKIF_Notify_hWnd   =  0;

CONDITION_VARIABLE SKIF_IsFocused    = { };
//CONDITION_VARIABLE SKIF_IsNotFocused = { };

void
SKIF_ImGui_MissingGlyphCallback (wchar_t c)
{
  static UINT acp = GetACP();

  static std::unordered_set <wchar_t>
      unprintable_chars;
  if (unprintable_chars.emplace (c).second)
  {
    using range_def_s =
      std::pair <const ImWchar*, bool *>;

    static       auto pFonts = ImGui::GetIO ().Fonts;

    static const auto ranges =
      { // Sorted from least numer of unique characters to the most
        range_def_s { pFonts->GetGlyphRangesVietnamese              (), &SKIF_bFontVietnamese        },
        range_def_s { pFonts->GetGlyphRangesCyrillic                (), &SKIF_bFontCyrillic          },
        range_def_s { pFonts->GetGlyphRangesThai                    (), &SKIF_bFontThai              },
      ((acp == 932) // Prioritize Japanese for ACP 932
      ? range_def_s { pFonts->GetGlyphRangesJapanese                (), &SKIF_bFontJapanese          }
      : range_def_s { pFonts->GetGlyphRangesChineseSimplifiedCommon (), &SKIF_bFontChineseSimplified }),
      ((acp == 932)
      ? range_def_s { pFonts->GetGlyphRangesChineseSimplifiedCommon (), &SKIF_bFontChineseSimplified }
      : range_def_s { pFonts->GetGlyphRangesJapanese                (), &SKIF_bFontJapanese          }),
        range_def_s { pFonts->GetGlyphRangesKorean                  (), &SKIF_bFontKorean            }
#ifdef _WIN64
      // 32-bit SKIF breaks if too many character sets are
      //   loaded so omit Chinese Full on those versions.
      , range_def_s { pFonts->GetGlyphRangesChineseFull             (), &SKIF_bFontChineseAll        }
#endif
      };

    for ( const auto &[span, enable] : ranges)
    {
      ImWchar const *sp =
        &span [2];

      while (*sp != 0x0)
      {
        if ( c <= (wchar_t)(*sp++) &&
             c >= (wchar_t)(*sp++) )
        {
           sp             = nullptr;
          *enable         = true;
          invalidateFonts = true;

          break;
        }
      }

      if (sp == nullptr)
        break;
    }
  }
}

const ImWchar*
SK_ImGui_GetGlyphRangesDefaultEx (void)
{
  static const ImWchar ranges [] =
  {
    0x0020,  0x00FF, // Basic Latin + Latin Supplement
    0x0100,  0x03FF, // Latin, IPA, Greek
    0x2000,  0x206F, // General Punctuation
    0x2100,  0x21FF, // Letterlike Symbols
    0x2600,  0x26FF, // Misc. Characters
    0x2700,  0x27BF, // Dingbats
    0x207f,  0x2090, // N/A (literally, the symbols for N/A :P)
    0xc2b1,  0xc2b3, // ²
    0
  };
  return &ranges [0];
}

const ImWchar*
SK_ImGui_GetGlyphRangesKorean (void)
{
  static const ImWchar ranges[] =
  {
      0x0020, 0x00FF, // Basic Latin + Latin Supplement
      0x3131, 0x3163, // Korean alphabets
//#ifdef _WIN64
      0xAC00, 0xD7A3, // Korean characters (Hangul syllables) -- should not be included on 32-bit OSes due to system limitations
//#endif
      0,
  };
  return &ranges[0];
}

const ImWchar*
SK_ImGui_GetGlyphRangesFontAwesome (void)
{
  static const ImWchar ranges [] =
  {
    ICON_MIN_FA, ICON_MAX_FA,
    0 // 🔗, 🗙
  };
  return &ranges [0];
}

auto SKIF_ImGui_LoadFont =
   []( const std::wstring& filename,
             float         point_size,
       const ImWchar*      glyph_range,
             ImFontConfig* cfg = nullptr )
{
  auto& io =
    ImGui::GetIO ();

  wchar_t wszFullPath [ MAX_PATH + 2 ] = { };

  //OutputDebugString(L"Input: ");
  //OutputDebugString(filename.c_str());
  //OutputDebugString(L"\n");

  if (GetFileAttributesW (              filename.c_str ()) != INVALID_FILE_ATTRIBUTES)
     wcsncpy_s ( wszFullPath, MAX_PATH, filename.c_str (),
                             _TRUNCATE );

  else
  {
    wchar_t     wszFontsDir [MAX_PATH] = { };
    wcsncpy_s ( wszFontsDir, MAX_PATH,
             SK_GetFontsDir ().c_str (), _TRUNCATE );

    PathCombineW ( wszFullPath,
                   wszFontsDir, filename.c_str () );

    if (GetFileAttributesW (wszFullPath) == INVALID_FILE_ATTRIBUTES)
      *wszFullPath = L'\0';
  }

  //OutputDebugString(L"Font path: ");
  //OutputDebugString(wszFullPath);
  //OutputDebugString(L"\n");

  if (*wszFullPath != L'\0')
  {
    return
      io.Fonts->AddFontFromFileTTF ( SK_WideCharToUTF8 (wszFullPath).c_str (),
                                       point_size,
                                         cfg,
                                           glyph_range );
  }

  return (ImFont *)nullptr;
};

ImFont* fontConsolas = nullptr;

void
SKIF_ImGui_InitFonts (float fontSize)
{
  static UINT acp = GetACP();

  auto& io =
    ImGui::GetIO ();

  extern ImGuiContext *GImGui;

  if (io.Fonts != nullptr)
  {
    if (GImGui->FontAtlasOwnedByContext)
    {
      if (GImGui->Font != nullptr)
      {
        GImGui->Font->ClearOutputData ();

        if (GImGui->Font->ContainerAtlas != nullptr)
            GImGui->Font->ContainerAtlas->Clear ();
      }

      io.FontDefault = nullptr;

      IM_DELETE (io.Fonts);
                 io.Fonts = IM_NEW (ImFontAtlas)();
    }
  }

  ImFontConfig
  font_cfg           = {  };
  font_cfg.MergeMode = true;
  
  std::filesystem::path fontDir
          (path_cache.specialk_userdata);

  fontDir /= L"Fonts";

  std::wstring standardFont = (tinyDPIFonts) ? L"Verdana.ttf" :  L"msyh.ttc";

  std::error_code ec;
  // Create any missing directories
  if (! std::filesystem::exists (            fontDir, ec))
        std::filesystem::create_directories (fontDir, ec);

  // Core character set
  SKIF_ImGui_LoadFont     (standardFont, fontSize, SK_ImGui_GetGlyphRangesDefaultEx());
  //SKIF_ImGui_LoadFont     ((fontDir / L"NotoSans-Regular.ttf"), fontSize, SK_ImGui_GetGlyphRangesDefaultEx());

  // Load extended character sets when SKIF is not used as a launcher
  if (! _Signal.Launcher || _Signal.AddSKIFGame)
  {
    // Cyrillic character set
    if (SKIF_bFontCyrillic)
      SKIF_ImGui_LoadFont   (standardFont,   fontSize, io.Fonts->GetGlyphRangesCyrillic                 (), &font_cfg);
      //SKIF_ImGui_LoadFont   ((fontDir / L"NotoSans-Regular.ttf"), fontSize, io.Fonts->GetGlyphRangesCyrillic        (), &font_cfg);
  
    // Japanese character set
    // Load before Chinese for ACP 932 so that the Japanese font is not overwritten
    if (SKIF_bFontJapanese && acp == 932)
    {
      //SKIF_ImGui_LoadFont ((fontDir / L"NotoSansJP-Regular.ttf"), fontSize, io.Fonts->GetGlyphRangesJapanese        (), &font_cfg);
      ///*
      if (SKIF_Util_IsWindows10OrGreater ( ))
        SKIF_ImGui_LoadFont (L"YuGothR.ttc",  fontSize, io.Fonts->GetGlyphRangesJapanese                (), &font_cfg);
      else
        SKIF_ImGui_LoadFont (L"yugothic.ttf", fontSize, io.Fonts->GetGlyphRangesJapanese                (), &font_cfg);
      //*/
    }

    // Simplified Chinese character set
    // Also includes almost all of the Japanese characters except for some Kanjis
    if (SKIF_bFontChineseSimplified)
      SKIF_ImGui_LoadFont   (L"msyh.ttc",     fontSize, io.Fonts->GetGlyphRangesChineseSimplifiedCommon (), &font_cfg);
      //SKIF_ImGui_LoadFont ((fontDir / L"NotoSansSC-Regular.ttf"), fontSize, io.Fonts->GetGlyphRangesChineseSimplifiedCommon        (), &font_cfg);

    // Japanese character set
    // Load after Chinese for the rest of ACP's so that the Chinese font is not overwritten
    if (SKIF_bFontJapanese && acp != 932)
    {
      //SKIF_ImGui_LoadFont ((fontDir / L"NotoSansJP-Regular.ttf"), fontSize, io.Fonts->GetGlyphRangesJapanese        (), &font_cfg);
      ///*
      if (SKIF_Util_IsWindows10OrGreater ( ))
        SKIF_ImGui_LoadFont (L"YuGothR.ttc",  fontSize, io.Fonts->GetGlyphRangesJapanese                (), &font_cfg);
      else
        SKIF_ImGui_LoadFont (L"yugothic.ttf", fontSize, io.Fonts->GetGlyphRangesJapanese                (), &font_cfg);
      //*/
    }
    
    // All Chinese character sets
    if (SKIF_bFontChineseAll)
      SKIF_ImGui_LoadFont   (L"msjh.ttc",     fontSize, io.Fonts->GetGlyphRangesChineseFull             (), &font_cfg);
      //SKIF_ImGui_LoadFont ((fontDir / L"NotoSansTC-Regular.ttf"), fontSize, io.Fonts->GetGlyphRangesChineseFull        (), &font_cfg);

    // Korean character set
    // On 32-bit builds this does not include Hangul syllables due to system limitaitons
    if (SKIF_bFontKorean)
      SKIF_ImGui_LoadFont   (L"malgun.ttf",   fontSize, SK_ImGui_GetGlyphRangesKorean                   (), &font_cfg);
      //SKIF_ImGui_LoadFont ((fontDir / L"NotoSansKR-Regular.ttf"), fontSize, io.Fonts->SK_ImGui_GetGlyphRangesKorean        (), &font_cfg);

    // Thai character set
    if (SKIF_bFontThai)
      SKIF_ImGui_LoadFont   (standardFont,   fontSize, io.Fonts->GetGlyphRangesThai                    (), &font_cfg);
      //SKIF_ImGui_LoadFont   ((fontDir / L"NotoSansThai-Regular.ttf"),   fontSize, io.Fonts->GetGlyphRangesThai      (), &font_cfg);

    // Vietnamese character set
    if (SKIF_bFontVietnamese)
      SKIF_ImGui_LoadFont   (standardFont,   fontSize, io.Fonts->GetGlyphRangesVietnamese              (), &font_cfg);
      //SKIF_ImGui_LoadFont   ((fontDir / L"NotoSans-Regular.ttf"),   fontSize, io.Fonts->GetGlyphRangesVietnamese    (), &font_cfg);
  }

  static auto
    skif_fs_wb = ( std::ios_base::binary
                 | std::ios_base::out  );

  auto _UnpackFontIfNeeded =
  [&]( const char*   szFont,
       const uint8_t akData [],
       const size_t  cbSize )
  {
    if (! std::filesystem::is_regular_file ( fontDir / szFont, ec)        )
                     std::ofstream ( fontDir / szFont, skif_fs_wb ).
      write ( reinterpret_cast <const char *> (akData),
                                               cbSize);
  };

  auto      awesome_fonts = {
    std::make_tuple (
      FONT_ICON_FILE_NAME_FAR, fa_regular_400_ttf,
                   _ARRAYSIZE (fa_regular_400_ttf) ),
    std::make_tuple (
      FONT_ICON_FILE_NAME_FAS, fa_solid_900_ttf,
                   _ARRAYSIZE (fa_solid_900_ttf) ),
    std::make_tuple (
      FONT_ICON_FILE_NAME_FAB, fa_brands_400_ttf,
                   _ARRAYSIZE (fa_brands_400_ttf) )
                            };

  std::for_each (
            awesome_fonts.begin (),
            awesome_fonts.end   (),
    [&](const auto& font)
    {        _UnpackFontIfNeeded (
      std::get <0> (font),
      std::get <1> (font),
      std::get <2> (font)        );
     SKIF_ImGui_LoadFont (
                    fontDir/
      std::get <0> (font),
                    fontSize - 2.0f,
        SK_ImGui_GetGlyphRangesFontAwesome (),
                   &font_cfg
                         );
    }           );

  io.Fonts->AddFontDefault ();

  fontConsolas = SKIF_ImGui_LoadFont (L"msyh.ttc", fontSize - 4.0f, SK_ImGui_GetGlyphRangesDefaultEx());
  //fontConsolas = SKIF_ImGui_LoadFont ((fontDir / L"NotoSansMono-Regular.ttf"), fontSize/* - 4.0f*/, SK_ImGui_GetGlyphRangesDefaultEx());
}


ImGuiStyle SKIF_ImGui_DefaultStyle;

HWND hWndOrigForeground;

void
SKIF_ProxyCommandAndExitIfRunning (LPWSTR lpCmdLine)
{
  PLOG_DEBUG << u8"正在处理命令行参数: " << lpCmdLine;

  HWND hwndAlreadyExists =
    FindWindowExW (0, 0, SKIF_WindowClass, nullptr);

  if (hwndAlreadyExists != 0)
    PLOG_VERBOSE << "hwndAlreadyExists: " << hwndAlreadyExists;

  _Signal.Start =
    StrStrIW (lpCmdLine, L"Start")    != NULL;

  _Signal.Temporary =
    StrStrIW (lpCmdLine, L"Temp")     != NULL;

  _Signal.Stop =
    StrStrIW (lpCmdLine, L"Stop")     != NULL;

  _Signal.Quit =
    StrStrIW (lpCmdLine, L"Quit")     != NULL;

  _Signal.Minimize =
    StrStrIW (lpCmdLine, L"Minimize") != NULL;

  _Signal.AddSKIFGame =
    StrStrIW (lpCmdLine, L"AddGame=") != NULL;

  // Both AddSKIFGame and Launcher is expected to include .exe in the argument,
  //   so only set Launcher if AddSKIFGame is false.
  if (! _Signal.AddSKIFGame)
    _Signal.Launcher =
      StrStrIW (lpCmdLine, L".exe")     != NULL;

  if ( (  hwndAlreadyExists != 0 ) &&
       ( ! _Signal.Launcher )      &&
       ( ! _Signal.AddSKIFGame)    &&
       ((! _registry.bAllowMultipleInstances)        ||
                                   _Signal.Stop || _Signal.Start    ||
                                   _Signal.Quit || _Signal.Minimize
       )
     )
  {
    PLOG_VERBOSE << "hwndAlreadyExists was found to be true; proxying call...";

    if (! _Signal.Start     &&
        ! _Signal.Temporary &&
        ! _Signal.Stop      &&
        ! _Signal.Quit      &&
        ! _Signal.Minimize  &&
        ! _Signal.Launcher)
    {
      //if (IsIconic        (hwndAlreadyExists))
      //  ShowWindow        (hwndAlreadyExists, SW_SHOWNA);
      
      PostMessage (hwndAlreadyExists, WM_SKIF_RESTORE, 0x0, 0x0);
      //SetForegroundWindow (hwndAlreadyExists);
    }

    if (_Signal.Stop)
      PostMessage (hwndAlreadyExists, WM_SKIF_STOP, 0x0, 0x0);

    if (_Signal.Quit)
      PostMessage (hwndAlreadyExists, WM_CLOSE, 0x0, 0x0);

    if (_Signal.Start)
      PostMessage (hwndAlreadyExists, (_Signal.Temporary) ? WM_SKIF_TEMPSTART : WM_SKIF_START, 0x0, 0x0);
    
    if (_Signal.Minimize)
      PostMessage (hwndAlreadyExists, WM_SKIF_MINIMIZE, 0x0, 0x0);

    else {
      if (IsIconic        (hWndOrigForeground))
        ShowWindow        (hWndOrigForeground, SW_SHOWNA);
      SetForegroundWindow (hWndOrigForeground);
    }

    if (_Signal.Quit || (! _Signal._Disowned))
    {
      PLOG_INFO << "Terminating due to one of these contions were found to be true:";
      PLOG_INFO << "_Signal.Quit: "        << (  _Signal.Quit     );
      PLOG_INFO << "! _Signal._Disowned: " << (! _Signal._Disowned);
      ExitProcess (0x0);
    }
  }

  else if (_Signal.Quit)
  {
    PLOG_VERBOSE << "hwndAlreadyExists was found to be false; handling call...";

    if (_Signal.Stop)
      _inject._StartStopInject (true);
    
    PLOG_INFO << "Terminating due to _Signal.Quit";
    ExitProcess (0x0);
  }

  // Handle adding custom game
  if (_Signal.AddSKIFGame)
  {
    PLOG_VERBOSE << "SKIF being used to add a custom game to SKIF...";
    // O:\WindowsApps\DevolverDigital.MyFriendPedroWin10_1.0.6.0_x64__6kzv4j18v0c96\MyFriendPedro.exe

    std::wstring cmdLine        = std::wstring(lpCmdLine);
    std::wstring cmdLineArgs    = cmdLine;

    // Transform to lowercase
    std::wstring cmdLineLower   = SKIF_Util_TowLower (cmdLine);

    std::wstring splitPos1Lower = L"addgame="; // Start split
    std::wstring splitEXELower  = L".exe";     // Stop split (exe)
    std::wstring splitLNKLower  = L".lnk";     // Stop split (lnk)

    // Exclude anything before "addgame=", if any
    cmdLine = cmdLine.substr(cmdLineLower.find(splitPos1Lower) + splitPos1Lower.length());

    // First position is a space -- skip that one
    if (cmdLine.find(L" ") == 0)
      cmdLine = cmdLine.substr(1);

    // First position is a quotation mark -- we need to strip those
    if (cmdLine.find(L"\"") == 0)
      cmdLine = cmdLine.substr(1, cmdLine.find(L"\"", 1) - 1) + cmdLine.substr(cmdLine.find(L"\"", 1) + 1, std::wstring::npos);

    // Update lowercase
    cmdLineLower   = SKIF_Util_TowLower (cmdLine);

    // If .exe is part of the string
    if (cmdLineLower.find(splitEXELower) != std::wstring::npos)
    {
      // Extract proxied arguments, if any
      cmdLineArgs = cmdLine.substr(cmdLineLower.find(splitEXELower) + splitEXELower.length());

      // Exclude anything past ".exe"
      cmdLine = cmdLine.substr(0, cmdLineLower.find(splitEXELower) + splitEXELower.length());
    }

    // If .lnk is part of the string
    else if (cmdLineLower.find(splitLNKLower) != std::wstring::npos)
    {
      // Exclude anything past ".lnk" since we're reading the arguments from the shortcut itself
      cmdLine = cmdLine.substr(0, cmdLineLower.find(splitLNKLower) + splitLNKLower.length());
      
      WCHAR wszTarget   [MAX_PATH];
      WCHAR wszArguments[MAX_PATH];

      SKIF_Util_ResolveShortcut (SKIF_hWnd, cmdLine.c_str(), wszTarget, wszArguments, MAX_PATH);

      cmdLine     = std::wstring(wszTarget);
      cmdLineArgs = std::wstring(wszArguments);
    }

    // Clear var if no valid path was found
    else {
      cmdLine.clear();
    }

    // Only proceed if we have an actual valid path
    if (cmdLine.length() > 0)
    {
      // First position of the arguments is a space -- skip that one
      if (cmdLineArgs.find(L" ") == 0)
        cmdLineArgs = cmdLineArgs.substr(1);

      extern std::wstring SKIF_GetProductName    (const wchar_t* wszName);
      extern int          SKIF_AddCustomAppID    (std::vector<std::pair<std::string, app_record_s>>* apps,
                                                  std::wstring name, std::wstring path, std::wstring args);
      extern
        std::vector <
          std::pair < std::string, app_record_s >
                    > apps;

      if (PathFileExists (cmdLine.c_str()))
      {
        std::wstring productName = SKIF_GetProductName (cmdLine.c_str());

        if (productName == L"")
          productName = std::filesystem::path (cmdLine).replace_extension().filename().wstring();

        SelectNewSKIFGame = (uint32_t)SKIF_AddCustomAppID (&apps, productName, cmdLine, cmdLineArgs);
    
        // If a running instance of SKIF already exists, terminate this one as it has served its purpose
        if (SelectNewSKIFGame > 0 && hwndAlreadyExists != 0)
        {
          SendMessage (hwndAlreadyExists, WM_SKIF_REFRESHGAMES, SelectNewSKIFGame, 0x0);
          PLOG_INFO << "Terminating due to one of these contions were found to be true:";
          PLOG_INFO << "SelectNewSKIFGame > 0: "  << (SelectNewSKIFGame  > 0);
          PLOG_INFO << "hwndAlreadyExists != 0: " << (hwndAlreadyExists != 0);
          ExitProcess (0x0);
        }
      }
    }

    // Terminate the process if given a non-valid string
    else {
      PLOG_INFO << "Terminating due to given a non-valid string!";
      ExitProcess (0x0);
    }
  }
  
  // Handle quick launching
  else if (_Signal.Launcher)
  {
    PLOG_VERBOSE << "SKIF being used as a launcher...";

    // Display in small mode
    _registry.bSmallMode = true;

    std::wstring cmdLine        = std::wstring(lpCmdLine);
    std::wstring delimiter      = L".exe"; // split lpCmdLine at the .exe

    // First position is a quotation mark -- we need to strip those
    if (cmdLine.find(L"\"") == 0)
      cmdLine = cmdLine.substr(1, cmdLine.find(L"\"", 1) - 1) + cmdLine.substr(cmdLine.find(L"\"", 1) + 1, std::wstring::npos);

    // Transform to lowercase
    std::wstring cmdLineLower = SKIF_Util_TowLower (cmdLine);

    // Extract the target path and any proxied command line arguments
    std::wstring path           = cmdLine.substr(0, cmdLineLower.find(delimiter) + delimiter.length());                        // path
    std::wstring proxiedCmdLine = cmdLine.substr(   cmdLineLower.find(delimiter) + delimiter.length(), cmdLineLower.length()); // proxied command line

    // Path does not seem to be absolute -- add the current working directory in front of the path
    if (path.find(L"\\") == std::wstring::npos)
      path = SK_FormatStringW (LR"(%ws\%ws)", path_cache.skif_workdir_org, path.c_str()); //orgWorkingDirectory.wstring() + L"\\" + path;

    std::wstring workingDirectory = std::filesystem::path(path).parent_path().wstring();               // path to the parent folder                              

    PLOG_VERBOSE << "Executable:        " << path;
    PLOG_VERBOSE << "Working Directory: " << workingDirectory;

    bool isLocalBlacklisted  = false,
         isGlobalBlacklisted = false;

    if (PathFileExistsW (path.c_str()))
    {
      std::wstring blacklistFile = SK_FormatStringW (L"%s\\SpecialK.deny.%ws",
                                                     std::filesystem::path(path).parent_path().wstring().c_str(),                 // full path to parent folder
                                                     std::filesystem::path(path).filename().replace_extension().wstring().c_str() // filename without extension
      );

      // Check if the executable is blacklisted
      isLocalBlacklisted  = PathFileExistsW (blacklistFile.c_str());
      isGlobalBlacklisted = _inject._TestUserList (SK_WideCharToUTF8(path).c_str(), false);

      if (! isLocalBlacklisted &&
          ! isGlobalBlacklisted)
      {
        // Whitelist the path if it haven't been already
        _inject._WhitelistBasedOnPath (SK_WideCharToUTF8(path));

        if (hwndAlreadyExists != 0)
          SendMessage (hwndAlreadyExists, WM_SKIF_LAUNCHER, 0x0, 0x0);

        else if (! _inject.bCurrentState)
        {
          bExitOnInjection = true;
          _inject._StartStopInject (false, true);
        }
      }

      SHELLEXECUTEINFOW
        sexi              = { };
        sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
        sexi.lpVerb       = L"OPEN";
        sexi.lpFile       = path.c_str();
        sexi.lpParameters = proxiedCmdLine.c_str();
        sexi.lpDirectory  = workingDirectory.c_str();
        sexi.nShow        = SW_SHOW;
        sexi.fMask        = SEE_MASK_FLAG_NO_UI |
                            SEE_MASK_NOASYNC    | SEE_MASK_NOZONECHECKS;

      // Launch executable
      ShellExecuteExW (&sexi);
      
      PLOG_INFO << "Launched the given executable!";
    }

    // If a running instance of SKIF already exists, or the game was blacklisted, terminate this one as it has served its purpose
    if (hwndAlreadyExists != 0 || isLocalBlacklisted || isGlobalBlacklisted)
    {
      PLOG_INFO << "Terminating due to one of these contions were found to be true:";
      PLOG_INFO << "hwndAlreadyExists != 0: " << (hwndAlreadyExists != 0);
      PLOG_INFO << "isLocalBlacklisted: "     << (isLocalBlacklisted    );
      PLOG_INFO << "isGlobalBlacklisted: "    << (isGlobalBlacklisted   );
      ExitProcess (0x0);
    }
  }
}

int
SKIF_RegisterApp (bool force = false)
{
  static int ret = -1;

  if (ret != -1 && ! force)
    return ret;

  if (! _inject.bHasServlet)
  {
    PLOG_ERROR << "Missing critical service components!";
    return -1;
  }

  std::wstring wsExePath = std::wstring (path_cache.skif_executable);

  if (_registry.wsPath            == path_cache.specialk_userdata &&
      _registry.wsAppRegistration == wsExePath)
  {
    ret = 1;
  }

  else if (force || _registry.wsPath.empty() || _registry.wsAppRegistration.empty())
  {
    ret = 1;

    if (_registry.regKVAppRegistration.putData (wsExePath))
      PLOG_INFO << "App registration was successful: " << wsExePath;
    else
    {
      PLOG_ERROR << "Failed to register SKIF in Windows";
      ret = 0;
    }

    if (_registry.regKVPath.putData (path_cache.specialk_userdata))
      PLOG_INFO << "Updated central Special K userdata location: " << path_cache.specialk_userdata;
    else
    {
      PLOG_ERROR << "Failed to update the central Special K userdata location!";
      ret = 0;
    }

    /*
    if (SKIF_Util_CreateShortcut (
                linkPath.c_str(),
                wszPath,
                linkArgs.c_str(),
                pApp->launch_configs[0].working_dir.c_str(),
                SK_UTF8ToWideChar(name).c_str(),
                pApp->launch_configs[0].getExecutableFullPath(pApp->id).c_str()
                ))
                */
  }

  return ret;
}

bool
SKIF_hasControlledFolderAccess (void)
{
  if (! SKIF_Util_IsWindows10OrGreater ( ))
    return false;

  HKEY hKey;
  DWORD buffer = 0;
  unsigned long size = 1024;
  bool enabled = false;

  // Check if Controlled Folder Access is enabled
  if (ERROR_SUCCESS == RegOpenKeyExW (HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows Defender\Windows Defender Exploit Guard\Controlled Folder Access\)", 0, KEY_READ, &hKey))
  {
    if (ERROR_SUCCESS == RegQueryValueEx (hKey, L"EnableControlledFolderAccess", NULL, NULL, (LPBYTE)&buffer, &size))
      enabled = buffer;

    RegCloseKey (hKey);
  }

  if (enabled)
  {
    // Regular users / unelevated processes has read access to this key on Windows 10, but not on Windows 11.
    if (ERROR_SUCCESS == RegOpenKeyExW (HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows Defender\Windows Defender Exploit Guard\Controlled Folder Access\AllowedApplications\)", 0, KEY_READ, &hKey))
    {
      static TCHAR               szExePath[MAX_PATH];
      GetModuleFileName   (NULL, szExePath, _countof(szExePath));

      if (ERROR_SUCCESS == RegQueryValueEx (hKey, szExePath, NULL, NULL, NULL, NULL))
        enabled = false;

      RegCloseKey(hKey);
    }
  }

  return enabled;
}


void SKIF_GetMonitorRefreshRatePeriod (HWND hwnd, DWORD dwFlags, DWORD& dwPeriod)
{
  DEVMODE 
    dm        = { };
    dm.dmSize = sizeof (DEVMODE);
    
  HMONITOR
      hMonitor  = MonitorFromWindow (hwnd, dwFlags);
  if (hMonitor != NULL)
  {
    MONITORINFOEX
      minfoex        = { };
      minfoex.cbSize = sizeof (MONITORINFOEX);

    if (GetMonitorInfo (hMonitor, (LPMONITORINFOEX)&minfoex))
      if (EnumDisplaySettings (minfoex.szDevice, ENUM_CURRENT_SETTINGS, &dm))
        dwPeriod = (1000 / dm.dmDisplayFrequency);

    if (dwPeriod == 0)
      dwPeriod = 16; // In case we go too low, use 16 ms (60 Hz) to prevent division by zero later
  }
}


void SKIF_putStopOnInjection (bool in)
{
  _registry.regKVDisableStopOnInjection.putData(!in);

  // If we're disabling the feature, also disable the legacy key
  if (in)
    _registry.regKVLegacyDisableStopOnInjection.putData(!in);

  if (_inject.bCurrentState)
    _inject._ToggleOnDemand (in);
}

std::string patrons;
std::atomic<int> update_thread         = 0; // 0 = No update check has run,            1 = Update check is running,       2 = Update check has completed
std::atomic<int> update_thread_new     = 0; // 0 = No new update check is needed run,  1 = A new update check should start if the current one is complete
std::atomic<int> update_thread_patreon = 0; // 0 = patrons.txt is not ready,           1 = patrons.txt is ready 

std::vector <std::pair<std::string, std::string>> updateChannels;

std::string SKIF_GetPatrons ( )
{
  if (update_thread_patreon.load ( ) == 1)
    return patrons;
  else
    return "";
}

SKIF_UpdateCheckResults SKIF_CheckForUpdates()
{
  if (_Signal.Launcher            ||
      _Signal.Temporary           ||
      _Signal.Stop                ||
      _Signal.Quit)
    return SKIF_UpdateCheckResults();

  static SKIF_UpdateCheckResults results;

  if (update_thread.load ( ) == 0)
  {
    update_thread.store (1);

    if (changedUpdateChannel)
    {
      results.filename.clear();
      results.description.clear();
      results.history.clear();
    }

    _beginthreadex(nullptr,
                           0,
    [](LPVOID lpUser)->unsigned
    {
      SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_UpdateCheck");

      SKIF_UpdateCheckResults* _res = (SKIF_UpdateCheckResults*)lpUser;

      CoInitializeEx (nullptr,
        COINIT_APARTMENTTHREADED |
        COINIT_DISABLE_OLE1DDE
      );

      PLOG_DEBUG << "Update Thread Started!";

      std::wstring root         = SK_FormatStringW (LR"(%ws\Version\)",    path_cache.specialk_userdata);
      std::wstring path         = root + LR"(repository.json)";
      std::wstring path_patreon = SK_FormatStringW (LR"(%ws\patrons.txt)", path_cache.specialk_userdata);

      // Get UNIX-style time
      time_t ltime;
      time (&ltime);

      std::wstring url  = L"https://sk-data.special-k.info/repository.json";
                   url += L"?t=";
                   url += std::to_wstring (ltime); // Add UNIX-style timestamp to ensure we don't get anything cached
      std::wstring url_patreon = L"https://sk-data.special-k.info/patrons.txt";

      // Create any missing directories
      std::error_code ec;
      if (! std::filesystem::exists (            root, ec))
            std::filesystem::create_directories (root, ec);

      bool downloadNewFiles = false;

      if (_registry.iCheckForUpdates != 0 && ! _registry.bLowBandwidthMode)
      {
        // Download files if any does not exist or if we're forcing an update
        if (! PathFileExists (path.c_str()) || ! PathFileExists (path_patreon.c_str()) || _registry.iCheckForUpdates == 2)
        {
          downloadNewFiles = true;
        }

        else {
          WIN32_FILE_ATTRIBUTE_DATA fileAttributes{};

          if (GetFileAttributesEx (path.c_str(),    GetFileExInfoStandard, &fileAttributes))
          {
            FILETIME ftSystemTime{}, ftAdjustedFileTime{};
            SYSTEMTIME systemTime{};
            GetSystemTime (&systemTime);

            if (SystemTimeToFileTime(&systemTime, &ftSystemTime))
            {
              ULARGE_INTEGER uintLastWriteTime{};

              // Copy to ULARGE_INTEGER union to perform 64-bit arithmetic
              uintLastWriteTime.HighPart        = fileAttributes.ftLastWriteTime.dwHighDateTime;
              uintLastWriteTime.LowPart         = fileAttributes.ftLastWriteTime.dwLowDateTime;

              // Perform 64-bit arithmetic to add 7 days to last modified timestamp
              uintLastWriteTime.QuadPart        = uintLastWriteTime.QuadPart + ULONGLONG(7 * 24 * 60 * 60 * 1.0e+7);

              // Copy the results to an FILETIME struct
              ftAdjustedFileTime.dwHighDateTime = uintLastWriteTime.HighPart;
              ftAdjustedFileTime.dwLowDateTime  = uintLastWriteTime.LowPart;

              // Compare with system time, and if system time is later (1), then update the local cache
              if (CompareFileTime (&ftSystemTime, &ftAdjustedFileTime) == 1)
              {
                downloadNewFiles = true;
              }
            }
          }
        }
      }

      // Update patrons.txt
      if (downloadNewFiles)
      {
        PLOG_INFO << "Downloading patrons.txt...";
        SKIF_Util_GetWebResource (url_patreon, path_patreon);
      }

      // Read patrons.txt
      if (patrons.empty( ))
      {
        std::wifstream fPatrons(L"patrons.txt");
        std::vector<std::wstring> lines;
        std::wstring full_text;

        if (fPatrons.is_open ())
        {
          // Requires Windows 10 1903+ (Build 18362)
          if (SKIF_Util_IsWindowsVersionOrGreater (10, 0, 18362))
          {
            fPatrons.imbue (
                std::locale (".UTF-8")
            );
          }
          else
          {
            // Contemplate removing this fallback entirely since neither Win8.1 and Win10 pre-1903 is not supported any longer by Microsoft
            // Win8.1 fallback relies on deprecated stuff, so surpress warning when compiling
#pragma warning(disable : 4996)
            fPatrons.imbue (std::locale (std::locale::empty (), new (std::nothrow) std::codecvt_utf8 <wchar_t, 0x10ffff> ()));
          }

          std::wstring line;

          while (fPatrons.good ())
          {
            std::getline (fPatrons, line);

            // Skip blank lines, since they would match everything....
            for (const auto& it : line)
            {
              if (iswalpha(it) != 0)
              {
                lines.push_back(line);
                break;
              }
            }
          }

          if (! lines.empty())
          {
            // Shuffle the lines using a random number generator
            auto rd  = std::random_device{};
            auto gen = std::default_random_engine{ rd() };
            std::shuffle(lines.begin(), lines.end(), gen);  // Shuffle the vector

            for (const auto& vline : lines) {
              full_text += vline + L"\n";
            }

            if (full_text.length() > 0)
              full_text.resize (full_text.length () - 1);

            patrons = SK_WideCharToUTF8(full_text);
          }

          fPatrons.close ();
        }
      }

      // Indicate patrons variable is ready to be accessed from the main thread
      update_thread_patreon.store (1);

      // Update repository.json
      if (downloadNewFiles)
      {
        PLOG_INFO << "Downloading repository.json...";
        SKIF_Util_GetWebResource (url, path);
      }
    
      std::ifstream file(path);
      nlohmann::ordered_json jf = nlohmann::ordered_json::parse(file, nullptr, false);
      file.close();

      if (jf.is_discarded ( ))
      {
        PLOG_ERROR << "Parse error for repository.json. Deleting file so we retry on next launch...";
        DeleteFile (path.c_str()); // Something went wrong -- delete the file so a new attempt is performed on next launch
        return 0;
      }

      else {

        std::wstring wsCurrentBranch = _registry.wsUpdateChannel;
        std::string  currentBranch   = SK_WideCharToUTF8 (wsCurrentBranch);

        PLOG_INFO << "Update Channel: " << wsCurrentBranch;

#ifdef _WIN64
        std::wstring currentVersion = SK_UTF8ToWideChar (_inject.SKVer64);
#else
        std::wstring currentVersion = SK_UTF8ToWideChar (_inject.SKVer32);
#endif

        try {
          
          // Populate update channels
          try {
            static bool
                firstRun = true;
            if (firstRun)
            {   firstRun = false;

              bool detectedBranch = false;
              for (auto& branch : jf["Main"]["Branches"])
              {
                updateChannels.emplace_back(branch["Name"].get<std::string>(), branch["Description"].get<std::string>());

                if (branch["Name"].get<std::string_view>()._Equal(currentBranch))
                  detectedBranch = true;
              }

              // If we cannot find the branch, move the user over to the closest "parent" branch
              if (! detectedBranch)
              {
                PLOG_ERROR << "Could not find the update channel in repository.json!";

                if (     wsCurrentBranch.find(L"Website")       != std::string::npos
                      || wsCurrentBranch.find(L"Release")       != std::string::npos)
                         wsCurrentBranch = L"Website";
                else if (wsCurrentBranch.find(L"Discord")       != std::string::npos
                      || wsCurrentBranch.find(L"Testing")       != std::string::npos)
                         wsCurrentBranch = L"Discord";
                else if (wsCurrentBranch.find(L"Ancient")       != std::string::npos
                      || wsCurrentBranch.find(L"Compatibility") != std::string::npos)
                         wsCurrentBranch = L"Ancient";
                else
                         wsCurrentBranch = L"Website";

                PLOG_ERROR << "Using fallback channel: " << wsCurrentBranch;

                _registry.wsIgnoreUpdate = L"";

                currentBranch = SK_WideCharToUTF8 (wsCurrentBranch);
              }
            }
          }
          catch (const std::exception&)
          {

          }

          if (_registry.iCheckForUpdates != 0 && !_registry.bLowBandwidthMode)
          {
            // Used to populate history
            bool found = false;

            // Detect if any new version is available in the selected channel
            for (auto& version : jf["Main"]["Versions"])
            {
              bool isBranch = false;

              for (auto& branch : version["Branches"])
                if (branch.get<std::string_view>()._Equal(currentBranch))
                  isBranch = true;
        
              if (isBranch)
              {
                std::wstring branchVersion = SK_UTF8ToWideChar(version["Name"].get<std::string>());

                // Check if the version of this branch is different from the current one.
                // We don't check if the version is *newer* since we need to support downgrading
                // to other branches as well, which means versions that are older.

                // Limit download to a single version only
                if (! found)
                {
                  if ((SKIF_Util_CompareVersionStrings (branchVersion, currentVersion) != 0 && changedUpdateChannel) ||
                       SKIF_Util_CompareVersionStrings (branchVersion, currentVersion)  > 0)
                  {
                    PLOG_INFO << "Installed version: " << currentVersion;
                    PLOG_INFO << "Latest version: "    << branchVersion;

                    std::wstring branchInstaller    = SK_UTF8ToWideChar(version["Installer"]   .get<std::string>());
                    std::wstring filename           = branchInstaller.substr(branchInstaller.find_last_of(L"/"));

                    PLOG_INFO << "Downloading installer: " << branchInstaller;

                    _res->version      = branchVersion;
                    _res->filename     = filename;
                    _res->description  = SK_UTF8ToWideChar(version["Description"] .get<std::string>());
                    _res->releasenotes = SK_UTF8ToWideChar(version["ReleaseNotes"].get<std::string>());

                    if (! PathFileExists ((root + filename).c_str()) && _res->description != _registry.wsIgnoreUpdate)
                      SKIF_Util_GetWebResource (branchInstaller, root + filename);

                    found = true;
                  }
                }

                // Found right branch -- no need to check more since versions are sorted newest to oldest
                if (changedUpdateChannel)
                  break;

                // Only populate the history stuff on launch -- not when the user has changed update channel
                else {
                  _res->history += version["Description"].get<std::string>();
                  _res->history += "\n";
                  _res->history += "=================\n";
                  if (version["ReleaseNotes"].get<std::string>().empty())
                    _res->history += "No listed changes.";
                  else
                    _res->history += version["ReleaseNotes"].get<std::string>();
                  _res->history += "\n\n\n";
                }
              }
            }
          }
        }
        catch (const std::exception&)
        {

        }
      }
      
      PLOG_DEBUG << "Update Thread Stopped!";
      update_thread.store (2);
      _endthreadex(0);

      return 0;
    }, (LPVOID)&results, NULL, NULL);
  }

  // If the check is complete, return the results
  if (update_thread.load ( ) == 2)
  {
    // If a new refresh has been requested, reset the progress, but only if the current one is done
    if (update_thread_new.load ( ) == 1)
    {
      update_thread.store (0);
      update_thread_new.store (0);

      // Don't return the current results as it's outdated
      return SKIF_UpdateCheckResults ( );
    }

    return results;
  }

  return SKIF_UpdateCheckResults ( );
}

void SKIF_CreateUpdateNotifyMenu (void)
{
  if (hMenu != NULL)
    DestroyMenu (hMenu);

  bool svcRunning         = false,
       svcRunningAutoStop = false,
       svcStopped         = false;

  if (_inject.bCurrentState      && hInjectAck.m_h <= 0)
    svcRunning         = true;
  else if (_inject.bCurrentState && hInjectAck.m_h != 0)
    svcRunningAutoStop = true;
  else
    svcStopped         = true;

  hMenu = CreatePopupMenu ( );
  AppendMenu (hMenu, MF_STRING | ((svcRunning)         ? MF_CHECKED | MF_GRAYED : (svcRunningAutoStop) ? MF_GRAYED : 0x0), SKIF_NOTIFY_START,         L"Start Injection");
  AppendMenu (hMenu, MF_STRING | ((svcRunningAutoStop) ? MF_CHECKED | MF_GRAYED : (svcRunning)         ? MF_GRAYED : 0x0), SKIF_NOTIFY_STARTWITHSTOP, L"Start Injection (with auto stop)");
  AppendMenu (hMenu, MF_STRING | ((svcStopped)         ? MF_CHECKED | MF_GRAYED :                                    0x0), SKIF_NOTIFY_STOP,          L"Stop Injection");
  AppendMenu (hMenu, MF_SEPARATOR, 0, NULL);
  AppendMenu (hMenu, MF_STRING, SKIF_NOTIFY_EXIT,          L"Exit");
}

void SKIF_CreateNotifyIcon (void)
{
  ZeroMemory (&niData,  sizeof (NOTIFYICONDATA));
  niData.cbSize       = sizeof (NOTIFYICONDATA); // 6.0.6 or higher (Windows Vista and later)
  niData.uID          = SKIF_NOTIFY_ICON;
  niData.uFlags       = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
  niData.hIcon        = LoadIcon (hModSKIF, MAKEINTRESOURCE(IDI_SKIF));
  niData.hWnd         = SKIF_Notify_hWnd;
  niData.uVersion     = NOTIFYICON_VERSION_4;
  wcsncpy_s (niData.szTip,      128, L"Special K",   128);

  niData.uCallbackMessage = WM_SKIF_NOTIFY_ICON;

  Shell_NotifyIcon (NIM_ADD, &niData);
  //Shell_NotifyIcon (NIM_SETVERSION, &niData); // Breaks shit, lol
}

void SKIF_UpdateNotifyIcon (void)
{
  niData.uFlags        = NIF_ICON;
  if (_inject.bCurrentState)
    niData.hIcon       = LoadIcon (hModSKIF, MAKEINTRESOURCE (IDI_SKIFONNOTIFY));
  else
    niData.hIcon       = LoadIcon (hModSKIF, MAKEINTRESOURCE (IDI_SKIF));

  Shell_NotifyIcon (NIM_MODIFY, &niData);
}

void SKIF_CreateNotifyToast (std::wstring message, std::wstring title = L"")
{
  if ( _registry.iNotifications == 1 ||                           // Always
      (_registry.iNotifications == 2 && ! SKIF_ImGui_IsFocused()) // When Unfocused
    )
  {
    niData.uFlags       = NIF_INFO;
    niData.dwInfoFlags  = NIIF_NONE | NIIF_NOSOUND | NIIF_RESPECT_QUIET_TIME;
    wcsncpy_s(niData.szInfoTitle, 64, title.c_str(), 64);
    wcsncpy_s(niData.szInfo, 256, message.c_str(), 256);

    Shell_NotifyIcon (NIM_MODIFY, &niData);
  }
}

void SKIF_UI_DrawComponentVersion (void)
{
  ImGui::BeginGroup       ( );

  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), (const char *)u8"\u2022 ");
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),    "Special K 32-bit");

#ifdef _WIN64
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), (const char *)u8"\u2022 ");
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),    "Special K 64-bit");
#endif
    
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), (const char *)u8"\u2022 ");
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),    u8"前端 (SKIF)");

  ImGui::EndGroup         ( );
  ImGui::SameLine         ( );
  ImGui::BeginGroup       ( );
    
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase), "v");
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),    _inject.SKVer32.c_str());

#ifdef _WIN64
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase), "v");
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),    _inject.SKVer64.c_str());
#endif
    
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase), "v");
  ImGui::SameLine         ( );
  ImGui::ItemSize         (ImVec2 (0.0f, ImGui::GetTextLineHeight ()));
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),    SKIF_VERSION_STR_A " (" __DATE__ ")");

  ImGui::EndGroup         ( );

  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), (const char *)u8"\u2022 ");
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      u8"查看发行说明..."
  );
  SKIF_ImGui_SetMouseCursorHand ( );
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    HistoryPopup = PopupState::Open;
  ImGui::EndGroup         ( );

  if (SKIF_UpdateReady)
  {
    SKIF_ImGui_Spacing      ( );
    
    ImGui::ItemSize         (ImVec2 (65.0f, 0.0f));

    ImGui::SameLine         ( );

    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning));
    if (ImGui::Button (ICON_FA_WRENCH "  Update", ImVec2 (150.0f * SKIF_ImGui_GlobalDPIScale,
                                                               30.0f * SKIF_ImGui_GlobalDPIScale )))
      UpdatePromptPopup = PopupState::Open;
    ImGui::PopStyleColor ( );
  }
}

void SKIF_UI_DrawPlatformStatus (void)
{
  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );

  static bool isSKIFAdmin = IsUserAnAdmin();
  if (isSKIFAdmin)
  {
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), ICON_FA_EXCLAMATION_TRIANGLE " ");
    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), u8"应用程序正在以管理员身份运行!");
    SKIF_ImGui_SetHoverTip ( u8"不建议运行提升，因为它会将Special K注入系统进程.\n"
                             u8"请以常规用户身份重新启动此应用程序和全局注射器服务.");
  }
  else {
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), ICON_FA_CHECK " ");
    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), u8"应用程序正在以正常权限运行.");
    SKIF_ImGui_SetHoverTip ( u8"这是推荐的选项，因为不会注入Special K\n"
                             u8"进入系统进程或以管理员身份运行的游戏.");
  }

  ImGui::EndGroup         ( );

  struct Platform {
    std::string     Name;
    std::wstring    ProcessName;
    DWORD           ProcessID   = 0,
                    PreviousPID = 0;
    bool            isRunning   = false,
                    isAdmin     = false;

    Platform (std::string n, std::wstring pn)
    {
      Name        =  n;
      ProcessName = pn;
    }
  };

  static DWORD dwLastRefresh = 0;
  static Platform Platforms[] = {
    {"32-bit service",  L"SKIFsvc32.exe"},
#ifdef _WIN64
    {"64-bit service",  L"SKIFsvc64.exe"},
#endif
    {"Steam",               L"steam.exe"},
    {"Origin",              L"Origin.exe"},
    {"Galaxy",              L"GalaxyClient.exe"},
    {"EA Desktop",          L"EADesktop.exe"},
    {"Epic Games Launcher", L"EpicGamesLauncher.exe"},
    {"Ubisoft Connect",     L"upc.exe"},
    {"RTSS",                L"RTSS.exe"}
  };

  // Timer has expired, refresh
  if (dwLastRefresh < SKIF_Util_timeGetTime() && (! ImGui::IsAnyMouseDown ( ) || ! SKIF_ImGui_IsFocused ( ) ))
  {
    for (auto& p : Platforms)
    {
      p.ProcessID = 0;
      p.isRunning = false;
    }

    PROCESSENTRY32W pe32 = { };

    SK_AutoHandle hProcessSnap (
      CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0)
    );

    if ((intptr_t)hProcessSnap.m_h > 0)
    {
      pe32.dwSize = sizeof (PROCESSENTRY32W);

      if (Process32FirstW (hProcessSnap, &pe32))
      {
        do
        {
          for (auto& p : Platforms)
          {
            if (wcsstr (pe32.szExeFile, p.ProcessName.c_str()))
            {
              p.ProcessID = pe32.th32ProcessID;
              p.isRunning = true;

              // If it is a new process, check if it is running as an admin
              if (p.ProcessID != p.PreviousPID)
              {
                p.PreviousPID = p.ProcessID;
                p.isAdmin     = SKIF_Util_IsProcessAdmin (p.ProcessID);
              }

              // Skip checking the remaining platforms for this process
              continue;
            }
          }
        } while (Process32NextW (hProcessSnap, &pe32));
      }
    }

    dwLastRefresh = SKIF_Util_timeGetTime () + 1000; // Set timer for next refresh
  }

  for ( auto& p : Platforms )
  {
    if (p.isRunning)
    {
      ImGui::BeginGroup       ( );
      ImGui::Spacing          ( );
      ImGui::SameLine         ( );

      if (p.isAdmin)
      {
        ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), ICON_FA_EXCLAMATION_TRIANGLE " ");
        ImGui::SameLine        ( );
        if (p.ProcessName == L"RTSS.exe")
          ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), (p.Name + u8" 正在运行，可能与Special K冲突!").c_str() );
        else
          ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), (p.Name + u8" 正在以管理员身份运行!").c_str() );

        if (isSKIFAdmin)
          SKIF_ImGui_SetHoverTip ((u8"也不建议运行 " + p.Name + " 或以管理员身份使用此应用程序.\n"
            u8"请以普通用户身份重新启动两者.").c_str());
        else if (p.ProcessName == L"RTSS.exe")
          SKIF_ImGui_SetHoverTip (u8"已知RivaTuner统计服务器偶尔会与Special K发生冲突.\n"
            u8"如果Special K无法正常工作，请停止它。\n"
            u8"您可能也必须停止MSI Afterburner.");
        else if (p.ProcessName == L"SKIFsvc32.exe" || p.ProcessName == L"SKIFsvc64.exe")
          SKIF_ImGui_SetHoverTip (u8"不建议运行提升，因为它会将Special K注入系统进程.\n"
            u8"请以常规用户身份重新启动前端和全局注射器服务.");
        else
          SKIF_ImGui_SetHoverTip ((u8"Running elevated将防止注射到这些游戏中.\n"
            u8"请重新启动 " + p.Name + " 作为普通用户.").c_str());
      }
      else {
        ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), ICON_FA_CHECK " ");
        ImGui::SameLine        ( );
        ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), (p.Name + u8" 正在运行.").c_str());
      }

      ImGui::EndGroup          ( );
    }
    else if (p.ProcessName == L"SKIFsvc32.exe" || p.ProcessName == L"SKIFsvc64.exe")
    {
      ImGui::Spacing          ( );
      ImGui::SameLine         ( );
      ImGui::ItemSize         (ImVec2 (ImGui::CalcTextSize (ICON_FA_CHECK " ") .x, ImGui::GetTextLineHeight()));
      //ImGui::TextColored      (ImColor (0.68F, 0.68F, 0.68F), " " ICON_FA_MINUS " ");
      ImGui::SameLine         ( );
      ImGui::TextColored      (ImColor (0.68F, 0.68F, 0.68F), (p.Name + u8" 已停止.").c_str());
    }

#ifdef _WIN64
    if (p.ProcessName == L"SKIFsvc64.exe")
      ImGui::NewLine           ( );
#else
    if (p.ProcessName == L"SKIFsvc32.exe")
      ImGui::NewLine();
#endif
  }
}


void SKIF_SetStyle (void)
{  
  // Setup Dear ImGui style
  switch (_registry.iStyle)
  {
  case 3:
    ImGui::StyleColorsClassic  ( );
    break;
  case 2:
    ImGui::StyleColorsLight    ( );
    break;
  case 1:
    ImGui::StyleColorsDark     ( );
    break;
  case 0:
  default:
    SKIF_ImGui_StyleColorsDark ( );
    _registry.iStyle = 0;
  }
}


void SKIF_Initialize (void)
{
  static bool isInitalized = false;

  if (! isInitalized)
  {
    CoInitializeEx (nullptr, 0x0);

    updateChannels = {};

    hModSKIF =
      GetModuleHandleW (nullptr);

    // Cache user profile locations
    SKIF_GetFolderPath ( &path_cache.my_documents       );
    SKIF_GetFolderPath ( &path_cache.app_data_local     );
    SKIF_GetFolderPath ( &path_cache.app_data_local_low );
    SKIF_GetFolderPath ( &path_cache.app_data_roaming   );
    SKIF_GetFolderPath ( &path_cache.win_saved_games    );
    SKIF_GetFolderPath ( &path_cache.desktop            );

    // Launching SKIF through the Win10 start menu can at times default the working directory to system32.
    // Store the original working directory in a variable, since it's used by custom launch, for example.
    GetCurrentDirectoryW (MAX_PATH, path_cache.skif_workdir_org);

    // Lets store the full path to SKIF's executable
    GetModuleFileNameW  (nullptr, path_cache.specialk_install, MAX_PATH);
    wcsncpy_s ( path_cache.skif_executable,   MAX_PATH,
                path_cache.specialk_install, _TRUNCATE );
    
    // Let's change the current working directory to the folder of the executable itself.
    PathRemoveFileSpecW (         path_cache.specialk_install);
    SetCurrentDirectory (         path_cache.specialk_install);

    // Generate 8.3 filenames
    SK_Generate8Dot3    (path_cache.skif_workdir_org);
    SK_Generate8Dot3    (path_cache.specialk_install);

    bool fallback = true;
    // Cache the Special K user data path
    std::filesystem::path testDir  (path_cache.specialk_install);
    std::filesystem::path testFile (testDir);

    testDir  /= L"SKIFTMPDIR";
    testFile /= L"SKIFTMPFILE.tmp";

    // Try to delete any existing tmp folder or file (won't throw an exception at least)
    RemoveDirectory (testDir.c_str());
    DeleteFile      (testFile.wstring().c_str());

    std::error_code ec;
    // See if we can create a folder
    if (! std::filesystem::exists (            testDir, ec) &&
          std::filesystem::create_directories (testDir, ec))
    {
      // Delete it
      RemoveDirectory (testDir.c_str());

      // See if we can create a file
      HANDLE h = CreateFile ( testFile.wstring().c_str(),
              GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                  NULL,
                    CREATE_NEW,
                      FILE_ATTRIBUTE_NORMAL,
                        NULL );

      // If the file was created successfully
      if (h != INVALID_HANDLE_VALUE)
      {
        // We need to close the handle as well
        CloseHandle (h);

        // Delete it
        DeleteFile (testFile.wstring().c_str());

        // Use current path as we have write permissions
        wcsncpy_s ( path_cache.specialk_userdata, MAX_PATH,
                    path_cache.specialk_install, _TRUNCATE );

        // No need to rely on the fallback
        fallback = false;
      }
    }

    if (fallback)
    {
      // Fall back to appdata in case of issues
      std::wstring fallbackDir =
        std::wstring (path_cache.my_documents.path) + LR"(\My Mods\SpecialK\)";

      wcsncpy_s ( path_cache.specialk_userdata, MAX_PATH,
                  fallbackDir.c_str(), _TRUNCATE);
        
      // Create any missing directories
      if (! std::filesystem::exists (            fallbackDir, ec))
            std::filesystem::create_directories (fallbackDir, ec);
    }


    // Cache the Steam install folder
    wcsncpy_s ( path_cache.steam_install, MAX_PATH,
                  SK_GetSteamDir (),      _TRUNCATE );


    // Now we can proceed with initializing the logging
    std::wstring logPath =
      SK_FormatStringW (LR"(%ws\SKIF.log)", path_cache.specialk_userdata);

    // Delete old log file
    DeleteFile (logPath.c_str());

    // Engage logging!
    plog::init (plog::debug, logPath.c_str(), 10000000, 1);

#ifdef _WIN64
    PLOG_INFO << u8"Special K 注入前端 (SKIF) 64-bit v " << SKIF_VERSION_STR_A;
#else
    PLOG_INFO << u8"Special K 注入前端 (SKIF) 32-bit v " << SKIF_VERSION_STR_A;
#endif

    PLOG_INFO << "Built " __TIME__ ", " __DATE__;
    PLOG_INFO << SKIF_LOG_SEPARATOR;
    PLOG_INFO << "Working directory:  ";
    PLOG_INFO << "Old:                " << path_cache.skif_workdir_org;
    PLOG_INFO << "New:                " << std::filesystem::current_path ();
    PLOG_INFO << "SKIF executable:    " << path_cache.skif_executable;
    PLOG_INFO << "Special K install:  " << path_cache.specialk_install;
    PLOG_INFO << "Special K userdata: " << path_cache.specialk_userdata;
    PLOG_INFO << SKIF_LOG_SEPARATOR;

    isInitalized = true;
  }
}

typedef enum EFFECTIVE_POWER_MODE {
    EffectivePowerModeNone    = -1,   // Used as default value if querying failed
    EffectivePowerModeBatterySaver,
    EffectivePowerModeBetterBattery,
    EffectivePowerModeBalanced,
    EffectivePowerModeHighPerformance,
    EffectivePowerModeMaxPerformance, // EFFECTIVE_POWER_MODE_V1
    EffectivePowerModeGameMode,
    EffectivePowerModeMixedReality,   // EFFECTIVE_POWER_MODE_V2
} EFFECTIVE_POWER_MODE;

std::atomic<EFFECTIVE_POWER_MODE> enumEffectivePowerMode;

#define EFFECTIVE_POWER_MODE_V1 (0x00000001)
#define EFFECTIVE_POWER_MODE_V2 (0x00000002)

typedef VOID WINAPI EFFECTIVE_POWER_MODE_CALLBACK (
    _In_     EFFECTIVE_POWER_MODE  Mode,
    _In_opt_ VOID                 *Context
);

VOID WINAPI SKIF_EffectivePowerModeCallback (
    _In_     EFFECTIVE_POWER_MODE  Mode,
    _In_opt_ VOID                 *Context
)
{
  UNREFERENCED_PARAMETER(Context);

  enumEffectivePowerMode.store(Mode);

  PostMessage (SKIF_hWnd, WM_NULL, NULL, NULL);
};

std::string SKIF_GetEffectivePowerMode (void)
{
  std::string sMode;

  switch (enumEffectivePowerMode.load( ))
  {
  case EffectivePowerModeNone:
    sMode = u8"无";
    break;
  case EffectivePowerModeBatterySaver:
    sMode = u8"电池节电器";
    break;
  case EffectivePowerModeBetterBattery:
    sMode = u8"更好的电池";
    break;
  case EffectivePowerModeBalanced:
    sMode = u8"平衡的";
    break;
  case EffectivePowerModeHighPerformance:
    sMode = u8"高性能";
    break;
  case EffectivePowerModeMaxPerformance:
    sMode = u8"最大性能";
    break;
  case EffectivePowerModeGameMode:
    sMode = u8"游戏模式";
    break;
  case EffectivePowerModeMixedReality:
    sMode = u8"混合现实";
    break;
  default:
    sMode = u8"未知的模式";
    break;
  }

  return sMode;
}


bool bKeepWindowAlive  = true,
     bKeepProcessAlive = true;

SKIF_UpdateCheckResults newVersion;

// Uninstall registry keys
// Current User: HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Uninstall\{F4A43527-9457-424A-90A6-17CF02ACF677}_is1
// All Users:   HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\{F4A43527-9457-424A-90A6-17CF02ACF677}_is1

// Install folders
// Legacy:                              Documents\My Mods\SpecialK
// Modern (Current User; non-elevated): %LOCALAPPDATA%\Programs\Special K
// Modern (All Users;        elevated): %PROGRAMFILES%\Special K

// Main code
int
APIENTRY
wWinMain ( _In_     HINSTANCE hInstance,
           _In_opt_ HINSTANCE hPrevInstance,
           _In_     LPWSTR    lpCmdLine,
           _In_     int       nCmdShow )
{
  UNREFERENCED_PARAMETER (hPrevInstance);
  UNREFERENCED_PARAMETER (hInstance);

  SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOALIGNMENTFAULTEXCEPT);
  
  SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_MainThread");

  /*
  if (! SKIF_Util_IsWindows8Point1OrGreater ( ))
  {
    PLOG_INFO << "Unsupported version of Windows detected. Special K requires at least Windows 8.1; please update to a newer version.";
    MessageBox (NULL, L"Special K requires at least Windows 8.1\nPlease update to a newer version of Windows.", L"Unsupported Windows", MB_OK | MB_ICONERROR);
    return 0;
  }
  */

  // 2023-04-05: Shouldn't be needed as DPI-awareness is set through the embedded appmanifest file // Aemony
  //ImGui_ImplWin32_EnableDpiAwareness ();

  /* 2023-04-05: I'm pretty sure this block is unnecessary // Aemony
  GetSystemMetricsForDpi =
   (GetSystemMetricsForDpi_pfn)GetProcAddress (GetModuleHandle (L"user32.dll"),
   "GetSystemMetricsForDpi");
  */

  //CoInitializeEx (nullptr, 0x0);

  hWndOrigForeground =
    GetForegroundWindow ();

  plog::get()->setMaxSeverity((plog::Severity) _registry.iLogging);

  PLOG_INFO << "Max severity to log was set to " << _registry.iLogging;

  SKIF_ProxyCommandAndExitIfRunning (lpCmdLine);

  // We don't want Steam's overlay to draw upon SKIF,
  //   but only if not used as a launcher.
  //if (! _Signal.Launcher) // Shouldn't be needed, and might cause Steam to hook SKIF...
  SetEnvironmentVariable (L"SteamNoOverlayUIDrawing", L"1");

  // First round
  if (_Signal.Minimize)
    nCmdShow = SW_SHOWMINNOACTIVE;

  if (nCmdShow == SW_SHOWMINNOACTIVE && _registry.bCloseToTray)
    nCmdShow = SW_HIDE;

  // Second round
  if (nCmdShow == SW_SHOWMINNOACTIVE)
    startedMinimized = true;
  else if (nCmdShow == SW_HIDE)
    startedMinimized = SKIF_isTrayed = true;

  // Clear out old installers
  {
    PLOG_INFO << "Clearing out old installers...";

    auto _isWeekOld = [&](FILETIME ftLastWriteTime) -> bool
    {
      FILETIME ftSystemTime{}, ftAdjustedFileTime{};
      SYSTEMTIME systemTime{};
      GetSystemTime (&systemTime);

      if (SystemTimeToFileTime(&systemTime, &ftSystemTime))
      {
        ULARGE_INTEGER uintLastWriteTime{};

        // Copy to ULARGE_INTEGER union to perform 64-bit arithmetic
        uintLastWriteTime.HighPart        = ftLastWriteTime.dwHighDateTime;
        uintLastWriteTime.LowPart         = ftLastWriteTime.dwLowDateTime;

        // Perform 64-bit arithmetic to add 7 days to last modified timestamp
        uintLastWriteTime.QuadPart        = uintLastWriteTime.QuadPart + ULONGLONG(7 * 24 * 60 * 60 * 1.0e+7);

        // Copy the results to an FILETIME struct
        ftAdjustedFileTime.dwHighDateTime = uintLastWriteTime.HighPart;
        ftAdjustedFileTime.dwLowDateTime  = uintLastWriteTime.LowPart;

        // Compare with system time, and if system time is later (1), then update the local cache
        if (CompareFileTime(&ftSystemTime, &ftAdjustedFileTime) == 1)
        {
          return true;
        }
      }

      return false;
    };

    HANDLE hFind = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATA ffd;

    std::wstring VersionFolder = SK_FormatStringW(LR"(%ws\Version\)", path_cache.specialk_userdata);

    hFind = FindFirstFile((VersionFolder + L"SpecialK_*.exe").c_str(), &ffd);

    if (INVALID_HANDLE_VALUE != hFind)
    {
      if (_isWeekOld    (ffd.ftLastWriteTime))
        DeleteFile      ((VersionFolder + ffd.cFileName).c_str());

      while (FindNextFile (hFind, &ffd))
        if (_isWeekOld  (ffd.ftLastWriteTime))
          DeleteFile    ((VersionFolder + ffd.cFileName).c_str());

      FindClose (hFind);
    }
  }

  // Check if Controlled Folder Access is enabled
  if (_registry.bDisableCFAWarning == false && SKIF_hasControlledFolderAccess ( ))
  {
    if (IDYES == MessageBox(NULL, L"Controlled Folder Access is enabled in Windows and may prevent Special K and even some games from working properly. "
                                  L"It is recommended to either disable the feature or add exclusions for games where Special K is used as well as SKIF (this application)."
                                  L"\n\n"
                                  L"Do you want to disable this warning for all future launches?"
                                  L"\n\n"
                                  L"Microsoft's support page with more information will open when you select any of the options below.",
                                  L"Warning about Controlled Folder Access",
               MB_ICONWARNING | MB_YESNOCANCEL))
    {
      _registry.regKVDisableCFAWarning.putData (true);
    }

    SKIF_Util_OpenURI(L"https://support.microsoft.com/windows/allow-an-app-to-access-controlled-folders-b5b6627a-b008-2ca2-7931-7e51e912b034");
  }

  // Register SKIF in Windows to enable quick launching.
  PLOG_INFO << SKIF_LOG_SEPARATOR;
  PLOG_INFO << "Current Registry State:";
  PLOG_INFO << "Special K user data:   " << _registry.wsPath;
  PLOG_INFO << "SKIF app registration: " << _registry.wsAppRegistration;
  SKIF_RegisterApp ( );
  PLOG_INFO << SKIF_LOG_SEPARATOR;

  // Create application window
  WNDCLASSEX wc =
  { sizeof (WNDCLASSEX),
            CS_CLASSDC, SKIF_WndProc,
            0L,         0L,
    hModSKIF, nullptr,  nullptr,
              nullptr,  nullptr,
    SKIF_WindowClass,
              nullptr          };

  if (! ::RegisterClassEx (&wc))
  {
    return 0;
  }

  // Create invisible notify window (for the traybar icon and notification toasts)
  WNDCLASSEX wcNotify =
  { sizeof (WNDCLASSEX),
            CS_CLASSDC, SKIF_Notify_WndProc,
            0L,         0L,
        NULL, nullptr,  nullptr,
              nullptr,  nullptr,
    _T ("SK_Injection_Frontend_Notify"),
              nullptr          };

  if (! ::RegisterClassEx (&wcNotify))
  {
    return 0;
  }

  DWORD dwStyle   = ( WS_VISIBLE | WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU ),
        dwStyleEx = ( WS_EX_APPWINDOW | WS_EX_NOACTIVATE );

  if (nCmdShow != SW_SHOWMINNOACTIVE &&
      nCmdShow != SW_SHOWNOACTIVATE  &&
      nCmdShow != SW_SHOWNA          &&
      nCmdShow != SW_HIDE)
    dwStyleEx &= ~WS_EX_NOACTIVATE;

  if (SKIF_isTrayed)
    dwStyle &= ~WS_VISIBLE;


  /* 2023-04-05: I'm pretty sure this whole block is unnecessary // Aemony
  HMONITOR hMonitor =
    MonitorFromWindow (hWndOrigForeground, MONITOR_DEFAULTTONEAREST);

  MONITORINFOEX
    miex        = {           };
    miex.cbSize = sizeof (miex);

  UINT dpi = 0;
  if ( GetMonitorInfoW (hMonitor, &miex) )
  {
    float fdpiX =
      ImGui_ImplWin32_GetDpiScaleForMonitor (hMonitor);

    dpi =
      static_cast <UINT> (fdpiX * 96.0f);

    //int cxLogical = ( miex.rcMonitor.right  -
    //                  miex.rcMonitor.left  );
    //int cyLogical = ( miex.rcMonitor.bottom -
    //                  miex.rcMonitor.top   );
  }
  */

  SKIF_hWnd             =
    CreateWindowExW (                    dwStyleEx,
      wc.lpszClassName, _L("Special K"), dwStyle,
      /* 2023-04-05: I'm pretty sure this block is unnecessary // Aemony
      SK_FULLSCREEN_X (dpi) / 2 - __width  / 2,
      SK_FULLSCREEN_Y (dpi) / 2 - __height / 2,
                   __width, __height,
      */
                         0, 0,
                         0, 0,
                   nullptr, nullptr,
              wc.hInstance, nullptr
    );

  SKIF_Notify_hWnd      =
    CreateWindowExW (                                 WS_EX_NOACTIVATE,
      wcNotify.lpszClassName, _T("Special K Notify"), WS_ICONIC,
                         0, 0,
                         0, 0,
                   nullptr, nullptr,
        wcNotify.hInstance, nullptr
    );

  HWND  hWnd  = SKIF_hWnd;
  //HDC   hDC   =
  //  GetWindowDC (hWnd); // No purpose since it only concerns the 0x0 invisible native Win32 window and not the ImGui stuff
  HICON hIcon =
    LoadIcon (hModSKIF, MAKEINTRESOURCE (IDI_SKIF));

  InitializeConditionVariable (&SKIF_IsFocused);
  //InitializeConditionVariable (&SKIF_IsNotFocused);

  SendMessage      (hWnd, WM_SETICON, ICON_BIG,        (LPARAM)hIcon);
  SendMessage      (hWnd, WM_SETICON, ICON_SMALL,      (LPARAM)hIcon);
  SendMessage      (hWnd, WM_SETICON, ICON_SMALL2,     (LPARAM)hIcon);
  SetClassLongPtrW (hWnd, GCL_HICON,         (LONG_PTR)(LPARAM)hIcon);

  // Initialize Direct3D
  if (! CreateDeviceD3D (hWnd))
  {
    CleanupDeviceD3D ();
    return 1;
  }

  SetWindowLongPtr (hWnd, GWL_EXSTYLE, dwStyleEx & ~WS_EX_NOACTIVATE);

  // The window has been created but not displayed.
  // Now we have a parent window to which a notification tray icon can be associated.
  SKIF_CreateNotifyIcon       ();
  SKIF_CreateUpdateNotifyMenu ();

  // Show the window
  if (! SKIF_isTrayed)
  {
    ShowWindowAsync (hWnd, nCmdShow);
    UpdateWindow    (hWnd);
  }

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION   ();
  ImGui::CreateContext ();

  ImGuiIO& io =
    ImGui::GetIO ();

  (void)io; // WTF does this do?!

  io.IniFilename = "SKIF.ini";                                // nullptr to disable imgui.ini
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;        // Enable Gamepad Controls
//io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows
//io.ConfigFlags |= ImGuiConfigFlags_NavEnableSetMousePos;
  io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;
  io.ConfigViewportsNoAutoMerge      = false;
  io.ConfigViewportsNoTaskBarIcon    =  true;
  io.ConfigViewportsNoDefaultParent  = false;
  io.ConfigDockingAlwaysTabBar       = false;
  io.ConfigDockingTransparentPayload =  true;


  if (_registry.bDisableDPIScaling)
  {
    io.ConfigFlags &= ~ImGuiConfigFlags_DpiEnableScaleFonts;     // FIXME-DPI: THIS CURRENTLY DOESN'T WORK AS EXPECTED. DON'T USE IN USER APP!
  //io.ConfigFlags |=  ImGuiConfigFlags_DpiEnableScaleViewports; // FIXME-DPI
  }

  // Setup Dear ImGui style
  SKIF_SetStyle ( );

  ImGuiStyle& style =
  ImGui::GetStyle ();

  // When viewports are enabled we tweak WindowRounding/WindowBg
  //   so platform windows can look identical to regular ones.

  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
  {
    style.WindowRounding               = 5.0F;
    style.Colors [ImGuiCol_WindowBg].w = 1.0F;
  }

  style.WindowRounding  = 4.0F;// style.ScrollbarRounding;
  style.ChildRounding   = style.WindowRounding;
  style.TabRounding     = style.WindowRounding;
  style.FrameRounding   = style.WindowRounding;
  
  if (_registry.bDisableBorders)
  {
    style.TabBorderSize   = 0.0F;
    style.FrameBorderSize = 0.0F;
  }
  else {
    style.TabBorderSize   = 1.0F * SKIF_ImGui_GlobalDPIScale;
    style.FrameBorderSize = 1.0F * SKIF_ImGui_GlobalDPIScale;
  }

  // Setup Platform/Renderer bindings
  ImGui_ImplWin32_Init (hWnd); // This ends up creating a separate window/hWnd as well
  ImGui_ImplDX11_Init  (g_pd3dDevice, g_pd3dDeviceContext);

  DWORD dwDwmPeriod = 16; // Assume 60 Hz by default
  SKIF_GetMonitorRefreshRatePeriod (SKIF_hWnd, MONITOR_DEFAULTTOPRIMARY, dwDwmPeriod);
  //OutputDebugString((L"Initial refresh rate period: " + std::to_wstring (dwDwmPeriod) + L"\n").c_str());

#define SKIF_FONTSIZE_DEFAULT 18.0F // 18.0F

  SKIF_ImGui_InitFonts (SKIF_FONTSIZE_DEFAULT);

  // Our state
  ImVec4 clear_color         =
    ImVec4 (0.45F, 0.55F, 0.60F, 1.00F);

  // Message queue/pump
  MSG msg = { };

  // Variables related to the display SKIF is visible on
  ImGuiPlatformMonitor* monitor = nullptr;
  ImVec2 windowPos;
  ImRect windowRect       = ImRect(0.0f, 0.0f, 0.0f, 0.0f);
  ImRect monitor_extent   = ImRect(0.0f, 0.0f, 0.0f, 0.0f);
  bool changedMode        = false;
       RepositionSKIF     = (! PathFileExistsW(L"SKIF.ini") || _registry.bOpenAtCursorPosition);

  // Handle cases where a Start / Stop Command Line was Passed,
  //   but no running instance existed to service it yet...
  _Signal._Disowned = TRUE;

  // Don't execute again when used as a launcher (prevents false positives from game paths/executables)
  if      ((_Signal.Start || _Signal.Stop) && ! _Signal.Launcher)
    SKIF_ProxyCommandAndExitIfRunning (lpCmdLine);

  // Variable related to continue/pause rendering behaviour
  bool HiddenFramesContinueRendering = true;  // We always have hidden frames that require to continue rendering on init
  bool svcTransitionFromPendingState = false; // This is used to continue rendering if we transitioned over from a pending state (which kills the refresh timer)
  int  updaterExpectingNewFile      = 10;     // This is used to continue rendering for a few frames if we're expecting a new version file

  // Force a one-time check before we enter the main loop
  _inject.TestServletRunlevel (true);

  // Fetch SK DLL versions
  _inject._RefreshSKDLLVersions ();

  // Register SKIF for effective power notifications on Windows 10 1809+
  using PowerRegisterForEffectivePowerModeNotifications_pfn =
    HRESULT (WINAPI *)(ULONG Version, EFFECTIVE_POWER_MODE_CALLBACK *Callback, VOID *Context, VOID **RegistrationHandle);

  static PowerRegisterForEffectivePowerModeNotifications_pfn
    SKIF_PowerRegisterForEffectivePowerModeNotifications =
        (PowerRegisterForEffectivePowerModeNotifications_pfn)GetProcAddress (LoadLibraryEx (L"powrprof.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        "PowerRegisterForEffectivePowerModeNotifications");

  using PowerUnregisterFromEffectivePowerModeNotifications_pfn =
    HRESULT (WINAPI *)(VOID *RegistrationHandle);

  static PowerUnregisterFromEffectivePowerModeNotifications_pfn
    SKIF_PowerUnregisterFromEffectivePowerModeNotifications =
        (PowerUnregisterFromEffectivePowerModeNotifications_pfn)GetProcAddress (LoadLibraryEx (L"powrprof.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        "PowerUnregisterFromEffectivePowerModeNotifications");
  
	HANDLE hEffectivePowerModeRegistration = NULL;

  enumEffectivePowerMode.store(EffectivePowerModeNone);
  if (SKIF_PowerRegisterForEffectivePowerModeNotifications      != nullptr)
  {
    if (SKIF_PowerUnregisterFromEffectivePowerModeNotifications != nullptr)
    {
      PLOG_DEBUG << "Registering SKIF for effective power mode notifications";
      SKIF_PowerRegisterForEffectivePowerModeNotifications (EFFECTIVE_POWER_MODE_V2, SKIF_EffectivePowerModeCallback, NULL, &hEffectivePowerModeRegistration);
    }
  }

  // Register a hotkey for toggling HDR on a per-display basis (WinKey + Ctrl + Shift + H)
  if (RegisterHotKey (SKIF_hWnd, SKIF_HotKey_HDR, MOD_WIN | MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 0x48))
    PLOG_INFO << "Successfully registered global hotkey (WinKey + Ctrl + Shift + H) for toggling HDR on the display the cursor is currently on.";
  /*
  * Re. MOD_WIN: Either WINDOWS key was held down. These keys are labeled with the Windows logo.
  *              Keyboard shortcuts that involve the WINDOWS key are reserved for use by the operating system.
  */

  // Main loop
  while (! SKIF_Shutdown && IsWindow (hWnd) )
  {                         msg          = { };
    static UINT uiLastMsg = 0x0;
    coverFadeActive = false; // Assume there's no cover fade effect active

    auto _TranslateAndDispatch = [&](void) -> bool
    {
      while (! SKIF_Shutdown && PeekMessage (&msg, 0, 0U, 0U, PM_REMOVE))
      {
        if (! IsWindow (hWnd))
          return false;

        /*
        if (     msg.hwnd ==        SKIF_hWnd)
          OutputDebugString (L"Message bound for SKIF_WndProc ( )!\n");
        else if (msg.hwnd == SKIF_Notify_hWnd)
          OutputDebugString (L"Message bound for SKIF_Notify_WndProc ( )!\n");
        else if (msg.hwnd ==  SKIF_ImGui_hWnd)
          OutputDebugString (L"Message bound for ImGui_ImplWin32_WndProcHandler_PlatformWindow ( )!\n");
        else
          OutputDebugString (L"Message bound for ImGui_ImplWin32_WndProcHandler_PlatformWindow ( )!\n");
        */

        // There are three different window procedures that a message can be dispatched to based on the HWND of the message
        //                                  SKIF_WndProc ( )  <=         SKIF_hWnd                         :: Handles messages meant for the "main" (aka hidden) SKIF 0x0 window that resides in the top left corner of the display
        //                           SKIF_Notify_WndProc ( )  <=  SKIF_Notify_hWnd                         :: Handles messages meant for the notification icon.
        // ImGui_ImplWin32_WndProcHandler_PlatformWindow ( )  <=   SKIF_ImGui_hWnd, Other HWNDs            :: Handles messages meant for the overarching ImGui Platform window of SKIF, as well as any
        //                                                                                                      additional swapchain windows (menus/tooltips that stretches beyond SKIF_ImGui_hWnd).
        // ImGui_ImplWin32_WndProcHandler                ( )  <=  SKIF_hWnd, SKIF_ImGui_hWnd, Other HWNDs  :: Gets called by the two main window procedures:
        //                                                                                                      - SKIF_WndProc ( )
        //                                                                                                      - ImGui_ImplWin32_WndProcHandler_PlatformWindow ( ).
        // 
        TranslateMessage (&msg);
        DispatchMessage  (&msg);

        if (msg.message == WM_MOUSEMOVE)
        {
          static LPARAM lParamPrev;
          static WPARAM wParamPrev;

          // Workaround for a bug in System Informer where it sends a fake WM_MOUSEMOVE message to the window the cursor is over
          // Ignore the message if WM_MOUSEMOVE has identical data as the previous msg
          if (msg.lParam == lParamPrev &&
              msg.wParam == wParamPrev)
            msgDontRedraw = true;
          else {
            lParamPrev = msg.lParam;
            wParamPrev = msg.wParam;
          }
        }

        uiLastMsg = msg.message;
      }

      return ! SKIF_Shutdown; // return false on exit or system shutdown
    };

    // Injection acknowledgment; shutdown injection
    //
    //  * This is backed by a periodic WM_TIMER message if injection
    //      was programmatically started and ACK has not signaled
    //
    if (                     hInjectAck.m_h != 0 &&
        WaitForSingleObject (hInjectAck.m_h,   0) == WAIT_OBJECT_0)
    {
      hInjectAck.Close ();
      _inject.bAckInjSignaled = true;
      _inject._StartStopInject (true);
    }

    // If SKIF is acting as a temporary launcher, exit when the running service has been stopped
    if (bExitOnInjection && _inject.runState == SKIF_InjectionContext::RunningState::Stopped)
    {
      static DWORD dwExitDelay = SKIF_Util_timeGetTime();
      static int iDuration = -1;

      if (iDuration == -1)
      {
        HKEY    hKey;
        DWORD32 dwData  = 0;
        DWORD   dwSize  = sizeof (DWORD32);

        if (RegOpenKeyW (HKEY_CURRENT_USER, LR"(Control Panel\Accessibility\)", &hKey) == ERROR_SUCCESS)
        {
          iDuration = (RegGetValueW(hKey, NULL, L"MessageDuration", RRF_RT_REG_DWORD, NULL, &dwData, &dwSize) == ERROR_SUCCESS) ? dwData : 5;
          RegCloseKey(hKey);
        }
        else {
          iDuration = 5;
        }
      }
      // MessageDuration * 2 seconds delay to allow Windows to send both notifications properly
      // If notifications are disabled, exit immediately
      if (dwExitDelay + iDuration * 2 * 1000 < SKIF_Util_timeGetTime() || _registry.iNotifications == 0)
      {
        bExitOnInjection = false;
        PostMessage (hWnd, WM_QUIT, 0, 0);
      }
    }

    // Set DPI related variables
    SKIF_ImGui_GlobalDPIScale_Last = SKIF_ImGui_GlobalDPIScale;
    float fontScale = 18.0F * SKIF_ImGui_GlobalDPIScale;
    if (fontScale < 15.0F)
      fontScale += 1.0F;

    // Handling sub-1000px resolutions by rebuilding the font at 11px
    if (SKIF_ImGui_GlobalDPIScale < 1.0f && (! tinyDPIFonts))
    {
      tinyDPIFonts = true;

      PLOG_VERBOSE << "DPI scale detected as being below 100%; using font scale " << fontScale << "F";
      SKIF_ImGui_InitFonts (fontScale); // 11.0F
      ImGui::GetIO ().Fonts->Build ();
      ImGui_ImplDX11_InvalidateDeviceObjects ();

      invalidatedFonts = SKIF_Util_timeGetTime();
    }

    else if (SKIF_ImGui_GlobalDPIScale >= 1.0f && tinyDPIFonts)
    {
      tinyDPIFonts = false;

      PLOG_VERBOSE << "DPI scale detected as being at or above 100%; using font scale 18.0F";

      SKIF_ImGui_InitFonts (SKIF_FONTSIZE_DEFAULT);
      ImGui::GetIO ().Fonts->Build ();
      ImGui_ImplDX11_InvalidateDeviceObjects ();

      invalidatedFonts = SKIF_Util_timeGetTime();
    }

    else if (invalidateFonts)
    {
      PLOG_VERBOSE_IF(tinyDPIFonts) << "DPI scale detected as being below 100%; using font scale " << fontScale << "F";
      SKIF_ImGui_InitFonts ((tinyDPIFonts) ? fontScale : SKIF_FONTSIZE_DEFAULT);
      ImGui::GetIO ().Fonts->Build ();
      ImGui_ImplDX11_InvalidateDeviceObjects ();

      invalidateFonts = false;
      invalidatedFonts = SKIF_Util_timeGetTime();
    }
    
    // This occurs on the next frame, as failedLoadFonts gets evaluated and set as part of ImGui_ImplDX11_NewFrame
    else if (failedLoadFonts)
    {
      SKIF_bFontChineseSimplified = false;
      SKIF_bFontChineseAll        = false;
      SKIF_bFontCyrillic          = false;
      SKIF_bFontJapanese          = false;
      SKIF_bFontKorean            = false;
      SKIF_bFontThai              = false;
      SKIF_bFontVietnamese        = false;

      PLOG_VERBOSE_IF(tinyDPIFonts) << "DPI scale detected as being below 100%; using font scale " << fontScale << "F";
      SKIF_ImGui_InitFonts ((tinyDPIFonts) ? fontScale : SKIF_FONTSIZE_DEFAULT);
      ImGui::GetIO ().Fonts->Build ();
      ImGui_ImplDX11_InvalidateDeviceObjects ();

      failedLoadFonts = false;
      failedLoadFontsPrompt = true;
    }

#pragma region New UI Frame

    // Start the Dear ImGui frame
    ImGui_ImplDX11_NewFrame  (); // (Re)create individual swapchain windows
    ImGui_ImplWin32_NewFrame (); // Handle input
    ImGui::NewFrame          ();
    {
      // Fixes the wobble that occurs when switching between tabs,
      //  as the width/height of the window isn't dynamically calculated.
#define SKIF_wLargeMode 1038
#define SKIF_hLargeMode  944 // Does not include the status bar
#define SKIF_wSmallMode  415
#define SKIF_hSmallMode  305

      static ImVec2 SKIF_vecSmallMode,
                    SKIF_vecLargeMode,
                    SKIF_vecCurrentMode;
      ImRect rectCursorMonitor;

      // RepositionSKIF -- Step 1: Retrieve monitor of cursor, and set global DPI scale
      if (RepositionSKIF)
      {
        ImRect t;
        for (int monitor_n = 0; monitor_n < ImGui::GetPlatformIO().Monitors.Size; monitor_n++)
        {
          const ImGuiPlatformMonitor& tmpMonitor = ImGui::GetPlatformIO().Monitors[monitor_n];
          t = ImRect(tmpMonitor.MainPos, (tmpMonitor.MainPos + tmpMonitor.MainSize));
          if (t.Contains(ImGui::GetMousePos()))
          {
            SKIF_ImGui_GlobalDPIScale = tmpMonitor.DpiScale;

            rectCursorMonitor = t;
          }
        }
      }

      SKIF_vecSmallMode   = ImVec2 ( SKIF_wSmallMode * SKIF_ImGui_GlobalDPIScale,
                                     SKIF_hSmallMode * SKIF_ImGui_GlobalDPIScale );
      SKIF_vecLargeMode   = ImVec2 ( SKIF_wLargeMode * SKIF_ImGui_GlobalDPIScale,
                                     SKIF_hLargeMode * SKIF_ImGui_GlobalDPIScale );

      // Add the status bar if it is not disabled
      if ( ! _registry.bDisableStatusBar )
      {
        SKIF_vecLargeMode.y += 31.0f * SKIF_ImGui_GlobalDPIScale;
        SKIF_vecLargeMode.y += (_registry.bDisableTooltips) ? 18.0f * SKIF_ImGui_GlobalDPIScale : 0.0f;
      }

      SKIF_vecCurrentMode  =
                    (_registry.bSmallMode) ? SKIF_vecSmallMode
                                      : SKIF_vecLargeMode;

      if (ImGui::GetFrameCount() > 2)
        ImGui::SetNextWindowSize (SKIF_vecCurrentMode);

      // RepositionSKIF -- Step 2: Repositon the window
      if (RepositionSKIF)
      {
        // Repositions the window in the center of the monitor the cursor is currently on
        ImGui::SetNextWindowPos (ImVec2(rectCursorMonitor.GetCenter().x - (SKIF_vecCurrentMode.x / 2.0f), rectCursorMonitor.GetCenter().y - (SKIF_vecCurrentMode.y / 2.0f)));
      }

      // Calculate new window boundaries and changes to fit within the workspace if it doesn't fit
      //   Delay running the code to on the third frame to allow other required parts to have already executed...
      //     Otherwise window gets positioned wrong on smaller monitors !
      if (changedMode && ImGui::GetFrameCount() > 2)
      {
        changedMode = false;

        ImVec2 topLeft      = windowPos,
               bottomRight  = windowPos + SKIF_vecCurrentMode,
               newWindowPos = windowPos;

        if (      topLeft.x < monitor_extent.Min.x )
             newWindowPos.x = monitor_extent.Min.x;
        if (      topLeft.y < monitor_extent.Min.y )
             newWindowPos.y = monitor_extent.Min.y;

        if (  bottomRight.x > monitor_extent.Max.x )
             newWindowPos.x = monitor_extent.Max.x - SKIF_vecCurrentMode.x;
        if (  bottomRight.y > monitor_extent.Max.y )
             newWindowPos.y = monitor_extent.Max.y - SKIF_vecCurrentMode.y;

        if ( newWindowPos.x != windowPos.x ||
             newWindowPos.y != windowPos.y )
          ImGui::SetNextWindowPos(newWindowPos);
      }

#pragma region Move SKIF using Windows Key + Arrow Keys

      if ((io.KeysDown[VK_LWIN] && io.KeysDownDuration[VK_LWIN] == 0.0f) ||
          (io.KeysDown[VK_RWIN] && io.KeysDownDuration[VK_RWIN] == 0.0f))
        KeyWinKey = true;
      else if (! io.KeysDown[VK_LWIN] && ! io.KeysDown[VK_RWIN])
        KeyWinKey = false;

      if (KeyWinKey && SnapKeys)
      {
        if (monitor != nullptr)
        {
          HMONITOR currentMonitor = MonitorFromWindow (SKIF_hWnd, MONITOR_DEFAULTTONULL);

          if (currentMonitor != NULL)
          {
            MONITORINFO currentMonitorInfo{}, nearestMonitorInfo{};
            currentMonitorInfo.cbSize = sizeof(currentMonitorInfo);
            nearestMonitorInfo.cbSize = sizeof(nearestMonitorInfo);

            if (GetMonitorInfo(currentMonitor, (LPMONITORINFO)&currentMonitorInfo))
            {
              // Some simple math to basically create a dummy RECT that
              //  lies alongside one of the sides of the current monitor
              RECT dummy(currentMonitorInfo.rcMonitor);
              if      (SnapKeys &  2) // Left
              {
                dummy.right  = dummy.left;
                dummy.left   = dummy.left   - static_cast<long>(SKIF_vecCurrentMode.x);
              }
              else if (SnapKeys &  4) // Up
              {
                dummy.bottom = dummy.top;
                dummy.top    = dummy.top    - static_cast<long>(SKIF_vecCurrentMode.y);
              }
              else if (SnapKeys &  8) // Right
              {
                dummy.left   = dummy.right;
                dummy.right  = dummy.right  + static_cast<long>(SKIF_vecCurrentMode.x);
              }
              else if (SnapKeys & 16) // Down
              {
                dummy.top = dummy.bottom;
                dummy.bottom = dummy.bottom + static_cast<long>(SKIF_vecCurrentMode.y);
              }

              // Let us retrieve the closest monitor based on our dummy rect
              HMONITOR nearestMonitor = MonitorFromRect(&dummy, MONITOR_DEFAULTTONEAREST);
              if (GetMonitorInfo(nearestMonitor, (LPMONITORINFO)&nearestMonitorInfo))
              {
                // Don't bother if the nearest monitor is also the current monitor
                if (nearestMonitorInfo.rcMonitor != currentMonitorInfo.rcMonitor)
                {
                  // Loop through all platform monitors
                  for (int monitor_n = 0; monitor_n < ImGui::GetPlatformIO().Monitors.Size; monitor_n++)
                  {
                    const ImGuiPlatformMonitor& tmpMonitor = ImGui::GetPlatformIO().Monitors[monitor_n];

                    // Skip if we are on the wrong monitor
                    if (tmpMonitor.MainPos.x != static_cast<float>(nearestMonitorInfo.rcMonitor.left) ||
                        tmpMonitor.MainPos.y != static_cast<float>(nearestMonitorInfo.rcMonitor.top))
                      continue;

                    // Get the screen area of the monitor
                    ImRect tmpMonRect = ImRect(tmpMonitor.MainPos, (tmpMonitor.MainPos + tmpMonitor.MainSize));

                    // Calculate the expected new size using the DPI scale of the monitor
                    ImVec2 tmpWindowSize = 
                                  (_registry.bSmallMode) ? ImVec2 ( SKIF_wSmallMode * tmpMonitor.DpiScale,
                                                               SKIF_hSmallMode * tmpMonitor.DpiScale)
                                                    : ImVec2 ( SKIF_wLargeMode * tmpMonitor.DpiScale,
                                                               SKIF_hLargeMode * tmpMonitor.DpiScale);

                    // Calculate the expected position on the new monitor
                    ImVec2 suggestedPos = ImVec2(tmpMonRect.GetCenter().x - (tmpWindowSize.x / 2.0f), tmpMonRect.GetCenter().y - (tmpWindowSize.y / 2.0f));

                    // Check if the position is within the boundaries of the
                    //  monitor and move the suggested position if it is not.
                    if (suggestedPos.x < tmpMonitor.MainPos.x)
                        suggestedPos.x = tmpMonitor.MainPos.x;
                    if (suggestedPos.y < tmpMonitor.MainPos.y)
                        suggestedPos.y = tmpMonitor.MainPos.y;

                    // Set the new window position and break out of the loop
                    ImGui::SetNextWindowPos(suggestedPos);
                    break;
                  }
                }
              }
            }
          }
        }
      }

      SnapKeys = 0;

#pragma endregion

      ImGui::Begin ( SKIF_WINDOW_TITLE_A SKIF_WINDOW_HASH,
                       nullptr,
                         ImGuiWindowFlags_NoResize          |
                         ImGuiWindowFlags_NoCollapse        |
                         ImGuiWindowFlags_NoTitleBar        |
                         ImGuiWindowFlags_NoScrollbar       | // Hide the scrollbar for the main window
                         ImGuiWindowFlags_NoScrollWithMouse   // Prevent scrolling with the mouse as well
      );

      SK_RunOnce (ImGui::GetCurrentWindow()->HiddenFramesCannotSkipItems += 2);

      HiddenFramesContinueRendering = (ImGui::GetCurrentWindowRead()->HiddenFramesCannotSkipItems > 0);
      HoverTipActive = false;

      // Update current monitors/worksize etc;
      monitor     = &ImGui::GetPlatformIO        ().Monitors [ImGui::GetCurrentWindowRead()->ViewportAllowPlatformMonitorExtend];

      // Move the invisible Win32 parent window SKIF_hWnd over to the current monitor.
      //   This solves multiple taskbars not showing SKIF's window on all monitors properly.
      if (monitor->MainPos.x != ImGui::GetMainViewport()->Pos.x ||
          monitor->MainPos.y != ImGui::GetMainViewport()->Pos.y )
      {
        MoveWindow (SKIF_hWnd, (int)monitor->MainPos.x, (int)monitor->MainPos.y, 0, 0, false);

        // Update refresh rate for the current monitor
        SKIF_GetMonitorRefreshRatePeriod (SKIF_hWnd, MONITOR_DEFAULTTONEAREST, dwDwmPeriod);
        //OutputDebugString ((L"Updated refresh rate period: " + std::to_wstring (dwDwmPeriod) + L"\n").c_str());

        RecreateSwapChains = true;
      }

      float fDpiScaleFactor =
        ((io.ConfigFlags & ImGuiConfigFlags_DpiEnableScaleFonts) ? monitor->DpiScale : 1.0f);

      // RepositionSKIF -- Step 3: The Final Step -- Prevent the global DPI scale from potentially being set to outdated values
      if (RepositionSKIF)
      {
        RepositionSKIF = false;
      } else if ( monitor->WorkSize.y / fDpiScaleFactor < ((float)SKIF_hLargeMode + 40.0f) && ImGui::GetFrameCount () > 1)
      {
        SKIF_ImGui_GlobalDPIScale = (monitor->WorkSize.y / fDpiScaleFactor) / ((float)SKIF_hLargeMode / fDpiScaleFactor + 40.0f / fDpiScaleFactor);
      } else {
        SKIF_ImGui_GlobalDPIScale = (io.ConfigFlags & ImGuiConfigFlags_DpiEnableScaleFonts) ? ImGui::GetCurrentWindowRead()->Viewport->DpiScale : 1.0f;
      }

      // Rescale the style on DPI changes
      if (SKIF_ImGui_GlobalDPIScale != SKIF_ImGui_GlobalDPIScale_Last)
      {
        style.WindowPadding                         = SKIF_ImGui_DefaultStyle.WindowPadding                       * SKIF_ImGui_GlobalDPIScale;
        style.WindowRounding                        = 4.0F                                                        * SKIF_ImGui_GlobalDPIScale;
        style.WindowMinSize                         = SKIF_ImGui_DefaultStyle.WindowMinSize                       * SKIF_ImGui_GlobalDPIScale;
        style.ChildRounding                         = style.WindowRounding;
        style.PopupRounding                         = SKIF_ImGui_DefaultStyle.PopupRounding                       * SKIF_ImGui_GlobalDPIScale;
        style.FramePadding                          = SKIF_ImGui_DefaultStyle.FramePadding                        * SKIF_ImGui_GlobalDPIScale;
        style.FrameRounding                         = style.WindowRounding;
        style.ItemSpacing                           = SKIF_ImGui_DefaultStyle.ItemSpacing                         * SKIF_ImGui_GlobalDPIScale;
        style.ItemInnerSpacing                      = SKIF_ImGui_DefaultStyle.ItemInnerSpacing                    * SKIF_ImGui_GlobalDPIScale;
        style.TouchExtraPadding                     = SKIF_ImGui_DefaultStyle.TouchExtraPadding                   * SKIF_ImGui_GlobalDPIScale;
        style.IndentSpacing                         = SKIF_ImGui_DefaultStyle.IndentSpacing                       * SKIF_ImGui_GlobalDPIScale;
        style.ColumnsMinSpacing                     = SKIF_ImGui_DefaultStyle.ColumnsMinSpacing                   * SKIF_ImGui_GlobalDPIScale;
        style.ScrollbarSize                         = SKIF_ImGui_DefaultStyle.ScrollbarSize                       * SKIF_ImGui_GlobalDPIScale;
        style.ScrollbarRounding                     = SKIF_ImGui_DefaultStyle.ScrollbarRounding                   * SKIF_ImGui_GlobalDPIScale;
        style.GrabMinSize                           = SKIF_ImGui_DefaultStyle.GrabMinSize                         * SKIF_ImGui_GlobalDPIScale;
        style.GrabRounding                          = SKIF_ImGui_DefaultStyle.GrabRounding                        * SKIF_ImGui_GlobalDPIScale;
        style.TabRounding                           = style.WindowRounding;
        if (style.TabMinWidthForUnselectedCloseButton != FLT_MAX)
          style.TabMinWidthForUnselectedCloseButton = SKIF_ImGui_DefaultStyle.TabMinWidthForUnselectedCloseButton * SKIF_ImGui_GlobalDPIScale;
        style.DisplayWindowPadding                  = SKIF_ImGui_DefaultStyle.DisplayWindowPadding                * SKIF_ImGui_GlobalDPIScale;
        style.DisplaySafeAreaPadding                = SKIF_ImGui_DefaultStyle.DisplaySafeAreaPadding              * SKIF_ImGui_GlobalDPIScale;
        style.MouseCursorScale                      = SKIF_ImGui_DefaultStyle.MouseCursorScale                    * SKIF_ImGui_GlobalDPIScale;

        // These are not a part of the default style so need to assign them separately
        if (! _registry.bDisableBorders)
        {
          style.TabBorderSize                       = 1.0F                                                        * SKIF_ImGui_GlobalDPIScale;
          style.FrameBorderSize                     = 1.0F                                                        * SKIF_ImGui_GlobalDPIScale;
        }
      }

#if 0
      FLOAT SKIF_GetHDRWhiteLuma (void);
      void  SKIF_SetHDRWhiteLuma (FLOAT);

      static auto regKVLuma =
        SKIF_MakeRegKeyF (
          LR"(SOFTWARE\Kaldaien\Special K\)",
            LR"(ImGui HDR Luminance)"
        );

      auto _InitFromRegistry =
        [&](void) ->
        float
      {
        float fLumaInReg =
          regKVLuma.getData ();

        if (fLumaInReg == 0.0f)
        {
          fLumaInReg = SKIF_GetHDRWhiteLuma ();
          regKVLuma.putData (fLumaInReg);
        }

        else
        {
          SKIF_SetHDRWhiteLuma (fLumaInReg);
        }

        return fLumaInReg;
      };

      static float fLuma =
        _InitFromRegistry ();

      auto _DrawHDRConfig = [&](void)
      {
        static bool bFullRange = false;

        FLOAT fMaxLuma =
          SKIF_GetMaxHDRLuminance (bFullRange);

        if (fMaxLuma != 0.0f)
        {
          ImGui::TreePush("");
          ImGui::SetNextItemWidth(300.0f * SKIF_ImGui_GlobalDPIScale);
          if (ImGui::SliderFloat ("###HDR Paper White", &fLuma, 80.0f, fMaxLuma, (const char *)u8"HDR White:\t%04.1f cd/m\u00B2"))
          {
            SKIF_SetHDRWhiteLuma (fLuma);
            regKVLuma.putData    (fLuma);
          }
          ImGui::TreePop ( );
          ImGui::Spacing();
        }
      };
#endif

      static ImGuiTabBarFlags flagsInjection =
                ImGuiTabItemFlags_None,
                              flagsHelp =
                ImGuiTabItemFlags_None;


      // Top right window buttons
      ImVec2 topCursorPos =
        ImGui::GetCursorPos ();

      ImGui::SetCursorPos (
        ImVec2 ( SKIF_vecCurrentMode.x - 120.0f * SKIF_ImGui_GlobalDPIScale,
                                           4.0f * SKIF_ImGui_GlobalDPIScale )
      );

      ImGui::PushStyleVar (
        ImGuiStyleVar_FrameRounding, 25.0f * SKIF_ImGui_GlobalDPIScale
      );

      if ( (io.KeyCtrl && io.KeysDown['R']    && io.KeysDownDuration['R']    == 0.0f) ||
           (              io.KeysDown[VK_F5]  && io.KeysDownDuration[VK_F5]  == 0.0f)
         )
      {
        if (SKIF_Tab_Selected == UITab_Library)
          RepopulateGames   = true;

        if (SKIF_Tab_Selected == UITab_Settings)
          RefreshSettingsTab = true;
      }

      if ( (io.KeyCtrl && io.KeysDown['T']    && io.KeysDownDuration['T']    == 0.0f) ||
           (              io.KeysDown[VK_F11] && io.KeysDownDuration[VK_F11] == 0.0f) ||
            ImGui::Button ( (_registry.bSmallMode) ? ICON_FA_EXPAND_ARROWS_ALT
                                              : ICON_FA_COMPRESS_ARROWS_ALT,
                            ImVec2 ( 40.0f * SKIF_ImGui_GlobalDPIScale,
                                      0.0f ) )
         )
      {
        _registry.bSmallMode = ! _registry.bSmallMode;
        _registry.regKVSmallMode.putData (_registry.bSmallMode);

        changedMode = true;

        // Hide the window for the 4 following frames as ImGui determines the sizes of items etc.
        //   This prevent flashing and elements appearing too large during those frames.
        ImGui::GetCurrentWindow()->HiddenFramesCannotSkipItems += 4;

        /* TODO: Fix Launcher creating timers on SKIF_hWnd = 0,
         * causing SKIF to be unable to close them later if switched out from the mode.
        
        // If the user changed mode, cancel the exit action.
        if (bExitOnInjection)
          bExitOnInjection = false;

        // Be sure to load all extended character sets when changing mode
        if (_Signal.Launcher)
          invalidateFonts = true;

        */
      }

      if ( (io.KeyCtrl && io.KeysDown['1']    && io.KeysDownDuration['1']    == 0.0f)
         )
      {
        if (SKIF_Tab_Selected != UITab_Library)
            SKIF_Tab_ChangeTo  = UITab_Library;
      }

      if ( (io.KeyCtrl && io.KeysDown['2']    && io.KeysDownDuration['2']    == 0.0f)
         )
      {
        if (SKIF_Tab_Selected != UITab_Monitor)
            SKIF_Tab_ChangeTo  = UITab_Monitor;
      }

      if ( (io.KeyCtrl && io.KeysDown['3']    && io.KeysDownDuration['3']    == 0.0f)
         )
      {
        if (SKIF_Tab_Selected != UITab_Settings)
            SKIF_Tab_ChangeTo  = UITab_Settings;
      }

      if ( (io.KeyCtrl && io.KeysDown['4']    && io.KeysDownDuration['4']    == 0.0f)
         )
      {
        if (SKIF_Tab_Selected != UITab_About)
            SKIF_Tab_ChangeTo  = UITab_About;
      }

      if ( (io.KeyCtrl && io.KeysDown['A']    && io.KeysDownDuration['A']    == 0.0f)
         )
      {
        if (SKIF_Tab_Selected != UITab_Library)
            SKIF_Tab_ChangeTo  = UITab_Library;

        AddGamePopup = PopupState::Open;
      }

      if (ImGui::IsKeyPressedMap (ImGuiKey_Escape))
      {
        if (AddGamePopup    != PopupState::Closed ||
            ModifyGamePopup != PopupState::Closed ||
            RemoveGamePopup != PopupState::Closed)
          ImGui::ClosePopupsOverWindow (ImGui::GetCurrentWindowRead ( ), false);
      }

      ImGui::SameLine ();

      if ( (io.KeyCtrl && io.KeysDown['N'] && io.KeysDownDuration['N'] == 0.0f) ||
            ImGui::Button (ICON_FA_WINDOW_MINIMIZE, ImVec2 ( 30.0f * SKIF_ImGui_GlobalDPIScale,
                                                             0.0f ) ) )
      {
        ShowWindow (hWnd, SW_MINIMIZE);
      }

      ImGui::SameLine ();

      if ( (io.KeyCtrl && io.KeysDown['Q'] && io.KeysDownDuration['Q'] == 0.0f) ||
            ImGui::Button (ICON_FA_WINDOW_CLOSE, ImVec2 ( 30.0f * SKIF_ImGui_GlobalDPIScale,
                                                          0.0f ) )
          || bKeepWindowAlive == false
         )
      {
        if (_registry.bCloseToTray && bKeepWindowAlive && ! SKIF_isTrayed)
        {
          bKeepWindowAlive = true;
          ShowWindow       (hWnd, SW_MINIMIZE);
          ShowWindow       (hWnd, SW_HIDE);
          UpdateWindow     (hWnd);
          SKIF_isTrayed    = true;
        }

        else
        {
          if (_inject.bCurrentState && ! _registry.bAllowBackgroundService )
            _inject._StartStopInject (true);

          bKeepProcessAlive = false;
        }
      }

      ImGui::PopStyleVar ();
      
      if (_registry.bCloseToTray)
        SKIF_ImGui_SetHoverTip ("This app will close to the notification area");
      else if (_inject.bCurrentState && _registry.bAllowBackgroundService)
        SKIF_ImGui_SetHoverTip ("Service continues running after this app is closed");

      ImGui::SetCursorPos (topCursorPos);

      // End of top right window buttons

      // Prepare Shelly color and Y position
      const float fGhostTimeStep = 0.01f;
      static float fGhostTime    = 0.0f;

      float fGhostYPos = 4.0f * (std::sin(6 * (fGhostTime / 2.5f)) + 1);

      ImVec4 vGhostColor = ImColor::ImColor (
          0.5f * (std::sin(6 * (fGhostTime / 2.5f)) + 1),
          0.5f * (std::sin(6 * (fGhostTime / 2.5f + 1.0f / 3.0f)) + 1),
          0.5f * (std::sin(6 * (fGhostTime / 2.5f + 2.0f / 3.0f)) + 1)
        );

      if (_registry.iStyle == 2)
        vGhostColor = vGhostColor * ImVec4(0.8f, 0.8f, 0.8f, 1.0f);

      ImGui::BeginGroup ();

      // Begin Small Mode
#pragma region UI: Small Mode

      if (_registry.bSmallMode)
      {
        SKIF_Tab_Selected = UITab_SmallMode;

        auto smallMode_id =
          ImGui::GetID ("###Small_Mode_Frame");

        // A new line to allow the window titlebar buttons to be accessible
        ImGui::NewLine ();

        SKIF_ImGui_BeginChildFrame ( smallMode_id,
          ImVec2 ( 400.0f * SKIF_ImGui_GlobalDPIScale,
                    12.0f * ImGui::GetTextLineHeightWithSpacing () ),
            ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NavFlattened      |
            ImGuiWindowFlags_NoScrollbar            | ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoBackground
        );

        _inject._GlobalInjectionCtl ();

        ImGui::EndChildFrame ();

        SKIF_ImGui_ServiceMenu ();

        // Shelly the Ghost (Small Mode)

        ImGui::SetCursorPosX (2.0f);

        ImGui::SetCursorPosY (
          7.0f * SKIF_ImGui_GlobalDPIScale
        );

        ImGui::Text ("");

        if (                          _registry.iGhostVisibility == 1 ||
            (_inject.bCurrentState && _registry.iGhostVisibility == 2) )
        {
          // Required for subsequent GetCursorPosX() calls to get the right pos, as otherwise it resets to 0.0f
          ImGui::SameLine();

          // Non-static as it needs to be updated constantly due to mixed-DPI monitor configs
          float fMaxPos = SKIF_vecCurrentMode.x - 136.0f * SKIF_ImGui_GlobalDPIScale;

          static float direction = -1.0f;
          static float fMinPos   =  0.0f;

          static float fNewPos   =
                     ( fMaxPos   -
                       fMinPos ) * 0.5f;

                   fNewPos +=
                       direction * SKIF_ImGui_GlobalDPIScale;

          if (     fNewPos < fMinPos)
          {        fNewPos = fMinPos - direction * 2.0f; direction = -direction; }
          else if (fNewPos > fMaxPos)
          {        fNewPos = fMaxPos - direction * 2.0f; direction = -direction; }

          ImGui::SameLine    ( 0.0f, fNewPos );


          /* Original method:
          auto current_time =
            SKIF_Util_timeGetTime ();

          ImGui::SetCursorPosY (
            ImGui::GetCursorPosY () + 4.0f * sin ((current_time % 500) / 125.f)
                               );
          ImVec4 vGhostColor =
            ImColor::HSV (   (float)(current_time % 1400)/ 2800.f,
                (.5f + (sin ((float)(current_time % 750) /  250.f)) * .5f) / 2.f,
                   1.f );
          */

          ImGui::SetCursorPosY (
            ImGui::GetCursorPosY () + fGhostYPos
                               );

          ImGui::TextColored (vGhostColor, ICON_FA_GHOST);
        }
      } // End Small Mode

#pragma endregion

      // Begin Large Mode
#pragma region UI: Large Mode

      else
      {
        ImGui::BeginTabBar ( "###SKIF_TAB_BAR",
                               ImGuiTabBarFlags_FittingPolicyResizeDown |
                               ImGuiTabBarFlags_FittingPolicyScroll );


        if (ImGui::BeginTabItem (" " ICON_FA_GAMEPAD " Library ", nullptr, ImGuiTabItemFlags_NoTooltip | ((SKIF_Tab_ChangeTo == UITab_Library) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)))
        {
          if (! _registry.bFirstLaunch)
          {
            // Select the About tab on first launch
            _registry.bFirstLaunch = ! _registry.bFirstLaunch;
            SKIF_Tab_ChangeTo = UITab_About;

            // Store in the registry so this only occur once.
            _registry.regKVFirstLaunch.putData(_registry.bFirstLaunch);
          }

          extern float fTint;
          if (SKIF_Tab_Selected != UITab_Library)
          {
            // Reset the dimmed cover when going back to the tab
            if (_registry.iDimCovers == 2)
              fTint = 0.75f;
          }

          SKIF_Tab_Selected = UITab_Library;
          if (SKIF_Tab_ChangeTo == UITab_Library)
            SKIF_Tab_ChangeTo = UITab_None;
            

          extern void
            SKIF_UI_Tab_DrawLibrary (void);
            SKIF_UI_Tab_DrawLibrary (     );

          ImGui::EndTabItem ();
        }


        if (ImGui::BeginTabItem (" " ICON_FA_TASKS " Monitor ", nullptr, ImGuiTabItemFlags_NoTooltip | ((SKIF_Tab_ChangeTo == UITab_Monitor) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)))
        {
          SKIF_ImGui_BeginTabChildFrame ();

          extern void
            SKIF_UI_Tab_DrawMonitor (void);
            SKIF_UI_Tab_DrawMonitor (    );

          ImGui::EndChildFrame    ( );
          ImGui::EndTabItem       ( );
        }

        // Unload the SpecialK DLL file if the tab is not selected
        else if (hModSpecialK != 0)
        {
          FreeLibrary (hModSpecialK);
          hModSpecialK = nullptr;
        }

        if (ImGui::BeginTabItem (" " ICON_FA_COG " Settings ", nullptr, ImGuiTabItemFlags_NoTooltip | ((SKIF_Tab_ChangeTo == UITab_Settings) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)))
        {
          SKIF_ImGui_BeginTabChildFrame ();

          SKIF_UI_Tab_DrawSettings( );

          ImGui::EndChildFrame    ( );
          ImGui::EndTabItem       ( );
        }

        if (ImGui::BeginTabItem (" " ICON_FA_INFO_CIRCLE " About ", nullptr, ImGuiTabItemFlags_NoTooltip | ((SKIF_Tab_ChangeTo == UITab_About) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)))
        {
          SKIF_ImGui_BeginTabChildFrame ();

          SKIF_Tab_Selected = UITab_About;
          if (SKIF_Tab_ChangeTo == UITab_About)
            SKIF_Tab_ChangeTo = UITab_None;

          // About Tab
          SKIF_UI_Tab_DrawAbout   ( );

          ImGui::EndChildFrame    ( );
          ImGui::EndTabItem       ( );
        }

        // Shelly the Ghost

        float title_len = (tinyDPIFonts) ? ImGui::CalcTextSize(SKIF_WINDOW_TITLE_SHORT_A).x : ImGui::CalcTextSize(SKIF_WINDOW_TITLE_A).x;
        float title_pos = SKIF_vecCurrentMode.x / 2.0f - title_len / 2.0f;

        ImGui::SetCursorPosX (title_pos);

        ImGui::SetCursorPosY (
          7.0f * SKIF_ImGui_GlobalDPIScale
        );

        if (SKIF_GetEffectivePowerMode() != "None")
          ImGui::TextColored (ImVec4 (0.5f, 0.5f, 0.5f, 1.f),
                                (tinyDPIFonts) ? SKIF_WINDOW_TITLE_SHORT_A
                                               : SK_FormatString (R"(%s (%s))", SKIF_WINDOW_TITLE_A, SKIF_GetEffectivePowerMode ( ).c_str ( ) ).c_str ( ));
        else
          ImGui::TextColored (ImVec4 (0.5f, 0.5f, 0.5f, 1.f),
                                (tinyDPIFonts) ? SKIF_WINDOW_TITLE_SHORT_A
                                               : SKIF_WINDOW_TITLE_A);

        if (                          _registry.iGhostVisibility == 1 ||
            (_inject.bCurrentState && _registry.iGhostVisibility == 2) )
        {
          // Required for subsequent GetCursorPosX() calls to get the right pos, as otherwise it resets to 0.0f
          ImGui::SameLine();

          // Non-static as it needs to be updated constantly due to mixed-DPI monitor configs
          float fMaxPos = SKIF_vecCurrentMode.x - ImGui::GetCursorPosX() - 125.0f * SKIF_ImGui_GlobalDPIScale;

          static float direction = -1.0f;
          static float fMinPos   =  0.0f;

          static float fNewPos   =
                     ( fMaxPos   -
                       fMinPos ) * 0.5f;

                   fNewPos +=
                       direction * SKIF_ImGui_GlobalDPIScale;

          if (     fNewPos < fMinPos)
          {        fNewPos = fMinPos - direction * 2.0f; direction = -direction; }
          else if (fNewPos > fMaxPos)
          {        fNewPos = fMaxPos - direction * 2.0f; direction = -direction; }

          ImGui::SameLine    ( 0.0f, fNewPos );

          /*
          auto current_time =
            SKIF_Util_timeGetTime ();

          ImGui::SetCursorPosY (
            ImGui::GetCursorPosY () + 4.0f * sin ((current_time % 500) / 125.f)
                               );

          ImVec4 vGhostColor =
            ImColor::HSV (   (float)(current_time % 1400)/ 2800.f,
                (.5f + (sin ((float)(current_time % 750) /  250.f)) * .5f) / 2.f,
                   1.f );

          if (_registry.iStyle == 2)
            vGhostColor = vGhostColor * ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
          */
          
          ImGui::SetCursorPosY (
            ImGui::GetCursorPosY () + fGhostYPos
                               );

          ImGui::TextColored (vGhostColor, ICON_FA_GHOST);
        }

        ImGui::EndTabBar          ( );
      } // End Large Mode

#pragma endregion

      ImGui::EndGroup             ( );

      // Increase Shelly timestep for next frame
      fGhostTime += fGhostTimeStep;

      // Status Bar at the bottom
      if ( ! _registry.bSmallMode        &&
           ! _registry.bDisableStatusBar )
      {
        // This counteracts math performed on SKIF_vecLargeMode.y at the beginning of the frame
        float statusBarY = ImGui::GetWindowSize().y;
              statusBarY -= 31.0f * SKIF_ImGui_GlobalDPIScale;
              statusBarY -= (_registry.bDisableTooltips) ? 18.0f * SKIF_ImGui_GlobalDPIScale : 0.0f;
        ImGui::SetCursorPosY (statusBarY);

        if (! _registry.bDisableBorders)
          ImGui::Separator    (       );

        // End Separation

        // Begin Add Game
        ImVec2 tmpPos = ImGui::GetCursorPos();

        static bool btnHovered = false;
        ImGui::PushStyleColor (ImGuiCol_Button,        ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg));
        ImGui::PushStyleColor (ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg)); //ImColor (64,  69,  82).Value);
        ImGui::PushStyleColor (ImGuiCol_ButtonActive,  ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg)); //ImColor (56, 60, 74).Value);

        if (btnHovered)
          ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption)); //ImVec4(1, 1, 1, 1));
        else
          ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)); //ImVec4(0.5f, 0.5f, 0.5f, 1.f));

        ImGui::PushStyleVar (ImGuiStyleVar_FrameBorderSize, 0.0f);
        if (ImGui::Button ( ICON_FA_PLUS_SQUARE " Add Game"))
        {
          AddGamePopup = PopupState::Open;
          if (SKIF_Tab_Selected != UITab_Library)
            SKIF_Tab_ChangeTo = UITab_Library;
        }
        ImGui::PopStyleVar  ( );

        btnHovered = ImGui::IsItemHovered() || ImGui::IsItemActive();

        ImGui::PopStyleColor (4);

        ImGui::SetCursorPos(tmpPos);
        // End Add Game

        // Begin Pulsating Refresh Icon
        if (update_thread.load ( ) == 1)
        {
          ImGui::SetCursorPosX (
            ImGui::GetCursorPosX () +
            ImGui::GetWindowSize ().x -
              ( ImGui::CalcTextSize ( ICON_FA_SYNC ).x ) -
            ImGui::GetCursorPosX () -
            ImGui::GetStyle   ().ItemSpacing.x * 2
          );

          ImGui::SetCursorPosY ( ImGui::GetCursorPosY () + style.FramePadding.y);

          ImGui::TextColored ( ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption) *
                                ImVec4 (0.75f, 0.75f, 0.75f, 0.50f + 0.5f * (float)sin (SKIF_Util_timeGetTime() * 1 * 3.14 * 2)
                               ), ICON_FA_SYNC );
        }

        ImGui::SetCursorPos(tmpPos);
        // End Refresh Icon

        // Begin Status Bar Text
        auto _StatusPartSize = [&](std::string& part) -> float
        {
          return
            part.empty () ?
                      0.0f : ImGui::CalcTextSize (
                                            part.c_str ()
                                                ).x;
        };

        float fStatusWidth = _StatusPartSize (SKIF_StatusBarText),
              fHelpWidth   = _StatusPartSize (SKIF_StatusBarHelp);

        ImGui::SetCursorPosX (
          ImGui::GetCursorPosX () +
          ImGui::GetWindowSize ().x -
            ( fStatusWidth +
              fHelpWidth ) -
          ImGui::GetCursorPosX () -
          ImGui::GetStyle   ().ItemSpacing.x * 2
        );

        ImGui::SetCursorPosY ( ImGui::GetCursorPosY () + style.FramePadding.y);

        ImGui::TextColored ( ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption) * ImVec4 (0.75f, 0.75f, 0.75f, 1.00f),
                                "%s", SKIF_StatusBarText.c_str ()
        );

        if (! SKIF_StatusBarHelp.empty ())
        {
          ImGui::SameLine ();
          ImGui::SetCursorPosX (
            ImGui::GetCursorPosX () -
            ImGui::GetStyle      ().ItemSpacing.x
          );
          ImGui::TextDisabled ("%s", SKIF_StatusBarHelp.c_str ());
        }

        // Clear the status every frame, it's mostly used for mouse hover tooltips.
        SKIF_StatusBarText.clear ();
        SKIF_StatusBarHelp.clear ();

        // End Status Bar Text
      }


      // Font warning
      if (failedLoadFontsPrompt && ! HiddenFramesContinueRendering)
      {
        failedLoadFontsPrompt = false;

        ImGui::OpenPopup ("###FailedFontsPopup");
      }
      

      float ffailedLoadFontsWidth = 400.0f * SKIF_ImGui_GlobalDPIScale;
      ImGui::SetNextWindowSize (ImVec2 (ffailedLoadFontsWidth, 0.0f));

      if (ImGui::BeginPopupModal ("Fonts failed to load###FailedFontsPopup", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
      {
        ImGui::TreePush    ("");

        SKIF_ImGui_Spacing ( );

        ImGui::TextWrapped ("The selected character sets failed to load due to system limitations and have been reset.");
        ImGui::NewLine     ( );
        ImGui::TextWrapped ("Please limit the selection to only the most essential.");

        SKIF_ImGui_Spacing ( );
        SKIF_ImGui_Spacing ( );

        ImVec2 vButtonSize = ImVec2(80.0f * SKIF_ImGui_GlobalDPIScale, 0.0f);

        ImGui::SetCursorPosX (ffailedLoadFontsWidth / 2 - vButtonSize.x / 2);

        if (ImGui::Button  ("OK", vButtonSize))
          ImGui::CloseCurrentPopup ( );

        SKIF_ImGui_Spacing ( );

        ImGui::TreePop     ( );

        ImGui::EndPopup ( );
      }

      // Uses a Directory Watch signal, so this is cheap; do it once every frame.
      svcTransitionFromPendingState = _inject.TestServletRunlevel ();

      // Another Directory Watch signal to check if DLL files should be refreshed.
      // 
      // TODO: This directory watch gets assigned to the current tab only, meaning it won't
      //       trigger an automated refresh if the user switches tabs before it is signaled.
      // 
      //       This also means the main DLL refresh watch is tied to the tab SKIF opens up
      //       on, whether that be SMALL MODE, LIBRARY, or ABOUT tab (first launch).
      static SKIF_DirectoryWatch root_folder;
      if (root_folder.isSignaled (path_cache.specialk_install, true))
      {
        // If the Special K DLL file is currently loaded, unload it
        if (hModSpecialK != 0)
        {
          FreeLibrary (hModSpecialK);
          hModSpecialK = nullptr;
        }

        _inject._DanceOfTheDLLFiles     ();
        _inject._RefreshSKDLLVersions   ();
      }

      static std::wstring updateRoot = SK_FormatStringW (LR"(%ws\Version\)", path_cache.specialk_userdata);

      // Set up the update directory watch
      static SKIF_DirectoryWatch version_folder (updateRoot, true, true);

      // Update newVersion on every frame
      newVersion = SKIF_CheckForUpdates ();

      if (version_folder.isSignaled (updateRoot, true, true, FALSE, FILE_NOTIFY_CHANGE_SIZE))
        updaterExpectingNewFile = 10;

      // Only proceed if we expect a new version
      if (! newVersion.filename.empty ())
      {
        SK_RunOnce(
          SKIF_UpdateReady = showUpdatePrompt = PathFileExists ((updateRoot + newVersion.filename).c_str())
        )

        // Download has finished, prompt about starting the installer here.
        if (updaterExpectingNewFile > 0 && PathFileExists ((updateRoot + newVersion.filename).c_str()))
        {
          SKIF_UpdateReady = showUpdatePrompt = true;
          updaterExpectingNewFile = 0;
        }
        else if (changedUpdateChannel)
        {
          changedUpdateChannel = false;
          SKIF_UpdateReady = showUpdatePrompt = PathFileExists((updateRoot + newVersion.filename).c_str());
        }

        if (showUpdatePrompt && newVersion.description != _registry.wsIgnoreUpdate)
        {
          showUpdatePrompt = false;
          UpdatePromptPopup = PopupState::Open;
        }
      }

      static float  UpdateAvailableWidth = 0.0f;
      static float  calculatedWidth      = 0.0f;
      static float  NumLines             = 0;
      static size_t NumCharsOnLine       = 0;
      static std::vector<char> vecNotes;

      if (UpdatePromptPopup == PopupState::Open && ! HiddenFramesContinueRendering && ! ImGui::IsAnyPopupOpen ( ))
      {
        //UpdateAvailableWidth = ImGui::CalcTextSize ((SK_WideCharToUTF8 (newVersion.description) + " is ready to be installed.").c_str()).x + 3 * ImGui::GetStyle().ItemSpacing.x;
        UpdateAvailableWidth = 360.0f;

        if (vecNotes.empty())
        {
          calculatedWidth = 0.0f;
          NumLines        = 0.0f;
          NumCharsOnLine  = 0;

          if (! newVersion.releasenotes.empty())
          {
            std::string strNotes = SK_WideCharToUTF8(newVersion.releasenotes);

            // Ensure the text wraps at every 110 character (longest line used yet, in v0.8.32)
            strNotes = TextFlow::Column(strNotes).width(110).toString();

            // Calc longest line and number of lines
            std::istringstream iss(strNotes);
            for (std::string line; std::getline(iss, line); NumLines++)
              if (line.length() > NumCharsOnLine)
                NumCharsOnLine = line.length();

            // 8.0f  per character
            // 15.0f for the scrollbar
            calculatedWidth = static_cast<float>(NumCharsOnLine) * 8.0f + 15.0f;

            // Populate the vector
            vecNotes.push_back ('\n');

            for (size_t i = 0; i < strNotes.length(); i++)
              vecNotes.push_back(strNotes[i]);

            vecNotes.push_back ('\n');

            // Ensure the vector array is double null terminated
            vecNotes.push_back ('\0');
            vecNotes.push_back ('\0');

            // Increase NumLines by 3, two from vecNotes.push_back and
            //  two from ImGui's love of having one and a half empty line below content
            NumLines += 3.5f;

            // Only allow up to 20 lines at most
            if (NumLines > 20.0f)
              NumLines = 20.0f;
          }
        }

        if ((calculatedWidth * SKIF_ImGui_GlobalDPIScale) > UpdateAvailableWidth)
          UpdateAvailableWidth = calculatedWidth;

        ImGui::OpenPopup ("###UpdatePrompt");
      }

      // Update Available prompt
      // 730px    - Full popup width
      // 715px    - Release Notes width
      //  15px    - Approx. scrollbar width
      //   7.78px - Approx. character width (700px / 90 characters)
      ImGui::SetNextWindowSize (
        ImVec2 ( UpdateAvailableWidth * SKIF_ImGui_GlobalDPIScale,
                   0.0f )
      );
      ImGui::SetNextWindowPos (ImGui::GetCurrentWindowRead()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));

      if (ImGui::BeginPopupModal ( "Version Available###UpdatePrompt", nullptr,
                                     ImGuiWindowFlags_NoResize         |
                                     ImGuiWindowFlags_NoMove           |
                                     ImGuiWindowFlags_AlwaysAutoResize )
         )
      {
#ifdef _WIN64
        std::wstring currentVersion = SK_UTF8ToWideChar (_inject.SKVer64);
#else
        std::wstring currentVersion = SK_UTF8ToWideChar (_inject.SKVer32);
#endif
        std::string compareLabel;
        ImVec4      compareColor;
        bool        compareNewer = false;

        if (UpdatePromptPopup == PopupState::Open)
        {
          // Set the popup as opened after it has appeared (fixes popup not opening from other tabs)
          ImGuiWindow* window = ImGui::FindWindowByName ("###UpdatePrompt");
          if (window != nullptr && ! window->Appearing)
            UpdatePromptPopup = PopupState::Opened;
        }

        if (SKIF_Util_CompareVersionStrings (newVersion.version, currentVersion) > 0)
        {
          compareLabel = "This version is newer than currently installed.";
          compareColor = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success);
          compareNewer = true;
        }
        else
        {
          compareLabel = "This version is older than currently installed!";
          compareColor = ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning);
        }

        SKIF_ImGui_Spacing ();

        float fX = (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize((SK_WideCharToUTF8 (newVersion.description) + " is ready to be installed.").c_str()).x + (((compareNewer) ? 2 : 1) * ImGui::GetStyle().ItemSpacing.x)) / 2;

        ImGui::SetCursorPosX(fX);

        ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success), SK_WideCharToUTF8 (newVersion.description).c_str());
        ImGui::SameLine ( );
        ImGui::Text ("is ready to be installed.");

        SKIF_ImGui_Spacing ();

        ImGui::Text     ("Target Folder:");
        ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Warning));
        ImGui::TextWrapped    (SK_WideCharToUTF8 (path_cache.specialk_install).c_str());
        ImGui::PopStyleColor  ( );

        SKIF_ImGui_Spacing ();

        if (! vecNotes.empty())
        {
          ImGui::Text           ("Changes:");
          ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase));
          ImGui::PushFont       (fontConsolas);
          ImGui::InputTextEx    ( "###UpdatePromptChanges", "The update does not contain any release notes...",
                                  vecNotes.data(), static_cast<int>(vecNotes.size()),
                                    ImVec2 ( (UpdateAvailableWidth - 15.0f) * SKIF_ImGui_GlobalDPIScale,
                                        (fontConsolas->FontSize * NumLines) * SKIF_ImGui_GlobalDPIScale ),
                                      ImGuiInputTextFlags_Multiline | ImGuiInputTextFlags_ReadOnly );

          ImGui::PopFont        ( );
          ImGui::PopStyleColor  ( );

          SKIF_ImGui_Spacing ();
        }

        fX = (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(compareLabel.c_str()).x + (((compareNewer) ? 2 : 1) * ImGui::GetStyle().ItemSpacing.x)) / 2;

        ImGui::SetCursorPosX(fX);
          
        ImGui::TextColored (compareColor, compareLabel.c_str());

        SKIF_ImGui_Spacing ();

        fX = (ImGui::GetContentRegionAvail().x - (((compareNewer) ? 3 : 2) * 100 * SKIF_ImGui_GlobalDPIScale) - (((compareNewer) ? 2 : 1) * ImGui::GetStyle().ItemSpacing.x)) / 2;

        ImGui::SetCursorPosX(fX);

        if (ImGui::Button ("Install", ImVec2 ( 100 * SKIF_ImGui_GlobalDPIScale,
                                                25 * SKIF_ImGui_GlobalDPIScale )))
        {
          if (_inject.bCurrentState)
            _inject._StartStopInject(true);

          std::wstring args = SK_FormatStringW (LR"(/VerySilent /NoRestart /Shortcuts=false /DIR="%ws")", path_cache.specialk_install);

          SKIF_Util_OpenURI (updateRoot + newVersion.filename, SW_SHOWNORMAL, L"OPEN", args.c_str());

          //bExitOnInjection = true; // Used to close SKIF once the service had been stopped

          //Sleep(50);
          //bKeepProcessAlive = false;

          vecNotes.clear();
          UpdatePromptPopup = PopupState::Closed;
          ImGui::CloseCurrentPopup ();
        }

        ImGui::SameLine ();
        ImGui::Spacing  ();
        ImGui::SameLine ();

        if (compareNewer)
        {
          if (ImGui::Button ("Ignore", ImVec2 ( 100 * SKIF_ImGui_GlobalDPIScale,
                                                 25 * SKIF_ImGui_GlobalDPIScale )))
          {
            _registry.regKVIgnoreUpdate.putData(newVersion.description);

            vecNotes.clear();
            UpdatePromptPopup = PopupState::Closed;
            ImGui::CloseCurrentPopup ();
          }

          SKIF_ImGui_SetHoverTip ("SKIF will not prompt about this version again.");

          ImGui::SameLine ();
          ImGui::Spacing  ();
          ImGui::SameLine ();
        }

        if (ImGui::Button ("Cancel", ImVec2 ( 100 * SKIF_ImGui_GlobalDPIScale,
                                               25 * SKIF_ImGui_GlobalDPIScale )))
        {
          vecNotes.clear();
          UpdatePromptPopup = PopupState::Closed;
          ImGui::CloseCurrentPopup ();
        }

        ImGui::EndPopup ();
      }

      
      static float  HistoryPopupWidth          = 0.0f;
      static float  calcHistoryPopupWidth      = 0.0f;
      static float  HistoryPopupNumLines       = 0;
      static size_t HistoryPopupNumCharsOnLine = 0;
      static std::vector<char> vecHistory;

      if (HistoryPopup == PopupState::Open && ! HiddenFramesContinueRendering && ! ImGui::IsAnyPopupOpen ( ))
      {
        //HistoryPopupWidth = ImGui::CalcTextSize ((SK_WideCharToUTF8 (newVersion.description) + " is ready to be installed.").c_str()).x + 3 * ImGui::GetStyle().ItemSpacing.x;
        HistoryPopupWidth = 360.0f;

        if (vecHistory.empty())
        {
          calcHistoryPopupWidth = 0.0f;
          HistoryPopupNumLines        = 0.0f;
          HistoryPopupNumCharsOnLine  = 0;

          if (! newVersion.history.empty())
          {
            std::string strHistory = newVersion.history;

            // Ensure the text wraps at every 110 character (longest line used yet, in v0.8.32)
            strHistory = TextFlow::Column(strHistory).width(110).toString();

            // Calc longest line and number of lines
            std::istringstream iss(strHistory);
            for (std::string line; std::getline(iss, line); HistoryPopupNumLines++)
              if (line.length() > HistoryPopupNumCharsOnLine)
                HistoryPopupNumCharsOnLine = line.length();

            // 8.0f  per character
            // 15.0f for the scrollbar
            calcHistoryPopupWidth = static_cast<float>(HistoryPopupNumCharsOnLine) * 8.0f + 15.0f;

            // Populate the vector
            vecHistory.push_back ('\n');

            for (size_t i = 0; i < strHistory.length(); i++)
              vecHistory.push_back(strHistory[i]);

            vecHistory.push_back ('\n');

            // Ensure the vector array is double null terminated
            vecHistory.push_back ('\0');
            vecHistory.push_back ('\0');

            // Increase NumLines by 3, two from vecHistory.push_back and
            //  two from ImGui's love of having one and a half empty line below content
            HistoryPopupNumLines += 3.5f;

            // Only allow up to 20 lines at most
            if (HistoryPopupNumLines > 40.0f)
              HistoryPopupNumLines = 40.0f;
          }
        }

        if ((calcHistoryPopupWidth * SKIF_ImGui_GlobalDPIScale) > HistoryPopupWidth)
          HistoryPopupWidth = calcHistoryPopupWidth;

        ImGui::OpenPopup ("###History");
      }
      
      ImGui::SetNextWindowSize (
        ImVec2 ( HistoryPopupWidth * SKIF_ImGui_GlobalDPIScale,
                   0.0f )
      );
      ImGui::SetNextWindowPos (ImGui::GetCurrentWindowRead()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2 (0.5f, 0.5f));

      if (ImGui::BeginPopupModal ( "Changelog###History", nullptr,
                                     ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_AlwaysAutoResize )
         )
      {

        if (HistoryPopup == PopupState::Open)
        {
          // Set the popup as opened after it has appeared (fixes popup not opening from other tabs)
          ImGuiWindow* window = ImGui::FindWindowByName ("###History");
          if (window != nullptr && ! window->Appearing)
            HistoryPopup = PopupState::Opened;
        }

        SKIF_ImGui_Spacing ();

        if (! newVersion.history.empty())
        {
          ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase));
          ImGui::PushFont       (fontConsolas);
          ImGui::InputTextEx    ( "###HistoryChanges", "No historical changes detected...",
                                  vecHistory.data(), static_cast<int>(vecHistory.size()),
                                    ImVec2 ( (HistoryPopupWidth - 15.0f) * SKIF_ImGui_GlobalDPIScale,
                         (fontConsolas->FontSize * HistoryPopupNumLines) * SKIF_ImGui_GlobalDPIScale ),
                                      ImGuiInputTextFlags_Multiline | ImGuiInputTextFlags_ReadOnly );

          ImGui::PopFont        ( );
          ImGui::PopStyleColor  ( );

          SKIF_ImGui_Spacing ();
        }

        SKIF_ImGui_Spacing ();

        float fX = (ImGui::GetContentRegionAvail().x - 100 * SKIF_ImGui_GlobalDPIScale) / 2;

        ImGui::SetCursorPosX(fX);

        if (ImGui::Button ("Close", ImVec2 ( 100 * SKIF_ImGui_GlobalDPIScale,
                                              25 * SKIF_ImGui_GlobalDPIScale )))
        {
          HistoryPopup = PopupState::Closed;
          ImGui::CloseCurrentPopup ();
        }

        ImGui::EndPopup ();
      }

      // Ensure the taskbar overlay icon always shows the correct state
      if (_inject.bTaskbarOverlayIcon != _inject.bCurrentState)
        _inject._SetTaskbarOverlay      (_inject.bCurrentState);

      monitor_extent =
        ImGui::GetWindowAllowedExtentRect (
          ImGui::GetCurrentWindowRead   ()
        );
      windowPos      = ImGui::GetWindowPos ();
      windowRect.Min = ImGui::GetWindowPos ();
      windowRect.Max = ImGui::GetWindowPos () + ImGui::GetWindowSize ();

      if (! HoverTipActive)
        HoverTipDuration = 0;

      // This allows us to ensure the window gets set within the workspace on the second frame after launch
      SK_RunOnce (
        changedMode = true
      );

      // This allows us to compact the working set on launch
      SK_RunOnce (
        invalidatedFonts = SKIF_Util_timeGetTime ( )
      );

      if (invalidatedFonts > 0 &&
          invalidatedFonts + 500 < SKIF_Util_timeGetTime())
      {
        SKIF_Util_CompactWorkingSet ();
        invalidatedFonts = 0;
      }

      ImGui::End();
    }

#pragma endregion

    SK_RunOnce (_inject._InitializeJumpList ( ));

    // Actual rendering is conditional, this just processes input
    ImGui::Render ();

    // Conditional rendering
    bool bRefresh = (SKIF_isTrayed || IsIconic (hWnd)) ? false : true;
    if ( bRefresh)
    {
      //g_pd3dDeviceContext->OMSetRenderTargets    (1, &g_mainRenderTargetView, nullptr);
      //g_pd3dDeviceContext->ClearRenderTargetView (    g_mainRenderTargetView, (float*)&clear_color);

      if (! startedMinimized && ! SKIF_isTrayed)
      {
        ImGui_ImplDX11_RenderDrawData (ImGui::GetDrawData ());

        // Update, Render and Present the main and any additional Platform Windows
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
          ImGui::UpdatePlatformWindows        ();
          ImGui::RenderPlatformWindowsDefault (); // Eventually calls ImGui_ImplDX11_SwapBuffers ( ) which Presents ( )
        }
      }
    }

    if ( startedMinimized && SKIF_ImGui_IsFocused ( ) )
    {
      startedMinimized = false;
      if ( _registry.bOpenAtCursorPosition )
        RepositionSKIF = true;
    }

    // Release any leftover resources from last frame
    CComPtr <IUnknown> pResource = nullptr;
    while (! SKIF_ResourcesToFree.empty ())
    {
      if (SKIF_ResourcesToFree.try_pop (pResource))
      {
        PLOG_VERBOSE << "SKIF_ResourcesToFree: Releasing " << pResource.p;
        pResource.p->Release();
      }
    }

    // If process should stop, post WM_QUIT
    if ((! bKeepProcessAlive) && hWnd != 0)
      PostMessage (hWnd, WM_QUIT, 0x0, 0x0);

    // GamepadInputPump child thread
    static auto thread =
      _beginthreadex ( nullptr, 0x0, [](void *) -> unsigned
      {
        CRITICAL_SECTION            GamepadInputPump = { };
        InitializeCriticalSection (&GamepadInputPump);
        EnterCriticalSection      (&GamepadInputPump);
        
        SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_GamepadInputPump");

        DWORD packetLast = 0,
              packetNew  = 0;

        while (IsWindow (SKIF_hWnd))
        {
          extern DWORD ImGui_ImplWin32_UpdateGamepads (void);
          packetNew  = ImGui_ImplWin32_UpdateGamepads ( );

          if (packetNew  > 0  &&
              packetNew != packetLast)
          {
            packetLast = packetNew;
            //SendMessageTimeout (SKIF_hWnd, WM_NULL, 0x0, 0x0, 0x0, 100, nullptr);
            PostMessage (SKIF_hWnd, WM_SKIF_GAMEPAD, 0x0, 0x0);
          }

          // Sleep until we're woken up by WM_SETFOCUS if SKIF is unfocused
          if (! SKIF_ImGui_IsFocused ())
          {
            SK_RunOnce (SKIF_Util_CompactWorkingSet ());

            SleepConditionVariableCS (
              &SKIF_IsFocused, &GamepadInputPump,
                INFINITE
            );
          }

          // XInput tends to have ~3-7 ms of latency between updates
          //   best-case, try to delay the next poll until there's
          //     new data.
          Sleep (5);
        }

        LeaveCriticalSection  (&GamepadInputPump);
        DeleteCriticalSection (&GamepadInputPump);

        _endthreadex (0x0);

        return 0;
      }, nullptr, 0x0, nullptr
    );

    // Handle dynamic pausing
    bool pause = false;
    static int
      renderAdditionalFrames = 0;

    bool input = SKIF_ImGui_IsAnyInputDown ( ) || uiLastMsg == WM_SKIF_GAMEPAD ||
                   (uiLastMsg >= WM_MOUSEFIRST && uiLastMsg <= WM_MOUSELAST)   ||
                   (uiLastMsg >= WM_KEYFIRST   && uiLastMsg <= WM_KEYLAST  );
    
    // We want SKIF to continue rendering in some specific scenarios
    ImGuiWindow* wnd = ImGui::FindWindowByName ("###KeyboardHint");
    if (wnd != nullptr && wnd->Active)
      renderAdditionalFrames = ImGui::GetFrameCount ( ) + 3; // If the keyboard hint/search is active
    else if (uiLastMsg == WM_SETCURSOR  || uiLastMsg == WM_TIMER   ||
             uiLastMsg == WM_SETFOCUS   || uiLastMsg == WM_KILLFOCUS)
      renderAdditionalFrames = ImGui::GetFrameCount ( ) + 3; // If we received some event changes
    else if (input)
      renderAdditionalFrames = ImGui::GetFrameCount ( ) + 3; // If we received any gamepad input or an input is held down
    else if (svcTransitionFromPendingState)
      renderAdditionalFrames = ImGui::GetFrameCount ( ) + 3; // If we transitioned away from a pending service state
    else if (1.0f > ImGui::GetCurrentContext()->DimBgRatio && ImGui::GetCurrentContext()->DimBgRatio > 0.0f)
      renderAdditionalFrames = ImGui::GetFrameCount ( ) + 3; // If the background is currently currently undergoing a fade effect
    else if (SKIF_Tab_Selected == UITab_Library && (startupFadeIn == 1 || coverFadeActive))
      renderAdditionalFrames = ImGui::GetFrameCount ( ) + 3; // If the cover is currently undergoing a fade effect
    else if (uiLastMsg == WM_SKIF_COVER)
      renderAdditionalFrames = ImGui::GetFrameCount ( ) + 10; // If the cover is currently loading in
    else if (updaterExpectingNewFile > 0)
      renderAdditionalFrames = ImGui::GetFrameCount ( ) + updaterExpectingNewFile--; // If we are expecting a new update file
    /*
    else if (  AddGamePopup == PopupState::Open ||
               ConfirmPopup == PopupState::Open ||
            ModifyGamePopup == PopupState::Open ||
          UpdatePromptPopup == PopupState::Open ||
               HistoryPopup == PopupState::Open )
      renderAdditionalFrames = ImGui::GetFrameCount ( ) + 3; // If a popup is transitioning to an opened state
    */
    else if (ImGui::GetFrameCount ( ) > renderAdditionalFrames)
      renderAdditionalFrames = 0;

    //OutputDebugString((L"Framerate: " + std::to_wstring(ImGui::GetIO().Framerate) + L"\n").c_str());

    // Clear gamepad/nav input for the next frame as we're done with it
    memset (ImGui::GetIO ( ).NavInputs, 0, sizeof(ImGui::GetIO ( ).NavInputs));

    //if (uiLastMsg == WM_SKIF_GAMEPAD)
    //  OutputDebugString(L"[doWhile] Message spotted: WM_SKIF_GAMEPAD\n");
    //else if (uiLastMsg == WM_SKIF_COVER)
    //  OutputDebugString(L"[doWhile] Message spotted: WM_SKIF_COVER\n");
    //else if (uiLastMsg != 0x0)
    //  OutputDebugString((L"[doWhile] Message spotted: " + std::to_wstring(uiLastMsg) + L"\n").c_str());

    // If there is any popups opened when SKIF is unfocused and not hovered, close them.
    // TODO: Investigate if this is what causes dropdown lists from collapsing after SKIF has launched
    if (! SKIF_ImGui_IsFocused ( ) && ! ImGui::IsAnyItemHovered ( ) && ImGui::IsAnyPopupOpen ( ))
    {
      // Don't close any popups if AddGame, Confirm, or ModifyGame is shown.
      //   But we do close the RemoveGame popup since that's not as critical.
      if (     AddGamePopup != PopupState::Opened &&
               ConfirmPopup != PopupState::Opened &&
            ModifyGamePopup != PopupState::Opened &&
          UpdatePromptPopup != PopupState::Opened &&
               HistoryPopup != PopupState::Opened )
        ImGui::ClosePopupsOverWindow (ImGui::GetCurrentWindowRead ( ), false);
    }
    
    // Pause if we don't need to render any additional frames
    if (renderAdditionalFrames == 0)
      pause = true;
    // Don't pause if there's hidden frames that needs rendering
    if (HiddenFramesContinueRendering)
      pause = false;

    bool frameRateUnlocked = static_cast<DWORD>(ImGui::GetIO().Framerate) > (1000 / (dwDwmPeriod));
    //OutputDebugString((L"Frame rate unlocked: " + std::to_wstring(frameRateUnlocked) + L"\n").c_str());

    do
    {
      // Pause rendering
      if (pause)
      {
        //OutputDebugString((L"[" + SKIF_Util_timeGetTimeAsWStr() + L"][#" + std::to_wstring(ImGui::GetFrameCount()) + L"][PAUSE] Rendering paused!\n").c_str());

        // SetProcessInformation (Windows 8+)
        using SetProcessInformation_pfn =
          BOOL (WINAPI *)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);

        static SetProcessInformation_pfn
          SKIF_SetProcessInformation =
              (SetProcessInformation_pfn)GetProcAddress (LoadLibraryEx (L"kernel32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
              "SetProcessInformation");

        static PROCESS_POWER_THROTTLING_STATE PowerThrottling = {};
        PowerThrottling.Version     = PROCESS_POWER_THROTTLING_CURRENT_VERSION;

        // Enable Efficiency Mode in Windows (requires idle priority + EcoQoS)
        SetPriorityClass (GetCurrentProcess(), IDLE_PRIORITY_CLASS);
        if (SKIF_SetProcessInformation != nullptr)
        {
          PowerThrottling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
          PowerThrottling.StateMask   = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
          SKIF_SetProcessInformation (GetCurrentProcess(), ProcessPowerThrottling, &PowerThrottling, sizeof(PowerThrottling));
        }

        //OutputDebugString ((L"vWatchHandles[SKIF_Tab_Selected].second.size(): " + std::to_wstring(vWatchHandles[SKIF_Tab_Selected].second.size()) + L"\n").c_str());
        
        // Sleep until a message is in the queue or a change notification occurs
        static bool bWaitTimeoutMsgInputFallback = false;
        if (WAIT_FAILED == MsgWaitForMultipleObjects (static_cast<DWORD>(vWatchHandles[SKIF_Tab_Selected].second.size()), vWatchHandles[SKIF_Tab_Selected].second.data(), false, bWaitTimeoutMsgInputFallback ? dwDwmPeriod : INFINITE, QS_ALLINPUT))
        {
          SK_RunOnce (
          {
            PLOG_ERROR << "Waiting on a new message or change notification failed with error message: " << SKIF_Util_GetErrorAsWStr ( );
            PLOG_ERROR << "Timeout has permanently been set to the monitors refresh rate period (" << dwDwmPeriod << ") !";
            bWaitTimeoutMsgInputFallback = true;
          });
        }

        // Wake up and disable idle priority + ECO QoS (let the system take over)
        SetPriorityClass (GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
        if (SKIF_SetProcessInformation != nullptr)
        {
          PowerThrottling.ControlMask = 0;
          PowerThrottling.StateMask   = 0;
          SKIF_SetProcessInformation (GetCurrentProcess (), ProcessPowerThrottling, &PowerThrottling, sizeof (PowerThrottling));
        }

        // Always render 3 additional frames after we wake up
        renderAdditionalFrames = ImGui::GetFrameCount() + 3;
        
        //OutputDebugString((L"[" + SKIF_Util_timeGetTimeAsWStr() + L"][#" + std::to_wstring(ImGui::GetFrameCount()) + L"][AWAKE] Woken up again!\n").c_str());
      }
      
      // The below is required as a fallback if V-Sync OFF is forced on SKIF and e.g. analog stick drift is causing constant input.
      else if (frameRateUnlocked && input) // Throttle to monitors refresh rate unless a new event is triggered, or user input is posted, but only if the frame rate is detected as being unlocked
        MsgWaitForMultipleObjects (static_cast<DWORD>(vWatchHandles[SKIF_Tab_Selected].second.size()), vWatchHandles[SKIF_Tab_Selected].second.data(), false, dwDwmPeriod, QS_ALLINPUT);

      
      if (bRefresh) //bRefresh)
      {
        //auto timePre = SKIF_Util_timeGetTime();

        if (! msgDontRedraw && ! vSwapchainWaitHandles.empty())
        {
          static bool bWaitTimeoutSwapChainsFallback = false;
          //OutputDebugString((L"[" + SKIF_Util_timeGetTimeAsWStr() + L"][#" + std::to_wstring(ImGui::GetFrameCount()) + L"] Maybe we'll be waiting? (handles: " + std::to_wstring(vSwapchainWaitHandles.size()) + L")\n").c_str());
          if (WAIT_FAILED == WaitForMultipleObjectsEx (static_cast<DWORD>(vSwapchainWaitHandles.size()), vSwapchainWaitHandles.data(), true, bWaitTimeoutSwapChainsFallback ? dwDwmPeriod : INFINITE, true))
          {
            SK_RunOnce (
            {
              PLOG_ERROR << "Waiting on the swapchain wait objects failed with error message: " << SKIF_Util_GetErrorAsWStr ( );
              PLOG_ERROR << "Timeout has permanently been set to the monitors refresh rate period (" << dwDwmPeriod << ") !";
              bWaitTimeoutSwapChainsFallback = true;
            });
          }
        }
        // Only reason we use a timeout here is in case a swapchain gets destroyed on the same frame we try waiting on its handle

        //auto timePost = SKIF_Util_timeGetTime();
        //auto timeDiff = timePost - timePre;
        //PLOG_VERBOSE << "Waited: " << timeDiff << " ms (handles : " << vSwapchainWaitHandles.size() << ")";
        //OutputDebugString((L"Waited: " + std::to_wstring(timeDiff) + L" ms (handles: " + std::to_wstring(vSwapchainWaitHandles.size()) + L")\n").c_str());
      }
      
      // Reset stuff that's set as part of pumping the message queue
      msgDontRedraw = false;
      uiLastMsg     = 0x0;

      // Pump the message queue, and break if we receive a false (WM_QUIT or WM_QUERYENDSESSION)
      if (! _TranslateAndDispatch ( ))
        break;
      
      // Break if SKIF is no longer a window
      if (! IsWindow (hWnd))
        break;

    } while (! SKIF_Shutdown && msgDontRedraw); // For messages we don't want to redraw on, we set msgDontRedraw to true.
  }
  
  if (! _registry.bLastSelectedWritten)
  {
    _registry.regKVLastSelectedGame.putData  (_registry.iLastSelectedGame);
    _registry.regKVLastSelectedStore.putData (_registry.wsLastSelectedStore);
    _registry.bLastSelectedWritten = true;
    PLOG_INFO << "Wrote the last selected game to registry: " << _registry.iLastSelectedGame << " (" << _registry.wsLastSelectedStore << ")";
  }

  if (UnregisterHotKey (SKIF_hWnd, SKIF_HotKey_HDR))
    PLOG_INFO << "Removed the global hotkey for toggling HDR.";

  PLOG_INFO << "Killing timers...";
  KillTimer (SKIF_hWnd, IDT_REFRESH_ONDEMAND);
  KillTimer (SKIF_hWnd, IDT_REFRESH_PENDING);
  KillTimer (SKIF_hWnd, IDT_REFRESH_TOOLTIP);
  KillTimer (SKIF_hWnd, IDT_REFRESH_UPDATER);
  KillTimer (SKIF_hWnd, IDT_REFRESH_GAMES);

  PLOG_INFO << "Shutting down ImGui...";
  ImGui_ImplDX11_Shutdown   (    );
  ImGui_ImplWin32_Shutdown  (    );

  CleanupDeviceD3D          (    );

  PLOG_INFO << "Destroying notification icon...";
  Shell_NotifyIcon          (NIM_DELETE, &niData);
  DeleteObject              (niData.hIcon);
  niData.hIcon               = 0;
  DestroyWindow             (SKIF_Notify_hWnd);

  PLOG_INFO << "Destroying main window...";
  //if (hDC != 0)
  //  ReleaseDC               (hWnd, hDC);
  DestroyWindow             (hWnd);

  PLOG_INFO << "Destroying ImGui context...";
  ImGui::DestroyContext     (    );

  SKIF_Notify_hWnd = 0;
  SKIF_hWnd = 0;
       hWnd = 0;

  if (hEffectivePowerModeRegistration)
  {
    PLOG_DEBUG << "Unregistering SKIF for effective power mode notifications...";
    SKIF_PowerUnregisterFromEffectivePowerModeNotifications (hEffectivePowerModeRegistration);
  }

  PLOG_INFO << "Terminating process...";
  return 0;
}


// Helper functions

// D3D9 test stuff
//#define SKIF_D3D9_TEST
#ifdef SKIF_D3D9_TEST
#pragma comment (lib, "d3d9.lib")
#define D3D_DEBUG_INFO
#include <d3d9.h>
#endif

bool CreateDeviceD3D (HWND hWnd)
{
  UNREFERENCED_PARAMETER (hWnd);

#ifdef SKIF_D3D9_TEST
  /* Test D3D9 debugging */
  IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
  if (d3d == nullptr)
  {
    OutputDebugString(L"Direct3DCreate9() failed!\n");
  } else {
    OutputDebugString(L"Direct3DCreate9() succeeded!?\n");
  }

  D3DPRESENT_PARAMETERS pp = {};
  pp.BackBufferWidth = 800;
  pp.BackBufferHeight = 600;
  pp.BackBufferFormat = D3DFMT_X8R8G8B8;
  pp.BackBufferCount = 1;
  pp.MultiSampleType = D3DMULTISAMPLE_NONE;
  pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
  pp.hDeviceWindow = NULL;
  pp.Windowed = TRUE;
  pp.EnableAutoDepthStencil = TRUE;
  pp.AutoDepthStencilFormat = D3DFMT_D16;

  IDirect3DDevice9* device = nullptr;
  // Intentionally passing an invalid parameter to CreateDevice to cause an exception to be thrown by the D3D9 debug layer
  //HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device);
  HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, (D3DDEVTYPE)100, NULL, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device);
  if (FAILED(hr))
  {
    OutputDebugString(L"d3d->CreateDevice() failed!\n");
  } else {
    OutputDebugString(L"d3d->CreateDevice() succeeded!?\n");
  }
#endif

  // Windows 7 (with the Platform Update) and newer
  SKIF_bCanFlip                 =         true; // Should never be set to false here

  if (SKIF_bCanFlip)
  {
    // Windows 8.1+
    SKIF_bCanWaitSwapchain      =
      SKIF_Util_IsWindows8Point1OrGreater ();

    // Windows 10+
    SKIF_bCanFlipDiscard        =
      SKIF_Util_IsWindows10OrGreater      ();

    // Windows 10 1709+ (Build 16299)
    SKIF_bCanHDR                =
      SKIF_Util_IsWindowsVersionOrGreater (10, 0, 16299) &&
      SKIF_Util_IsHDRSupported            (true);

    CComPtr <IDXGIFactory5>
                 pFactory5;

    CreateDXGIFactory1 (__uuidof (IDXGIFactory5), (void **)&pFactory5.p );

    // Windows 10+
    if (pFactory5 != nullptr)
    {
      BOOL supportsTearing = FALSE;
      pFactory5->CheckFeatureSupport (
                            DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                          &supportsTearing,
                                  sizeof  (supportsTearing)
                                                );
      SKIF_bCanAllowTearing = supportsTearing != FALSE;

      pFactory5.Release ( );
    }
  }

  // Overrides
  //SKIF_bCanAllowTearing       = false; // Allow Tearing
  //SKIF_bCanFlipDiscard        = false; // Flip Discard
  //SKIF_bCanFlip               = false; // Flip Sequential (if this is false, BitBlt Discard will be used instead)
  //SKIF_bCanWaitSwapchain      = false; // Waitable Swapchain


  UINT createDeviceFlags = 0;
  // This MUST be disabled before public release! Otherwise systems without the Windows SDK installed will crash on launch.
  //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG; // Enable debug layer of D3D11

  D3D_FEATURE_LEVEL featureLevel;
  const D3D_FEATURE_LEVEL
                    featureLevelArray [4] = {
    D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
  };

  if (D3D11CreateDevice ( nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                              createDeviceFlags, featureLevelArray,
                                                         sizeof (featureLevelArray) / sizeof featureLevel,
                                                D3D11_SDK_VERSION,
                                                       &g_pd3dDevice,
                                                                &featureLevel,
                                                       &g_pd3dDeviceContext) != S_OK ) return false;

  //CreateRenderTarget ();

  return true;
}

void CleanupDeviceD3D (void)
{
  //CleanupRenderTarget ();

  //IUnknown_AtomicRelease ((void **)&g_pSwapChain);
  IUnknown_AtomicRelease ((void **)&g_pd3dDeviceContext);
  IUnknown_AtomicRelease ((void **)&g_pd3dDevice);
}

// Prevent race conditions between asset loading and device init
//
void SKIF_WaitForDeviceInitD3D (void)
{
  while ( g_pd3dDevice        == nullptr    ||
          g_pd3dDeviceContext == nullptr /* ||
          g_pSwapChain        == nullptr  */ )
  {
    Sleep (10UL);
  }
}

CComPtr <ID3D11Device>
SKIF_D3D11_GetDevice (bool bWait)
{
  if (bWait)
    SKIF_WaitForDeviceInitD3D ();

  return
    g_pd3dDevice;
}

/*
void CreateRenderTarget (void)
{
  ID3D11Texture2D*                           pBackBuffer = nullptr;
  g_pSwapChain->GetBuffer (0, IID_PPV_ARGS (&pBackBuffer));

  if (pBackBuffer != nullptr)
  {
    g_pd3dDevice->CreateRenderTargetView   ( pBackBuffer, nullptr, &g_mainRenderTargetView);
                                             pBackBuffer->Release ();
  }
}

void CleanupRenderTarget (void)
{
  IUnknown_AtomicRelease ((void **)&g_mainRenderTargetView);
}
*/

// Win32 message handler
extern LRESULT
ImGui_ImplWin32_WndProcHandler (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT
WINAPI
SKIF_WndProc (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  // This is the message procedure for the (invisible) 0x0 SKIF window
  
  //OutputDebugString((L"[SKIF_WndProc] Message spotted: 0x" + std::format(L"{:x}", msg)    + L" (" + std::to_wstring(msg)    + L")" + (msg == WM_SETFOCUS ? L" == WM_SETFOCUS" : msg == WM_KILLFOCUS ? L" == WM_KILLFOCUS" : L"") + L"\n").c_str());
  //OutputDebugString((L"[SKIF_WndProc]          wParam: 0x" + std::format(L"{:x}", wParam) + L" (" + std::to_wstring(wParam) + L")" + ((HWND)wParam == NULL ? L" == NULL" : (HWND)wParam == SKIF_hWnd ? L" == SKIF_hWnd" : (HWND)wParam == SKIF_ImGui_hWnd ? L" == SKIF_ImGui_hWnd" : (HWND)wParam == SKIF_Notify_hWnd ? L" == SKIF_Notify_hWnd" : L"") + L"\n").c_str());
  //OutputDebugString((L"[SKIF_WndProc]          wParam: 0x" + std::format(L"{:x}", wParam) + L" (" + std::to_wstring(wParam) + L")\n").c_str());
  //OutputDebugString((L"[SKIF_WndProc]          lParam: 0x" + std::format(L"{:x}", lParam) + L" (" + std::to_wstring(lParam) + L")\n").c_str());

  /*
  if (msg != WM_NULL        && 
      msg != WM_NCHITTEST   &&
      msg != WM_MOUSEFIRST  &&
      msg != WM_MOUSEMOVE
    )
  {
    OutputDebugString((L"[WndProc] Message spotted: " + std::to_wstring(msg) + L" + wParam: " + std::to_wstring(wParam) + L"\n").c_str());
  }

  if (ImGui::GetCurrentContext() != NULL)
  {
    OutputDebugString((L"[WndProc][" + SKIF_Util_timeGetTimeAsWStr() + L"][#" + std::to_wstring(ImGui::GetFrameCount()) + L"] Message spotted : " + std::to_wstring(msg) + L" w wParam : " + std::to_wstring(wParam) + L"\n").c_str());
  }
  */

  if (ImGui_ImplWin32_WndProcHandler (hWnd, msg, wParam, lParam))
    return true;

  switch (msg)
  {
    case WM_HOTKEY:
      if (wParam == SKIF_HotKey_HDR)
        SKIF_Util_EnableHDROutput ( );
    break;
    case WM_QUIT:
      SKIF_Shutdown = true;
      break;
    case WM_ENDSESSION: 
      // Session is shutting down -- perform any last minute changes!
      if (wParam == 1)
      {
        PLOG_INFO << "Received system shutdown signal!";

        if (! _registry.bLastSelectedWritten)
        {
          _registry.regKVLastSelectedGame.putData  (_registry.iLastSelectedGame);
          _registry.regKVLastSelectedStore.putData (_registry.wsLastSelectedStore);
          _registry.bLastSelectedWritten = true;
          PLOG_INFO << "Wrote the last selected game to registry: " << _registry.iLastSelectedGame << " (" << _registry.wsLastSelectedStore << ")";
        }

        SKIF_Shutdown = true;
      }
      return 0;
      break;
    case WM_QUERYENDSESSION: // System wants to shut down and is asking if we can allow it
      //SKIF_Shutdown = true;
      PLOG_INFO << "System in querying if we can shut down!";
      return true;
      break;
    case WM_GETICON: // Work around bug in Task Manager sending this message every time it refreshes its process list
      msgDontRedraw = true;
      break;

    case WM_DISPLAYCHANGE:
      if (SKIF_Tab_Selected == UITab_Settings)
        RefreshSettingsTab = true; // Only set this if the Settings tab is actually selected
      break;

    case WM_SKIF_MINIMIZE:
      if (_registry.bCloseToTray && ! SKIF_isTrayed)
      {
        ShowWindow       (hWnd, SW_MINIMIZE);
        ShowWindow       (hWnd, SW_HIDE);
        UpdateWindow     (hWnd);
        SKIF_isTrayed    = true;
      }

      else if (! _registry.bCloseToTray) {
        ShowWindowAsync (hWnd, SW_MINIMIZE);
      }
      break;

    case WM_SKIF_START:
      if (_inject.runState != SKIF_InjectionContext::RunningState::Started)
        _inject._StartStopInject (false);
      break;

    case WM_SKIF_TEMPSTART:
      if (_inject.runState != SKIF_InjectionContext::RunningState::Started)
        _inject._StartStopInject (false, true);
      break;

    case WM_SKIF_STOP:
      _inject._StartStopInject   (true);
      break;

    case WM_SKIF_REFRESHGAMES: // TODO: Contemplate this design, and its position in the new design with situational pausing. Concerns WM_SKIF_REFRESHGAMES / IDT_REFRESH_GAMES.
      RepopulateGamesWasSet = SKIF_Util_timeGetTime();
      RepopulateGames = true;
      SelectNewSKIFGame = (uint32_t)wParam;

      SetTimer (SKIF_hWnd,
          IDT_REFRESH_GAMES,
          50,
          (TIMERPROC) NULL
      );
      break;

    case WM_SKIF_LAUNCHER:
      if (_inject.runState != SKIF_InjectionContext::RunningState::Started)
        _inject._StartStopInject (false, true);

      // Reload the whitelist as it might have been changed
      _inject._LoadList          (true);
      break;

    case WM_SKIF_RESTORE:
      _inject.bTaskbarOverlayIcon = false;

      if (! SKIF_isTrayed && ! IsIconic (hWnd))
        RepositionSKIF = true;

      if (SKIF_isTrayed)
      {
        SKIF_isTrayed               = false;
        ShowWindowAsync (hWnd, SW_SHOW);
      }

      ShowWindowAsync     (hWnd, SW_RESTORE);
      UpdateWindow        (hWnd);

      SetForegroundWindow (hWnd);
      SetActiveWindow     (hWnd);
      break;

    case WM_TIMER:
      switch (wParam)
      {
        case IDT_REFRESH_UPDATER:
          return 0;
        case IDT_REFRESH_TOOLTIP:
          // Do not redraw if SKIF is not being hovered by the mouse or a hover tip is not longer "active" any longer
          if (! SKIF_ImGui_IsMouseHovered ( ) || ! HoverTipActive)
            msgDontRedraw = true;
          
          KillTimer (SKIF_hWnd, IDT_REFRESH_TOOLTIP);
          return 0;
        case IDT_REFRESH_GAMES: // TODO: Contemplate this design, and its position in the new design with situational pausing. Concerns WM_SKIF_REFRESHGAMES / IDT_REFRESH_GAMES.
          if (RepopulateGamesWasSet != 0 && RepopulateGamesWasSet + 1000 < SKIF_Util_timeGetTime())
          {
            RepopulateGamesWasSet = 0;
            KillTimer (SKIF_hWnd, IDT_REFRESH_GAMES);
          }
          return 0;
        case IDT_REFRESH_ONDEMAND:
        case IDT_REFRESH_PENDING:
          // These are just dummy events to cause SKIF to refresh for a couple of frames more periodically
          return 0;
      }
      break;

   /* 2023-04-29: Disabled as this was never used and only referenced the unused empty "parent swapchain" that was never presented
    * HDR toggles are handled through ImGui_ImplDX11_NewFrame() and pFactory1->IsCurrent () */
#if 0
    case WM_SIZE:

      if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
      {
        RecreateSwapChains = true;
        UINT swap_flags = 0x0;

        if (SKIF_bCanFlip)
        {
          if (SKIF_bCanWaitSwapchain) // Note: IDXGISwapChain::ResizeBuffers can't be used to add or remove this flag. 
            swap_flags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

          if (SKIF_bCanAllowTearing)     //  Note: IDXGISwapChain::ResizeBuffers can't be used to add or remove this flag.
            swap_flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        }

        CleanupRenderTarget ();
        g_pSwapChain->ResizeBuffers (
          0, (UINT)LOWORD (lParam),
             (UINT)HIWORD (lParam),
            DXGI_FORMAT_UNKNOWN,
            swap_flags
        );
        CreateRenderTarget ();
      }
      return 0;
#endif

    case WM_SYSCOMMAND:
      if ((wParam & 0xfff0) == SC_KEYMENU)
      {
        // Disable ALT application menu
        if ( lParam == 0x00 ||
             lParam == 0x20 )
        {
          return 0;
        }
      }

      else if ((wParam & 0xfff0) == SC_MOVE)
      {
        // Disables the native move modal loop of Windows and
        // use the RepositionSKIF approach to move the window
        // to the center of the display the cursor is on.
        PostMessage (hWnd, WM_SKIF_RESTORE, 0x0, 0x0);
        return 0;
      }
      break;

    case WM_CLOSE:
      // Already handled in ImGui_ImplWin32_WndProcHandler
      break;

    case WM_DESTROY:
      ::PostQuitMessage (0);
      return 0;
  }

  return
    ::DefWindowProc (hWnd, msg, wParam, lParam);
}

LRESULT
WINAPI
SKIF_Notify_WndProc (HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  // This is the message procedure for the notification icon
  
  //OutputDebugString((L"[SKIF_Notify_WndProc] Message spotted: 0x" + std::format(L"{:x}", msg)    + L" (" + std::to_wstring(msg)    + L")" + (msg == WM_SETFOCUS ? L" == WM_SETFOCUS" : msg == WM_KILLFOCUS ? L" == WM_KILLFOCUS" : L"") + L"\n").c_str());
  //OutputDebugString((L"[SKIF_Notify_WndProc]          wParam: 0x" + std::format(L"{:x}", wParam) + L" (" + std::to_wstring(wParam) + L")" + ((HWND)wParam == NULL ? L" == NULL" : (HWND)wParam == SKIF_hWnd ? L" == SKIF_hWnd" : (HWND)wParam == SKIF_ImGui_hWnd ? L" == SKIF_ImGui_hWnd" : (HWND)wParam == SKIF_Notify_hWnd ? L" == SKIF_Notify_hWnd" : L"") + L"\n").c_str());
  //OutputDebugString((L"[SKIF_Notify_WndProc] Message spotted: 0x" + std::format(L"{:x}", msg)    + L" (" + std::to_wstring(msg)    + L")\n").c_str());
  //OutputDebugString((L"[SKIF_Notify_WndProc]          wParam: 0x" + std::format(L"{:x}", wParam) + L" (" + std::to_wstring(wParam) + L")\n").c_str());
  //OutputDebugString((L"[SKIF_Notify_WndProc]          wParam: 0x" + std::format(L"{:x}", wParam) + L" (" + std::to_wstring(wParam) + L") " + ((HWND)wParam == SKIF_hWnd ? L"== SKIF_hWnd" : ((HWND)wParam == SKIF_ImGui_hWnd ? L"== SKIF_ImGui_hWnd" : (HWND)wParam == SKIF_Notify_hWnd ? L"== SKIF_Notify_hWnd" : L"")) + L"\n").c_str());
  //OutputDebugString((L"[SKIF_Notify_WndProc]          lParam: 0x" + std::format(L"{:x}", lParam) + L" (" + std::to_wstring(lParam) + L")\n").c_str());

  switch (msg)
  {
    case WM_SKIF_NOTIFY_ICON:
      msgDontRedraw = true; // Don't redraw the main window when we're interacting with the notification icon
      switch (lParam)
      {
        case WM_LBUTTONDBLCLK:
        case WM_LBUTTONUP:
          PostMessage (SKIF_hWnd, WM_SKIF_RESTORE, 0x0, 0x0);
          return 0;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
          // Get current mouse position.
          POINT curPoint;
          GetCursorPos (&curPoint);

          // To display a context menu for a notification icon, the current window must be the foreground window
          // before the application calls TrackPopupMenu or TrackPopupMenuEx. Otherwise, the menu will not disappear
          // when the user clicks outside of the menu or the window that created the menu (if it is visible).
          SetForegroundWindow (hWnd);

          // TrackPopupMenu blocks the app until TrackPopupMenu returns
          TrackPopupMenu (
            hMenu,
            TPM_RIGHTBUTTON,
            curPoint.x,
            curPoint.y,
            0,
            hWnd,
            NULL
          );

          // However, when the current window is the foreground window, the second time this menu is displayed,
          // it appears and then immediately disappears. To correct this, you must force a task switch to the
          // application that called TrackPopupMenu. This is done by posting a benign message to the window or
          // thread, as shown in the following code sample:
          PostMessage (hWnd, WM_NULL, 0, 0);
          return 0;
        case NIN_BALLOONHIDE:
        case NIN_BALLOONSHOW:
        case NIN_BALLOONTIMEOUT:
        case NIN_BALLOONUSERCLICK:
        case NIN_POPUPCLOSE:
        case NIN_POPUPOPEN:
          break;
      }
      break;

    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case SKIF_NOTIFY_START:
          PostMessage (SKIF_hWnd, WM_SKIF_START, 0, 0);
          break;
        case SKIF_NOTIFY_STARTWITHSTOP:
          PostMessage (SKIF_hWnd, WM_SKIF_TEMPSTART, 0, 0);
          break;
        case SKIF_NOTIFY_STOP:
          PostMessage (SKIF_hWnd, WM_SKIF_STOP, 0, 0);
          break;
        case SKIF_NOTIFY_EXIT:
          PostMessage (SKIF_hWnd, WM_CLOSE, 0, 0);
          break;
      }
      break;
  }
  return
    ::DefWindowProc (hWnd, msg, wParam, lParam);
}