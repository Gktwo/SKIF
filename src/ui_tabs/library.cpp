//
// Copyright 2020 Andon "Kaldaien" Coleman
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

#include <wmsdk.h>
#include <filesystem>
#include <SKIF.h>
#include <SKIF_utility.h>
#include <SKIF_imgui.h>

#include <injection.h>

#include "../DirectXTex/DirectXTex.h"

#include <font_awesome.h>

#include <stores/Steam/apps_list.h>
#include <stores/Steam/asset_fetch.h>
#include <stores/Steam/apps_ignore.h>
#include <stores/GOG/gog_library.h>
#include <stores/EGS/epic_library.h>
#include <stores/Xbox/xbox_library.h>
#include <stores/SKIF/custom_library.h>

#include <cwctype>
#include <regex>
#include <iostream>
#include <locale>
#include <codecvt>
#include <fstream>
#include <filesystem>
#include <string>
#include <sstream>
#include <concurrent_queue.h>
#include <fsutil.h>
#include <atlimage.h>
#include <TlHelp32.h>
#include <gsl/gsl_util>

#include <patreon.png.h>
#include <sk_icon.jpg.h>
#include <sk_boxart.png.h>

#include <games.h>

// Registry Settings
#include <registry.h>

static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance( );

const int   SKIF_STEAM_APPID        = 1157970;
bool        SKIF_STEAM_OWNER        = false;
static bool clickedGameLaunch,
            clickedGameLaunchWoSK,
            clickedGalaxyLaunch,
            clickedGalaxyLaunchWoSK = false,
            openedGameContextMenu   = false;
const float fTintMin                = 0.75f;
      float fTint                   = 1.0f;
      float fAlpha                  = 0.0f;

PopupState IconMenu        = PopupState::Closed;
PopupState ServiceMenu     = PopupState::Closed;

PopupState AddGamePopup    = PopupState::Closed;
PopupState RemoveGamePopup = PopupState::Closed;
PopupState ModifyGamePopup = PopupState::Closed;
PopupState ConfirmPopup    = PopupState::Closed;

std::string confirmPopupTitle;
std::string confirmPopupText;

extern bool            SKIF_bSuppressServiceNotification;
extern HWND            SKIF_hWnd;
extern float           SKIF_ImGui_GlobalDPIScale;
extern float           SKIF_ImGui_GlobalDPIScale_Last;
extern std::string     SKIF_StatusBarHelp;
extern std::string     SKIF_StatusBarText;
extern std::wstring    SKIF_EGS_AppDataPath;

extern bool            SKIF_ImGui_BeginChildFrame (ImGuiID id, const ImVec2& size, ImGuiWindowFlags extra_flags = 0);
extern CComPtr <ID3D11Device> SKIF_D3D11_GetDevice (bool bWait = true);

static std::wstring sshot_file = L"";

std::atomic<int> textureLoadQueueLength{ 1 };

int getTextureLoadQueuePos (void) {
  return textureLoadQueueLength.fetch_add(1) + 1;
}

CComPtr <ID3D11Texture2D>          pPatTex2D;
CComPtr <ID3D11ShaderResourceView> pPatTexSRV;

enum class LibraryTexture
{
  Icon,
  Cover,
  Patreon
};

void
LoadLibraryTexture (
        LibraryTexture                      libTexToLoad,
        uint32_t                            appid,
        CComPtr <ID3D11ShaderResourceView>& pLibTexSRV,
        const std::wstring&                 name,
        ImVec2&                             vCoverUv0,
        ImVec2&                             vCoverUv1,
        app_record_s*                       pApp = nullptr)
{
  CComPtr <ID3D11Texture2D> pTex2D;
  DirectX::TexMetadata        meta = { };
  DirectX::ScratchImage        img = { };

  std::wstring load_str = L"\0",
               SKIFCustomPath,
               SteamCustomPath;

  bool succeeded   = false;
  bool customAsset = false;

  if (pApp != nullptr)
    appid = pApp->id;
  
  //if (libTexToLoad == LibraryTexture::Cover)
  //  OutputDebugString((L"[App#" + std::to_wstring(appid) + L"] Attempting to load library texture...\n").c_str());

  // SKIF
  if (       appid == SKIF_STEAM_APPID &&
      libTexToLoad != LibraryTexture::Patreon)
  {
    SKIFCustomPath = SK_FormatStringW (LR"(%ws\Assets\)", path_cache.specialk_userdata);

    if (libTexToLoad == LibraryTexture::Cover)
      SKIFCustomPath += L"cover";
    else
      SKIFCustomPath += L"icon";

    if      (PathFileExistsW ((SKIFCustomPath + L".png").c_str()))
      load_str =               SKIFCustomPath + L".png";
    else if (PathFileExistsW ((SKIFCustomPath + L".jpg").c_str()))
      load_str =               SKIFCustomPath + L".jpg";
    else if (libTexToLoad == LibraryTexture::Icon &&
             PathFileExistsW ((SKIFCustomPath + L".ico").c_str()))
      load_str =               SKIFCustomPath + L".ico";

    customAsset = (load_str != L"\0");
  }

  // SKIF Custom
  else if (pApp != nullptr && pApp->store == "SKIF")
  {
    SKIFCustomPath = SK_FormatStringW (LR"(%ws\Assets\Custom\%i\)", path_cache.specialk_userdata, appid);

    if (libTexToLoad == LibraryTexture::Cover)
      SKIFCustomPath += L"cover";
    else
      SKIFCustomPath += L"icon";

    if      (PathFileExistsW    ((SKIFCustomPath + L".png").c_str()))
      load_str =                  SKIFCustomPath + L".png";
    else if (PathFileExistsW    ((SKIFCustomPath + L".jpg").c_str()))
      load_str =                  SKIFCustomPath + L".jpg";
    else if (libTexToLoad == LibraryTexture::Icon &&
             PathFileExistsW    ((SKIFCustomPath + L".ico").c_str()))
      load_str =                  SKIFCustomPath + L".ico";

    customAsset = (load_str != L"\0");

    if (! customAsset)
    {
      if      (libTexToLoad == LibraryTexture::Icon &&
               SKIF_Util_SaveExtractExeIcon (pApp->launch_configs[0].getExecutableFullPath(pApp->id), SKIFCustomPath + L"-original.png"))
        load_str =                SKIFCustomPath + L"-original.png";
    }
  }

  // EGS
  else if (pApp != nullptr && pApp->store == "EGS")
  {
    std::wstring EGSAssetPath = SK_FormatStringW(LR"(%ws\Assets\EGS\%ws\)", path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->EGS_AppName).c_str());
    SKIFCustomPath = std::wstring(EGSAssetPath);

    if (libTexToLoad == LibraryTexture::Cover)
      SKIFCustomPath += L"cover";
    else
      SKIFCustomPath += L"icon";

    if      (PathFileExistsW  ((SKIFCustomPath + L".png").c_str()))
      load_str =                SKIFCustomPath + L".png";
    else if (PathFileExistsW  ((SKIFCustomPath + L".jpg").c_str()))
      load_str =                SKIFCustomPath + L".jpg";
    else if (libTexToLoad == LibraryTexture::Icon &&
             PathFileExistsW  ((SKIFCustomPath + L".ico").c_str()))
      load_str =                SKIFCustomPath + L".ico";

    customAsset = (load_str != L"\0");

    if (! customAsset)
    {
      if      (libTexToLoad == LibraryTexture::Cover &&
               PathFileExistsW ((EGSAssetPath + L"OfferImageTall.jpg").c_str()))
        load_str =               EGSAssetPath + L"OfferImageTall.jpg";
      else if (libTexToLoad == LibraryTexture::Icon &&
               SKIF_Util_SaveExtractExeIcon (pApp->launch_configs[0].getExecutableFullPath(pApp->id), EGSAssetPath + L"icon-original.png"))
        load_str =               SKIFCustomPath + L"-original.png";
    }
  }

  // GOG
  else if (pApp != nullptr && pApp->store == "GOG")
  {
    SKIFCustomPath = SK_FormatStringW (LR"(%ws\Assets\GOG\%i\)", path_cache.specialk_userdata, appid);

    if (libTexToLoad == LibraryTexture::Cover)
      SKIFCustomPath += L"cover";
    else
      SKIFCustomPath += L"icon";

    if      (PathFileExistsW ((SKIFCustomPath + L".png").c_str()))
      load_str =               SKIFCustomPath + L".png";
    else if (PathFileExistsW ((SKIFCustomPath + L".jpg").c_str()))
      load_str =               SKIFCustomPath + L".jpg";
    else if (libTexToLoad == LibraryTexture::Icon &&
             PathFileExistsW ((SKIFCustomPath + L".ico").c_str()))
      load_str =               SKIFCustomPath + L".ico";

    customAsset = (load_str != L"\0");

    if (! customAsset)
    {
      if      (libTexToLoad == LibraryTexture::Icon &&
               SKIF_Util_SaveExtractExeIcon (pApp->launch_configs[0].getExecutableFullPath(pApp->id), SKIFCustomPath + L"-original.png"))
        load_str =             SKIFCustomPath + L"-original.png";
      else if (libTexToLoad == LibraryTexture::Icon)
      {
        load_str =             name;
      }

      else if (libTexToLoad == LibraryTexture::Cover)
      {
        extern std::wstring GOGGalaxy_UserID;
        load_str = SK_FormatStringW (LR"(C:\ProgramData\GOG.com\Galaxy\webcache\%ws\gog\%i\)", GOGGalaxy_UserID.c_str(), appid);

        HANDLE hFind = INVALID_HANDLE_VALUE;
        WIN32_FIND_DATA ffd;

        hFind = FindFirstFile((load_str + name).c_str(), &ffd);

        if (INVALID_HANDLE_VALUE != hFind)
        {
          load_str += ffd.cFileName;
          FindClose(hFind);
        }
      }
    }
  }

  // Xbox
  else if (pApp != nullptr && pApp->store == "Xbox")
  {
    std::wstring XboxAssetPath = SK_FormatStringW(LR"(%ws\Assets\Xbox\%ws\)", path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->Xbox_PackageName).c_str());
    SKIFCustomPath = std::wstring(XboxAssetPath);

    if (libTexToLoad == LibraryTexture::Cover)
      SKIFCustomPath += L"cover";
    else
      SKIFCustomPath += L"icon";

    if      (PathFileExistsW  ((SKIFCustomPath + L".png").c_str()))
      load_str =                SKIFCustomPath + L".png";
    else if (PathFileExistsW  ((SKIFCustomPath + L".jpg").c_str()))
      load_str =                SKIFCustomPath + L".jpg";
    else if (libTexToLoad == LibraryTexture::Icon &&
             PathFileExistsW  ((SKIFCustomPath + L".ico").c_str()))
      load_str =                SKIFCustomPath + L".ico";

    customAsset = (load_str != L"\0");

    if (! customAsset)
    {
      if      (libTexToLoad == LibraryTexture::Cover &&
               PathFileExistsW ((SKIFCustomPath + L"-original.png").c_str()))
        load_str =               SKIFCustomPath + L"-original.png";
      else if (libTexToLoad == LibraryTexture::Cover &&
               PathFileExistsW ((SKIFCustomPath + L"-fallback.png").c_str()))
        load_str =               SKIFCustomPath + L"-fallback.png";
      else if (libTexToLoad == LibraryTexture::Icon &&
               PathFileExistsW ((SKIFCustomPath + L"-original.png").c_str()))
        load_str =               SKIFCustomPath + L"-original.png";
    }
  }

  // STEAM
  else if (pApp != nullptr && pApp->store == "Steam")
  {
    static unsigned long SteamUserID = 0;

    if (SteamUserID == 0)
    {
      WCHAR                    szData [255] = { };
      DWORD   dwSize = sizeof (szData);
      PVOID   pvData =         szData;
      CRegKey hKey ((HKEY)0);

      if (RegOpenKeyExW (HKEY_CURRENT_USER, LR"(SOFTWARE\Valve\Steam\ActiveProcess\)", 0, KEY_READ, &hKey.m_hKey) == ERROR_SUCCESS)
      {
        if (RegGetValueW (hKey, NULL, L"ActiveUser", RRF_RT_REG_DWORD, NULL, pvData, &dwSize) == ERROR_SUCCESS)
          SteamUserID = *(DWORD*)pvData;
      }
    }

    SKIFCustomPath  = SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)", path_cache.specialk_userdata, appid);
    SteamCustomPath = SK_FormatStringW (LR"(%ws\userdata\%i\config\grid\%i)", SK_GetSteamDir(), SteamUserID, appid);

    if (libTexToLoad == LibraryTexture::Cover)
      SKIFCustomPath += L"cover";
    else
      SKIFCustomPath += L"icon";

    if      (PathFileExistsW (( SKIFCustomPath +  L".png").c_str()))
      load_str =                SKIFCustomPath +  L".png";
    else if (PathFileExistsW (( SKIFCustomPath +  L".jpg").c_str()))
      load_str =                SKIFCustomPath +  L".jpg";
    else if (libTexToLoad == LibraryTexture::Icon  &&
             PathFileExistsW (( SKIFCustomPath +  L".ico").c_str()))
      load_str =                SKIFCustomPath +  L".ico";

    customAsset = (load_str != L"\0");

    if (! customAsset)
    {
      if      (libTexToLoad == LibraryTexture::Cover &&
               PathFileExistsW ((SteamCustomPath + L"p.png").c_str()))
        load_str =               SteamCustomPath + L"p.png";
      else if (libTexToLoad == LibraryTexture::Cover &&
               PathFileExistsW ((SteamCustomPath + L"p.jpg").c_str()))
        load_str =               SteamCustomPath + L"p.jpg";
      else
        load_str = name;
        //load_str = SK_FormatStringW(LR"(%ws\appcache\librarycache\%i%ws)", SK_GetSteamDir(), appid, name.c_str());
    }
  }

  PLOG_DEBUG << "Texture to load: " << load_str;

  if (pApp != nullptr)
  {
    if      (libTexToLoad == LibraryTexture::Cover)
      pApp->textures.isCustomCover = customAsset;
    else if (libTexToLoad == LibraryTexture::Icon)
      pApp->textures.isCustomIcon  = customAsset;
  }

  if (load_str != L"\0" &&
      SUCCEEDED(
        DirectX::LoadFromWICFile (
          load_str.c_str (),
            DirectX::WIC_FLAGS_FILTER_POINT | DirectX::WIC_FLAGS_IGNORE_SRGB, // WIC_FLAGS_IGNORE_SRGB solves some PNGs appearing too dark
              &meta, img
        )
      )
    )
  {
    succeeded = true;
  }

  else if (appid == SKIF_STEAM_APPID)
  {
    if (SUCCEEDED(
          DirectX::LoadFromWICMemory(
            (libTexToLoad == LibraryTexture::Icon) ?        sk_icon_jpg  : (libTexToLoad == LibraryTexture::Cover) ?        sk_boxart_png  :        patreon_png,
            (libTexToLoad == LibraryTexture::Icon) ? sizeof(sk_icon_jpg) : (libTexToLoad == LibraryTexture::Cover) ? sizeof(sk_boxart_png) : sizeof(patreon_png),
              DirectX::WIC_FLAGS_FILTER_POINT,
                &meta, img
          )
        )
      )
    {
      succeeded = true;
    }
  }

  // Push the existing texture to a stack to be released after the frame
  if (pLibTexSRV.p != nullptr)
  {
    extern concurrency::concurrent_queue <CComPtr <IUnknown>> SKIF_ResourcesToFree;
    PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << pLibTexSRV.p << " to be released";;
    SKIF_ResourcesToFree.push (pLibTexSRV.p);
    pLibTexSRV.p = nullptr;
  }

  if (succeeded)
  {
    //if (libTexToLoad == LibraryTexture::Cover)
    //  OutputDebugString((L"[App#" + std::to_wstring(appid) + L"] Loading the source image succeeded...\n").c_str());

    DirectX::ScratchImage* pImg   =
                                &img;
    DirectX::ScratchImage   converted_img;

    // Start aspect ratio
    vCoverUv0 = ImVec2(0.f, 0.f); // Top left corner
    vCoverUv1 = ImVec2(1.f, 1.f); // Bottom right corner
    ImVec2 vecTex2D = ImVec2(600.f, 900.f);

    vecTex2D.x = static_cast<float>(meta.width);
    vecTex2D.y = static_cast<float>(meta.height);

    ImVec2 diff = ImVec2(0.0f, 0.0f);

    // Crop wider aspect ratios by their width
    if ((vecTex2D.x / vecTex2D.y) > (600.f / 900.f))
    {
      float newWidth = vecTex2D.x / vecTex2D.y * 900.0f;
      diff.x = (600.0f / newWidth);
      diff.x -= 1.0f;
      diff.x /= 2;

      vCoverUv0.x = 0.f - diff.x;
      vCoverUv1.x = 1.f + diff.x;
      //vCoverUv0.y = 1.f;
      //vCoverUv1.y = 1.f;
    }

    // Crop thinner aspect ratios by their height
    else if ((vecTex2D.x / vecTex2D.y) < (600.f / 900.f))
    {
      float newHeight = vecTex2D.y / vecTex2D.x * 600.0f;
      diff.y = (900.0f / newHeight);
      diff.y -= 1.0f;
      diff.y /= 2;
      
      //vCoverUv0.x = 1.f;
      //vCoverUv1.x = 1.f;
      vCoverUv0.y = 0.f - diff.y;
      vCoverUv1.y = 1.f + diff.y;
    }

    // End aspect ratio

    // We don't want single-channel icons, so convert to RGBA
    if (meta.format == DXGI_FORMAT_R8_UNORM)
    {
      if (
        SUCCEEDED (
          DirectX::Convert (
            pImg->GetImages   (), pImg->GetImageCount (),
            pImg->GetMetadata (), DXGI_FORMAT_R8G8B8A8_UNORM,
              DirectX::TEX_FILTER_DEFAULT,
              DirectX::TEX_THRESHOLD_DEFAULT,
                converted_img
          )
        )
      ) { meta =  converted_img.GetMetadata ();
          pImg = &converted_img; }
    }

    auto pDevice =
      SKIF_D3D11_GetDevice ();

    if (! pDevice)
      return;

    pTex2D = nullptr;

    if (
      SUCCEEDED (
        DirectX::CreateTexture (
          pDevice,
            pImg->GetImages (), pImg->GetImageCount (),
              meta, (ID3D11Resource **)&pTex2D.p
        )
      )
    )
    {
      D3D11_SHADER_RESOURCE_VIEW_DESC
        srv_desc                           = { };
        srv_desc.Format                    = DXGI_FORMAT_UNKNOWN;
        srv_desc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels       = UINT_MAX;
        srv_desc.Texture2D.MostDetailedMip =  0;

        //CComPtr <ID3D11ShaderResourceView>
        //    pOrigTexSRV (pLibTexSRV.p);
                         //pLibTexSRV = nullptr; // Crashes on Intel

        //if (libTexToLoad == LibraryTexture::Cover)
        //  OutputDebugString((L"[App#" + std::to_wstring(appid) + L"] Game cover loaded\n").c_str());

      if (    pTex2D.p == nullptr ||
        FAILED (
          pDevice->CreateShaderResourceView (
              pTex2D.p, &srv_desc,
            &pLibTexSRV.p
          )
        )
      )
      {
        //pLibTexSRV = pOrigTexSRV;
      }

      // SRV is holding a reference, this is not needed anymore.
      pTex2D = nullptr;
    }
  }
};

using app_entry_t =
        std::pair < std::string,
                    app_record_s >;
using   app_ptr_t = app_entry_t const*;


// define character size
#define CHAR_SIZE 128

#pragma region Trie Keyboard Hint Search

// A Class representing a Trie node
class Trie
{
public:
  bool  isLeaf                = false;
  Trie* character [CHAR_SIZE] = {   };

  // Constructor
  Trie (void)
  {
    this->isLeaf = false;

    for (int i = 0; i < CHAR_SIZE; i++)
      this->character [i] = nullptr;
  }

  void insert       (        const std::string&);
  bool deletion     (Trie*&, const std::string&);
  bool search       (        const std::string&);
  bool haveChildren (Trie const*);
};

// Iterative function to insert a key in the Trie
void
Trie::insert (const std::string& key)
{
  // start from root node
  Trie* curr = this;
  for (size_t i = 0; i < key.length (); i++)
  {
    // create a new node if path doesn't exists
    if (curr->character [key [i]] == nullptr)
        curr->character [key [i]]  = new Trie ();

    // go to next node
    curr = curr->character [key [i]];
  }

  // mark current node as leaf
  curr->isLeaf = true;
}

// Iterative function to search a key in Trie. It returns true
// if the key is found in the Trie, else it returns false
bool Trie::search (const std::string& key)
{
  Trie* curr = this;
  for (size_t i = 0; i < key.length (); i++)
  {
    // go to next node
    curr = curr->character [key [i]];

    // if string is invalid (reached end of path in Trie)
    if (curr == nullptr)
      return false;
  }

  // if current node is a leaf and we have reached the
  // end of the string, return true
  return curr->isLeaf;
}

// returns true if given node has any children
bool Trie::haveChildren (Trie const* curr)
{
  for (int i = 0; i < CHAR_SIZE; i++)
    if (curr->character [i])
      return true;  // child found

  return false;
}

// Recursive function to delete a key in the Trie
bool Trie::deletion (Trie*& curr, const std::string& key)
{
  // return if Trie is empty
  if (curr == nullptr)
    return false;

  // if we have not reached the end of the key
  if (key.length ())
  {
    // recur for the node corresponding to next character in the key
    // and if it returns true, delete current node (if it is non-leaf)

    if (        curr                      != nullptr       &&
                curr->character [key [0]] != nullptr       &&
      deletion (curr->character [key [0]], key.substr (1)) &&
                curr->isLeaf == false)
    {
      if (! haveChildren (curr))
      {
        delete curr;
        curr = nullptr;
        return true;
      }

      else {
        return false;
      }
    }
  }

  // if we have reached the end of the key
  if (key.length () == 0 && curr->isLeaf)
  {
    // if current node is a leaf node and don't have any children
    if (! haveChildren (curr))
    {
      // delete current node
      delete curr;
      curr = nullptr;

      // delete non-leaf parent nodes
      return true;
    }

    // if current node is a leaf node and have children
    else
    {
      // mark current node as non-leaf node (DON'T DELETE IT)
      curr->isLeaf = false;

      // don't delete its parent nodes
      return false;
    }
  }

  return false;
}

struct {
  uint32_t    id = 0;
  std::string store;
} static manual_selection;

Trie labels;

#pragma endregion


// Temporarily disabled since this gets triggered on game launch/shutdown as well...
bool Steam_isLibrariesSignaled (void)
{
#define MAX_STEAM_LIBRARIES 16

  if ( _registry.bDisableSteamLibrary )
    return false;

  bool isSignaled = false;

  steam_library_t* steam_lib_paths = nullptr;
  int              steam_libs      = SK_Steam_GetLibraries (&steam_lib_paths);
  static SKIF_DirectoryWatch steam_libs_watch[MAX_STEAM_LIBRARIES];
  static int                 steam_libs_files[MAX_STEAM_LIBRARIES] = { 0 };
  static bool                hasInitialized = false;

  if (! steam_lib_paths)
    return false;

  if (steam_libs != 0)
  {
    for (int i = 0; i < steam_libs; i++)
    {
      wchar_t    wszManifestDir [MAX_PATH + 2] = { };
      swprintf ( wszManifestDir, MAX_PATH + 2,
                    LR"(%s\steamapps)",
                (wchar_t *)steam_lib_paths [i] );

      bool countFiles = false;

      if (steam_libs_watch[i]._hChangeNotification == INVALID_HANDLE_VALUE)
        steam_libs_watch[i] = SKIF_DirectoryWatch (wszManifestDir, true);

      if (steam_libs_watch[i].isSignaled (wszManifestDir, true))
        countFiles = true;

      if (countFiles || ! hasInitialized)
      {
        int prevCount = steam_libs_files[i];
        int currCount = 0;

        std::error_code dirError;
        std::filesystem::directory_iterator iterator = 
          std::filesystem::directory_iterator (wszManifestDir, dirError);

        // Only iterate over the files if the directory exists and is accessible
        if (! dirError)
        {
          for (auto& directory_entry : iterator)
            if (directory_entry.is_regular_file())
              currCount++;

          steam_libs_files[i] = currCount;
        }

        if (hasInitialized && prevCount != currCount)
          isSignaled = true;
      }
    }
  }

  hasInitialized = true;

  return isSignaled;
};

// TODO: Move this into, like, steam_library.cpp or something
std::vector <std::pair <std::string, app_record_s>>
Steam_GetInstalledAppIDs (void)
{
  std::vector <std::pair <std::string, app_record_s>> ret;

  if ( _registry.bDisableSteamLibrary )
    return ret;

  std::set <uint32_t> unique_apps;

  for ( auto app : SK_Steam_GetInstalledAppIDs ( ))
  {
    // Skip Steamworks Common Redists
    if (app == 228980) continue;

    // Skip IDs related to apps, DLCs, music, and tools (including Special K for now)
    if (std::find(std::begin(steam_apps_ignorable), std::end(steam_apps_ignorable), app) != std::end(steam_apps_ignorable)) continue;

    if (unique_apps.emplace (app).second)
    {
      // Opening the manifests to read the names is a
      //   lengthy operation, so defer names and icons
      ret.emplace_back (
        "Loading...", app
      );
    }
  }

  return ret;
};

void
SKIF_UI_Tab_DrawLibrary (void)
{
  SKIF_GamesCollection& _games = SKIF_GamesCollection::GetInstance();

  // Always read from the last written index
  int nowReading = _games.snapshot_idx_written.load ( );
  _games.snapshot_idx_reading.store (nowReading);

  if (RepopulateGames)
    _games.RefreshGames ( );

  /*
  std::vector <std::unique_ptr<app_generic_s>> &apps_new =
    _games.GetGames ( );
  */

  std::vector <std::unique_ptr<app_generic_s>>* apps_new = _games.GetGames ( );

  if (apps_new != nullptr && ! apps_new->empty() && RepopulateGames)
  {
    for (auto const& app : *apps_new) {
      //OutputDebugString(SK_UTF8ToWideChar(app->names.normal).c_str());
      //OutputDebugString(L"\n");
    }
  }

  /*
  if (! sshot_file.empty ())
  {
    SKIF_GameManagement_ShowScreenshot (sshot_file);
  }
  */

  static CComPtr <ID3D11Texture2D>          pTex2D;
  static CComPtr <ID3D11ShaderResourceView> pTexSRV;
  //static ImVec2                             vecTex2D;

  static ImVec2 vecCoverUv0 = ImVec2 (0, 0), 
                vecCoverUv1 = ImVec2 (1, 1);

  static DirectX::TexMetadata     meta = { };
  static DirectX::ScratchImage    img  = { };

  static
    std::wstring appinfo_path (
      SK_GetSteamDir ()
    );

  SK_RunOnce (
    appinfo_path.append (
      LR"(\appcache\appinfo.vdf)"
    )
  );

  SK_RunOnce (
    appinfo =
      std::make_unique <skValveDataFile> (
        appinfo_path
      )
  );

  auto& io =
    ImGui::GetIO ();

  static volatile LONG icon_thread  = 1;
  static volatile LONG need_sort    = 0;
  bool                 sort_changed = false;

  if (InterlockedCompareExchange (&need_sort, 0, 1))
  {
    std::sort ( apps.begin (),
                apps.end   (),
      []( const app_entry_t& a,
          const app_entry_t& b ) -> int
      {
        return a.second.names.all_upper.compare(
               b.second.names.all_upper
        ) < 0;
      }
    );

    sort_changed = true;
  }

  static bool     update         = true;
  static bool     updateInjStrat = false;
  static bool     loadCover      = false;
  static uint32_t lastCover      = 0;

  struct {
    uint32_t    appid = SKIF_STEAM_APPID;
    std::string store = "Steam";

    void reset()
    {
      appid = SKIF_STEAM_APPID;
      store = "Steam";
    }
  } static selection;

  static bool     populated      = false;
  
  static SKIF_DirectoryWatch SKIF_EGS_ManifestWatch;

  if (! ImGui::IsAnyMouseDown ( ) || ! SKIF_ImGui_IsFocused ( ))
  {
    // Temporarily disabled since this gets triggered on game launch/shutdown as well...
    //if (Steam_isLibrariesSignaled ())
    //  RepopulateGames = true;

    if (! _registry.bDisableEGSLibrary  && SKIF_EGS_ManifestWatch.isSignaled (SKIF_EGS_AppDataPath, true))
      RepopulateGames = true;

    if (! _registry.bDisableXboxLibrary && SKIF_Xbox_hasInstalledGamesChanged ( ))
      RepopulateGames = true;
  }

  if (RepopulateGames)
  {
    RepopulateGames = false;

    // Clear cached lists
    apps.clear   ();

    // Reset selection to Special K, but only if set to something else than -1
    if (selection.appid != 0)
      selection.reset();

    update    = true;

    populated = false;
  }

  if (! populated)
  {
    InterlockedExchange (&icon_thread, 1);

    PLOG_INFO << "Populating library list...";

    apps      = Steam_GetInstalledAppIDs ();

    for (auto& app : apps)
      if (app.second.id == SKIF_STEAM_APPID)
        SKIF_STEAM_OWNER = true;

    if ( ! SKIF_STEAM_OWNER )
    {
      app_record_s SKIF_record (SKIF_STEAM_APPID);

      SKIF_record.id              = SKIF_STEAM_APPID;
      SKIF_record.names.normal    = "Special K";
      SKIF_record.names.all_upper = "SPECIAL K";
      SKIF_record.install_dir     = path_cache.specialk_install;
      SKIF_record.store           = "Steam";
      SKIF_record.ImGuiLabelAndID = SK_FormatString("%s###%s%i", SKIF_record.names.normal.c_str(), SKIF_record.store.c_str(), SKIF_record.id);

      std::pair <std::string, app_record_s>
        SKIF ( "Special K", SKIF_record );

      apps.emplace_back (SKIF);
    }

    // Load GOG titles from registry
    if (! _registry.bDisableGOGLibrary)
      SKIF_GOG_GetInstalledAppIDs (&apps);

    // Load EGS titles from disk
    if (! _registry.bDisableEGSLibrary)
      SKIF_EGS_GetInstalledAppIDs (&apps);
    
    if (! _registry.bDisableXboxLibrary)
      SKIF_Xbox_GetInstalledAppIDs (&apps);

    // Load custom SKIF titles from registry
    SKIF_GetCustomAppIDs (&apps);

    // Set to last selected if it can be found
    if (selection.appid == SKIF_STEAM_APPID)
    {
      for (auto& app : apps)
      {
        if (app.second.id    ==                    _registry.iLastSelectedGame  &&
            app.second.store == SK_WideCharToUTF8 (_registry.wsLastSelectedStore))
        {
          selection.appid = app.second.id;
          selection.store = app.second.store;
          update = true;
        }
      }
    }

    PLOG_INFO << "Finished populating the library list.";

    // We're going to stream icons in asynchronously on this thread
    _beginthread ([](void*)->void
    {
      SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_LibRefreshWorker");

      CoInitializeEx (nullptr, 0x0);

      PLOG_INFO << "Thread started!";
      PLOG_INFO << "Streaming game icons and names asynchronously...";

      ImVec2 dontCare1, dontCare2;
      SK_RunOnce (
        LoadLibraryTexture (LibraryTexture::Patreon, SKIF_STEAM_APPID, pPatTexSRV, L"(patreon.png)", dontCare1, dontCare2)
      );

      PLOG_INFO << "Loaded the embedded Patreon texture.";

      for ( auto& app : apps )
      {
        PLOG_DEBUG << "Working on " << app.second.id << " (" << app.second.store << ")";

        // Special handling for non-Steam owners of Special K / SKIF
        if ( app.second.id == SKIF_STEAM_APPID )
          app.first = "Special K";

        // Regular handling for the remaining Steam games
        else if (app.second.store == "Steam") {
          app.first.clear ();

          app.second._status.refresh (&app.second);
        }

        // Only bother opening the application manifest
        //   and looking for a name if the client claims
        //     the app is installed.
        if (app.second._status.installed)
        {
          if (! app.second.names.normal.empty ())
          {
            app.first = app.second.names.normal;
          }

          // Some games have an install state but no name,
          //   for those we have to consult the app manifest
          else if (app.second.store == "Steam")
          {
            app.first =
              SK_UseManifestToGetAppName (
                           app.second.id );
          }

          // Corrupted app manifest / not known to Steam client; SKIP!
          if (app.first.empty ())
          {
            PLOG_DEBUG << "App ID " << app.second.id << " (" << app.second.store << ") has no name; ignoring!";

            app.second.id = 0;
            continue;
          }

          std::string original_name = app.first;

          // Some games use weird Unicode character combos that ImGui can't handle,
          //  so let's replace those with the normal ones.

          // Replace RIGHT SINGLE QUOTATION MARK (Code: 2019 | UTF-8: E2 80 99)
          //  with a APOSTROPHE (Code: 0027 | UTF-8: 27)
          app.first = std::regex_replace(app.first, std::regex("\xE2\x80\x99"), "\x27");

          // Replace LATIN SMALL LETTER O (Code: 006F | UTF-8: 6F) and COMBINING DIAERESIS (Code: 0308 | UTF-8: CC 88)
          //  with a LATIN SMALL LETTER O WITH DIAERESIS (Code: 00F6 | UTF-8: C3 B6)
          app.first = std::regex_replace(app.first, std::regex("\x6F\xCC\x88"), "\xC3\xB6");

          // Strip game names from special symbols (disabled due to breaking some Chinese characters)
          //const char* chars = (const char *)u8"\u00A9\u00AE\u2122"; // Copyright (c), Registered (R), Trademark (TM)
          //for (unsigned int i = 0; i < strlen(chars); ++i)
            //app.first.erase(std::remove(app.first.begin(), app.first.end(), chars[i]), app.first.end());

          // Remove COPYRIGHT SIGN (Code: 00A9 | UTF-8: C2 A9)
          app.first = std::regex_replace(app.first, std::regex("\xC2\xA9"), "");

          // Remove REGISTERED SIGN (Code: 00AE | UTF-8: C2 AE)
          app.first = std::regex_replace(app.first, std::regex("\xC2\xAE"), "");

          // Remove TRADE MARK SIGN (Code: 2122 | UTF-8: E2 84 A2)
          app.first = std::regex_replace(app.first, std::regex("\xE2\x84\xA2"), "");

          if (original_name != app.first)
          {
            PLOG_DEBUG << "Game title was changed:";
            PLOG_DEBUG << "Old: " << SK_UTF8ToWideChar(original_name.c_str()) << " (" << original_name << ")";
            PLOG_DEBUG << "New: " << SK_UTF8ToWideChar(app.first.c_str())     << " (" << app.first     << ")";
          }

          // Strip any remaining null terminators
          app.first.erase(std::find(app.first.begin(), app.first.end(), '\0'), app.first.end());

          // Trim leftover spaces
          app.first.erase(app.first.begin(), std::find_if(app.first.begin(), app.first.end(), [](unsigned char ch) { return !std::isspace(ch); }));
          app.first.erase(std::find_if(app.first.rbegin(), app.first.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), app.first.end());
          
          // Update ImGuiLabelAndID
          app.second.ImGuiLabelAndID = SK_FormatString("%s###%s%i", app.first.c_str(), app.second.store.c_str(), app.second.id);
        }

        // Check if install folder exists (but not for SKIF)
        if (app.second.id != SKIF_STEAM_APPID && app.second.store != "Xbox")
        {
          std::wstring install_dir;

          if (app.second.store == "Steam")
            install_dir = SK_UseManifestToGetInstallDir(app.second.id);
          else
            install_dir = app.second.install_dir;
          
          if (! PathFileExists(install_dir.c_str()))
          {
            PLOG_DEBUG << "App ID " << app.second.id << " (" << app.second.store << ") has non-existent install folder; ignoring!";

            app.second.id = 0;
            continue;
          }
        }

        if (app.second._status.installed || app.second.id == SKIF_STEAM_APPID)
        {
          std::string all_upper;

          for (const char c : app.first)
          {
            if (! ( isalnum (c) || isspace (c)))
              continue;

            all_upper += (char)toupper (c);
          }

          static const
            std::string toSkip [] =
            {
              std::string ("A "),
              std::string ("THE ")
            };

          for ( auto& skip_ : toSkip )
          {
            if (all_upper.find (skip_) == 0)
            {
              all_upper =
                all_upper.substr (
                  skip_.length ()
                );
              break;
            }
          }

          std::string trie_builder;

          for ( const char c : all_upper)
          {
            trie_builder += c;

            labels.insert (trie_builder);
          }

          app.second.names.all_upper = trie_builder;
          app.second.names.normal    = app.first;
        }

        std::wstring load_str;
        
        if (app.second.id == SKIF_STEAM_APPID) // SKIF
          load_str = L"_icon.jpg";
        else  if (app.second.store == "SKIF")  // SKIF Custom
          load_str = L"icon";
        else  if (app.second.store == "EGS")   // EGS
          load_str = L"icon";
        else  if (app.second.store == "GOG")   // GOG
          load_str = app.second.install_dir + L"\\goggame-" + std::to_wstring(app.second.id) + L".ico";
        else if (app.second.store == "Steam")  // STEAM
          load_str = SK_FormatStringW(LR"(%ws\appcache\librarycache\%i_icon.jpg)", SK_GetSteamDir(), app.second.id); //L"_icon.jpg"

        LoadLibraryTexture ( LibraryTexture::Icon,
                               app.second.id,
                                 app.second.textures.icon,
                                   load_str,
                                     dontCare1,
                                       dontCare2,
                                         &app.second );

        // UNUSED?
        /*
        static auto *pFont =
          ImGui::GetFont ();

        max_app_name_len =
          std::max ( max_app_name_len,
                        pFont->CalcTextSizeA (1.0f, FLT_MAX, 0.0f,
                          app.first.c_str (),
                StrStrA (app.first.c_str (), "##")
                        ).x
          );
          */

        PLOG_VERBOSE << "Finished with game!";
      }

      PLOG_INFO << "Finished streaming game icons and names asynchronously...";

      InterlockedExchange (&icon_thread, 0);
      InterlockedExchange (&need_sort, 1);

      PLOG_INFO << "Thread stopped!";
    }, 0x0, NULL);

    populated = true;
  }

  if (! update)
  {
    update =
      SKIF_LibraryAssets_CheckForUpdates (true);
  }

  extern bool  coverFadeActive;
  static int   tmp_iDimCovers = _registry.iDimCovers;
  
  static
    app_record_s* pApp = nullptr;

  for (auto& app : apps)
    if (app.second.id == selection.appid && app.second.store == selection.store)
      pApp = &app.second;

  // Apply changes when the selected game changes
  if (update)
  {
    fTint = (_registry.iDimCovers == 0) ? 1.0f : fTintMin;
  }

  // Apply changes when the _registry.iDimCovers var has been changed in the Settings tab
  else if (tmp_iDimCovers != _registry.iDimCovers)
  {
    fTint = (_registry.iDimCovers == 0) ? 1.0f : fTintMin;

    tmp_iDimCovers = _registry.iDimCovers;
  }

  ImGui::BeginGroup    (                                                  );
  float fX =
  ImGui::GetCursorPosX (                                                  );

  // Display cover image
  SKIF_ImGui_OptImage  (pTexSRV.p,
                                                    ImVec2 (600.0F * SKIF_ImGui_GlobalDPIScale,
                                                            900.0F * SKIF_ImGui_GlobalDPIScale),
                                                    vecCoverUv0, // Top Left coordinates
                                                    vecCoverUv1, // Bottom Right coordinates
                                                    (selection.appid == SKIF_STEAM_APPID)
                                                    ? ImVec4 ( 1.0f,  1.0f,  1.0f, 1.0f) // Tint for Special K (always full strength)
                                                    : ImVec4 (fTint, fTint, fTint, fAlpha), // Tint for other games (transition up and down as mouse is hovered)
                                  (! _registry.bDisableBorders) ? ImGui::GetStyleColorVec4 (ImGuiCol_Border) : ImVec4(0.0f, 0.0f, 0.0f, 0.0f) // Border
  );

  // Every >15 ms, increase/decrease the cover fade effect (makes it frame rate independent)
  static DWORD timeLastTick;
  DWORD timeCurr = SKIF_Util_timeGetTime();
  bool isHovered = ImGui::IsItemHovered();
  bool incTick = false;
  extern int startupFadeIn;

  if (startupFadeIn == 0 && pTexSRV.p != nullptr)
    startupFadeIn = 1;

  if (startupFadeIn == 1)
  {
    if (fAlpha < 1.0f && pTexSRV.p != nullptr)
    {
      if (timeCurr - timeLastTick > 15)
      {
        fAlpha += 0.05f;
        incTick = true;
      }
    }

    if (fAlpha >= 1.0f)
      startupFadeIn = 2;
  }

  if (_registry.iDimCovers == 2)
  {
    if (isHovered && fTint < 1.0f)
    {
      if (timeCurr - timeLastTick > 15)
      {
        fTint = fTint + 0.01f;
        incTick = true;
      }

      coverFadeActive = true;
    }
    else if (! isHovered && fTint > fTintMin)
    {
      if (timeCurr - timeLastTick > 15)
      {
        fTint = fTint - 0.01f;
        incTick = true;
      }

      coverFadeActive = true;
    }
  }

  // Increment the tick
  if (incTick)
    timeLastTick = timeCurr;

  if (ImGui::IsItemClicked (ImGuiMouseButton_Right))
    ImGui::OpenPopup ("CoverMenu");

  if (ImGui::BeginPopup ("CoverMenu"))
  {
    //static
     // app_record_s* pApp = nullptr;

    //for (auto& app : apps)
    //  if (app.second.id == appid)
    //    pApp = &app.second;

    if (pApp != nullptr)
    {
      // Column 1: Icons

      ImGui::BeginGroup  ( );
      ImVec2 iconPos = ImGui::GetCursorPos();

      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_FILE_IMAGE)       .x, ImGui::GetTextLineHeight()));
      if (pApp->textures.isCustomCover)
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_UNDO_ALT)       .x, ImGui::GetTextLineHeight()));
      ImGui::PushStyleColor (ImGuiCol_Separator, ImVec4(0, 0, 0, 0));
      ImGui::Separator  (  );
      ImGui::PopStyleColor (  );
      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_EXTERNAL_LINK_ALT).x, ImGui::GetTextLineHeight()));

      ImGui::EndGroup   (  );

      ImGui::SameLine   (  );

      // Column 2: Items
      ImGui::BeginGroup (  );
      bool dontCare = false;
      if (ImGui::Selectable ("Set Custom Artwork",   dontCare, ImGuiSelectableFlags_SpanAllColumns))
      {
        LPWSTR pwszFilePath = NULL;
        if (SK_FileOpenDialog(&pwszFilePath, COMDLG_FILTERSPEC{ L"Images", L"*.jpg;*.png" }, 1, FOS_FILEMUSTEXIST, FOLDERID_Pictures))
        {
          std::wstring targetPath = L"";
          std::wstring ext        = std::filesystem::path(pwszFilePath).extension().wstring();

          if (pApp->id == SKIF_STEAM_APPID)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\)",           path_cache.specialk_userdata);
          else if (pApp->store == "SKIF")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Custom\%i\)", path_cache.specialk_userdata, pApp->id);
          else if (pApp->store == "EGS")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\EGS\%ws\)",   path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->EGS_AppName).c_str());
          else if (pApp->store == "GOG")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\GOG\%i\)",    path_cache.specialk_userdata, pApp->id);
          else if (pApp->store == "Xbox")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Xbox\%ws\)",  path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->Xbox_PackageName).c_str());
          else if (pApp->store == "Steam")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)",  path_cache.specialk_userdata, pApp->id);

          if (targetPath != L"")
          {
            std::error_code ec;
            // Create any missing directories
            if (! std::filesystem::exists (            targetPath, ec))
                  std::filesystem::create_directories (targetPath, ec);

            targetPath += L"cover";

            if (ext == L".jpg")
              DeleteFile((targetPath + L".png").c_str());

            CopyFile(pwszFilePath, (targetPath + ext).c_str(), false);

            update    = true;
            lastCover = 0; // Needed as otherwise SKIF would not reload the cover
          }
        }
      }
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
      }

      if (pApp->textures.isCustomCover)
      {
        if (ImGui::Selectable ("Clear Custom Artwork", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          std::wstring targetPath = L"";

          if (pApp->id == SKIF_STEAM_APPID)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\)",           path_cache.specialk_userdata);
          else if (pApp->store == "SKIF")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Custom\%i\)", path_cache.specialk_userdata, pApp->id);
          else if (pApp->store == "EGS")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\EGS\%ws\)",   path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->EGS_AppName).c_str());
          else if (pApp->store == "GOG")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\GOG\%i\)",    path_cache.specialk_userdata, pApp->id);
          else if (pApp->store == "Xbox")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Xbox\%ws\)",  path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->Xbox_PackageName).c_str());
          else if (pApp->store == "Steam")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)",  path_cache.specialk_userdata, pApp->id);

          if (PathFileExists (targetPath.c_str()))
          {
            targetPath += L"cover";

            bool d1 = DeleteFile ((targetPath + L".png").c_str()),
                 d2 = DeleteFile ((targetPath + L".jpg").c_str());

            // If any file was removed
            if (d1 || d2)
            {
              update    = true;
              lastCover = 0; // Needed as otherwise SKIF would not reload the cover
            }
          }
        }
        else
        {
          SKIF_ImGui_SetMouseCursorHand ( );
        }
      }

      ImGui::Separator  (  );

      // Strip (recently added) from the game name
      std::string name = pApp->names.normal;
      try {
        name = std::regex_replace(name, std::regex(R"( \(recently added\))"), "");
      }
      catch (const std::exception& e)
      {
        UNREFERENCED_PARAMETER(e);
      }

      std::string linkGridDB = (pApp->store == "Steam")
                             ? SK_FormatString("https://www.steamgriddb.com/steam/%lu/grids", pApp->id)
                             : SK_FormatString("https://www.steamgriddb.com/search/grids?term=%s", name.c_str());

      if (ImGui::Selectable ("Browse SteamGridDB",   dontCare, ImGuiSelectableFlags_SpanAllColumns))
      {
        SKIF_Util_OpenURI   (SK_UTF8ToWideChar(linkGridDB).c_str());
      }
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
        SKIF_ImGui_SetHoverText       (linkGridDB);
      }

      ImGui::EndGroup   (  );

      ImGui::SetCursorPos (iconPos);

      ImGui::TextColored (
          (_registry.iStyle == 2) ? ImColor (0, 0, 0) : ImColor (255, 255, 255),
                ICON_FA_FILE_IMAGE
                            );

      if (pApp->textures.isCustomCover)
        ImGui::TextColored (
          (_registry.iStyle == 2) ? ImColor (0, 0, 0) : ImColor (255, 255, 255),
                  ICON_FA_UNDO_ALT
                              );

      ImGui::Separator  (  );

      ImGui::TextColored (
          ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                ICON_FA_EXTERNAL_LINK_ALT
                            );

    }

    ImGui::EndPopup   (  );
  }

  float fY =
  ImGui::GetCursorPosY (                                                  );

  ImGui::EndGroup             ( );
  ImGui::SameLine             ( );
  
  float fZ =
  ImGui::GetCursorPosX (                                                  );

  if (update)
  {
    //SKIF_GameManagement_ShowScreenshot (L"");
    update      = false;

    // Ensure we aren't already loading this cover
    if (lastCover != pApp->id)
    {
      loadCover = true;
      lastCover = pApp->id;
    }
  }

  if (loadCover && populated /* && (ImGui::GetCurrentWindowRead()->HiddenFramesCannotSkipItems == 0) */ && ! InterlockedExchangeAdd (&icon_thread, 0))
  { // Load cover first after the window has been shown -- to fix one copy leaking of the cover 
    // Note from 2023-03-24: Is this even needed any longer after fixing the double-loading that was going on?
    // Note from 2023-03-25: Disabled HiddenFramesCannotSkipItems check to see if it's solved.
    loadCover = false;

    if ( appinfo != nullptr && pApp->store == "Steam")
    {
      skValveDataFile::appinfo_s *pAppInfo =
        appinfo->getAppInfo ( pApp->id );

      DBG_UNREFERENCED_LOCAL_VARIABLE (pAppInfo);
    }

    //PLOG_VERBOSE << "ImGui Frame Counter: " << ImGui::GetFrameCount();

    // We're going to stream the cover in asynchronously on this thread
    _beginthread ([](void*)->void
    {
      CoInitializeEx (nullptr, 0x0);

      SKIF_Util_SetThreadDescription (GetCurrentThread (), L"SKIF_LibCoverWorker");

      PLOG_INFO << "Thread started!";
      PLOG_INFO << "Streaming game cover asynchronously...";

      int queuePos = getTextureLoadQueuePos();
      //PLOG_VERBOSE << "queuePos = " << queuePos;

      static ImVec2 _vecCoverUv0(vecCoverUv0);
      static ImVec2 _vecCoverUv1(vecCoverUv1);
      static CComPtr <ID3D11ShaderResourceView> _pTexSRV (pTexSRV.p);

      // Most textures are pushed to be released by LoadLibraryTexture(),
      //  however the current cover pointer is only updated to the new one
      //   *after* the old cover has been pushed to be released.
      //  
      // This means there's a short thread race where the main thread
      //  can still reference a texture that has already been released.
      //
      // As a result, we preface the whole loading of the new cover texture
      //  by explicitly changing the current cover texture to point to nothing.
      //
      // The only downside is that the cover transition is not seemless;
      //  a black/non-existent cover will be displayed in-between.
      // 
      // But at least SKIF does not run the risk of crashing as often :)
      pTexSRV = nullptr;

      std::wstring load_str;

      // SKIF
      if (pApp->id == SKIF_STEAM_APPID)
      {
        load_str = L"_library_600x900_x2.jpg";
      }

      // SKIF Custom
      else if (pApp->store == "SKIF")
      {
        load_str = L"cover";
      }

      // GOG
      else if (pApp->store == "GOG")
      {
        load_str = L"*_glx_vertical_cover.webp";
      }

      // EGS
      else if (pApp->store == "EGS")
      {
        load_str = 
          SK_FormatStringW (LR"(%ws\Assets\EGS\%ws\OfferImageTall.jpg)", path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->EGS_AppName).c_str());

        if ( ! PathFileExistsW (load_str.   c_str ()) )
        {
          SKIF_EGS_IdentifyAssetNew (pApp->EGS_CatalogNamespace, pApp->EGS_CatalogItemId, pApp->EGS_AppName, pApp->EGS_DisplayName);
        }
        
        else {
          // If the file exist, load the metadata from the local image, but only if low bandwidth mode is not enabled
          if ( ! _registry.bLowBandwidthMode &&
                SUCCEEDED (
                DirectX::GetMetadataFromWICFile (
                  load_str.c_str (),
                    DirectX::WIC_FLAGS_FILTER_POINT,
                      meta
                  )
                )
              )
          {
            // If the image is in reality 600 in width or 900 in height, which indicates a low-res cover,
            //   download the full-size cover and replace the existing one.
            if (meta.width  == 600 ||
                meta.height == 900)
            {
              SKIF_EGS_IdentifyAssetNew (pApp->EGS_CatalogNamespace, pApp->EGS_CatalogItemId, pApp->EGS_AppName, pApp->EGS_DisplayName);
            }
          }
        }
      }

      // Xbox
      else if (pApp->store == "Xbox")
      {
        load_str = 
          SK_FormatStringW (LR"(%ws\Assets\Xbox\%ws\cover-original.png)", path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->Xbox_PackageName).c_str());

        if ( ! PathFileExistsW (load_str.   c_str ()) )
        {
          SKIF_Xbox_IdentifyAssetNew (pApp->Xbox_PackageName, pApp->Xbox_StoreId);
        }
        
        else {
          // If the file exist, load the metadata from the local image, but only if low bandwidth mode is not enabled
          if ( ! _registry.bLowBandwidthMode &&
                SUCCEEDED (
                DirectX::GetMetadataFromWICFile (
                  load_str.c_str (),
                    DirectX::WIC_FLAGS_FILTER_POINT,
                      meta
                  )
                )
              )
          {
            // If the image is in reality 600 in width or 900 in height, which indicates a low-res cover,
            //   download the full-size cover and replace the existing one.
            if (meta.width  == 600 ||
                meta.height == 900)
            {
              SKIF_Xbox_IdentifyAssetNew (pApp->Xbox_PackageName, pApp->Xbox_StoreId);
            }
          }
        }
      }

      // Steam
      else if (pApp->store == "Steam")
      {
        std::wstring load_str_2x (
          SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)", path_cache.specialk_userdata, pApp->id)
        );

        std::error_code ec;
        // Create any missing directories
        if (! std::filesystem::exists (            load_str_2x, ec))
              std::filesystem::create_directories (load_str_2x, ec);

        load_str_2x += L"library_600x900_x2.jpg";
      
        load_str = SK_GetSteamDir ();

        load_str   += LR"(/appcache/librarycache/)" +
          std::to_wstring (pApp->id)                +
                                  L"_library_600x900.jpg";

        std::wstring load_str_final = load_str;
        //std::wstring load_str_final = L"_library_600x900.jpg";

        // If 600x900 exists but 600x900_x2 cannot be found
        if (  PathFileExistsW (load_str.   c_str ()) &&
            ! PathFileExistsW (load_str_2x.c_str ()) )
        {
          // Load the metadata from 600x900, but only if low bandwidth mode is not enabled
          if ( ! _registry.bLowBandwidthMode &&
                SUCCEEDED (
                DirectX::GetMetadataFromWICFile (
                  load_str.c_str (),
                    DirectX::WIC_FLAGS_FILTER_POINT,
                      meta
                  )
                )
              )
          {
            // If the image is in reality 300x450, which indicates a real cover,
            //   download the real 600x900 cover and store it in _x2
            if (meta.width  == 300 &&
                meta.height == 450)
            {
              SKIF_HTTP_GetAppLibImg (pApp->id, load_str_2x);
              load_str_final = load_str_2x;
              //load_str_final = L"_library_600x900_x2.jpg";
            }
          }
        }

        // If 600x900_x2 exists, check the last modified time stamps
        else {
          WIN32_FILE_ATTRIBUTE_DATA faX1{}, faX2{};

          // ... but only if low bandwidth mode is disabled
          if (! _registry.bLowBandwidthMode &&
              GetFileAttributesEx (load_str   .c_str (), GetFileExInfoStandard, &faX1) &&
              GetFileAttributesEx (load_str_2x.c_str (), GetFileExInfoStandard, &faX2))
          {
            // If 600x900 has been edited after 600_900_x2,
            //   download new copy of the 600_900_x2 cover
            if (CompareFileTime (&faX1.ftLastWriteTime, &faX2.ftLastWriteTime) == 1)
            {
              DeleteFile (load_str_2x.c_str ());
              SKIF_HTTP_GetAppLibImg (pApp->id, load_str_2x);
            }
          }
          
          // If 600x900_x2 exists now, load it
          if (PathFileExistsW (load_str_2x.c_str ()))
            load_str_final = load_str_2x;
        }

        load_str = load_str_final;
      }
    
      LoadLibraryTexture ( LibraryTexture::Cover,
                              pApp->id,
                                _pTexSRV,
                                  load_str,
                                    _vecCoverUv0,
                                      _vecCoverUv1,
                                        pApp);

      PLOG_VERBOSE << "_pTexSRV = " << _pTexSRV;

      int currentQueueLength = textureLoadQueueLength.load();

      if (currentQueueLength == queuePos)
      {
        PLOG_DEBUG << "Texture is live! Swapping it in.";
        vecCoverUv0 = _vecCoverUv0;
        vecCoverUv1 = _vecCoverUv1;
        pTexSRV     = _pTexSRV;

        // Force a refresh when the cover has been swapped in
        PostMessage (SKIF_hWnd, WM_SKIF_COVER, 0x0, 0x0);
      }

      else if (_pTexSRV.p != nullptr)
      {
        PLOG_DEBUG << "Texture is late! (" << queuePos << " vs " << currentQueueLength << ")";
        extern concurrency::concurrent_queue <CComPtr <IUnknown>> SKIF_ResourcesToFree;
        PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << _pTexSRV.p << " to be released";;
        SKIF_ResourcesToFree.push(_pTexSRV.p);
        _pTexSRV.p = nullptr;
      }

      PLOG_INFO << "Finished streaming game cover asynchronously...";

      //InterlockedExchange(&cover_thread, 0);

      PLOG_INFO << "Thread stopped!";
    }, 0x0, NULL);
  }

  /*
  float fTestScale    = SKIF_ImGui_GlobalDPIScale,
        fScrollbar    = ImGui::GetStyle ().ScrollbarSize,
        fFrameWidth   = ImGui::GetStyle ().FramePadding.x * 4.0f,
        fSpacing      = ImGui::GetStyle ().ItemSpacing.x  * 4.0f;
        fDecorations  = (fFrameWidth + fSpacing + fScrollbar);
      //fFrameHeight  = ImGui::GetStyle ().FramePadding.y * 2.0f,
      //fSpaceHeight  = ImGui::GetStyle ().ItemSpacing.y  * 2.0f,
        fInjectWidth =
         ( sk_global_ctl_x  + fDecorations - fScrollbar * 2.0f ),
        fLongestLabel =
         ( 32 + max_app_name_len + fDecorations );
  */

// AppListInset1
#define _WIDTH (414.0f * SKIF_ImGui_GlobalDPIScale) // std::max ( fInjectWidth * fTestScale, std::min ( 640.0f * fTestScale, fLongestLabel * fTestScale ) )
  //_WIDTH  (640/2)
#define _HEIGHT (620.0f * SKIF_ImGui_GlobalDPIScale) - (ImGui::GetStyle().FramePadding.x - 2.0f) //(float)_WIDTH / (fAspect)
  //_HEIGHT (360/2)

// AppListInset2
#define _WIDTH2  (414.0f * SKIF_ImGui_GlobalDPIScale) //((float)_WIDTH)
#define _HEIGHT2 (280.0f * SKIF_ImGui_GlobalDPIScale) // (900.0f * SKIF_ImGui_GlobalDPIScale/(21.0f/9.0f)/2.0f + 88.0f /*(float)_WIDTH / (21.0f/9.0f) + fFrameHeight + fSpaceHeight * 2.0f*/)

  ImGui::BeginGroup ();

  auto _HandleKeyboardInput = [&](void)
  {
          auto& duration     = io.KeysDownDuration;
           bool bText        = false;
    static char test_ [1024] = {      };
           char out   [2]    = { 0, 0 };

    auto _Append = [&](char c) {
      out [0] = c; StrCatA (test_, out);
      bText   = true;
    };

    static auto
      constexpr _text_chars =
        { 'A','B','C','D','E','F','G','H',
          'I','J','K','L','M','N','O','P',
          'Q','R','S','T','U','V','W','X',
          'Y','Z','0','1','2','3','4','5',
          '6','7','8','9',' ','-',':','.' };

    for ( auto c : _text_chars )
    {
      if (duration [c] == 0.0f)
      {
        _Append (c);
      }
    }

    const  DWORD dwTimeout    = 850UL; // 425UL
    static DWORD dwLastUpdate = SKIF_Util_timeGetTime ();

    struct {
      std::string text = "";
      std::string store;
      uint32_t    app_id = 0;
    } static result;

    if (bText)
    {
      dwLastUpdate = SKIF_Util_timeGetTime ();

      if (labels.search (test_))
      {
        for (auto& app : apps)
        {
          if (app.second.names.all_upper.find (test_) == 0)
          {
            result.text   = app.second.names.normal;
            result.store  = app.second.store;
            result.app_id = app.second.id;

            break;
          }
        }
      }

      else
      {
        strncpy (test_, result.text.c_str (), 1023);
      }
    }

    if (! result.text.empty ())
    {
      size_t len =
        strlen (test_);

      std::string strText = result.text.substr(0, len),
                  strHelp = result.text.substr (len, result.text.length () - len);

      ImGui::OpenPopup         ("###KeyboardHint");

      ImGui::SetNextWindowPos  (ImGui::GetCurrentWindowRead()->Viewport->GetMainRect().GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));

      if (ImGui::BeginPopupModal("###KeyboardHint", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize))
      {
        ImGui::TextColored ( ImColor::HSV(0.0f, 0.0f, 0.75f), // ImColor(53, 255, 3)
                                "%s", strText.c_str ()
        );

        if (! strHelp.empty ())
        {
          ImGui::SameLine ();
          ImGui::SetCursorPosX (
            ImGui::GetCursorPosX () -
            ImGui::GetStyle      ().ItemSpacing.x
          );
          ImGui::TextDisabled ("%s", strHelp.c_str ());
        }

        ImGui::EndPopup ( );
      }
    }

    if (                       dwLastUpdate != MAXDWORD &&
         SKIF_Util_timeGetTime () - dwLastUpdate >
                               dwTimeout )
    {
      if (result.app_id != 0)
      {
        *test_           = '\0';
        dwLastUpdate     = MAXDWORD;
        if (result.app_id != pApp->id || 
            result.store  != pApp->store)
        {
          manual_selection.id    = result.app_id;
          manual_selection.store = result.store;
        }
        result.app_id    = 0;
        result.store.clear ();
        result.text .clear ();
      }
    }
  };

  if (AddGamePopup    == PopupState::Closed &&
      ModifyGamePopup == PopupState::Closed &&
      RemoveGamePopup == PopupState::Closed &&
      ! io.KeyCtrl)
    _HandleKeyboardInput ();

  auto _PrintInjectionSummary = [&](app_record_s* pTargetApp) ->
  float
  {
    if ( pTargetApp != nullptr && pTargetApp->id != SKIF_STEAM_APPID )
    {
      struct summary_cache_s {
        struct {
          std::string   type;
          struct {
            std::string text;
            ImColor     color;
            ImColor     color_hover;
          } status;
          std::string   hover_text;
        } injection;
        std::string config_repo;
        struct {
          std::string shorthand; // Converted to utf-8 from utf-16
          std::string root_dir;  // Converted to utf-8 from utf-16
          std::string full_path; // Converted to utf-8 from utf-16
        } config;
        struct {
          std::string shorthand; // Converted to utf-8 from utf-16
          std::string version;   // Converted to utf-8 from utf-16
          std::string full_path; // Converted to utf-8 from utf-16
        } dll;
        AppId_t     app_id   = 0;
        DWORD       running  = 0;
        bool        service  = false;
        bool        autostop = false;
      } static cache;

      if (         cache.service  != _inject.bCurrentState       ||
                   cache.running  != pTargetApp->_status.running ||
                   cache.autostop != _inject.bAckInj
         )
      {
        cache.app_id = 0;
      }

      if (pTargetApp->id != cache.app_id)
      {
        cache.app_id   = pTargetApp->id;
        cache.running  = pTargetApp->_status.running;
        cache.autostop = _inject.bAckInj;

        cache.service  = (pTargetApp->specialk.injection.injection.bitness == InjectionBitness::ThirtyTwo &&  _inject.pid32) ||
                         (pTargetApp->specialk.injection.injection.bitness == InjectionBitness::SixtyFour &&  _inject.pid64) ||
                         (pTargetApp->specialk.injection.injection.bitness == InjectionBitness::Unknown   && (_inject.pid32  &&
                                                                                                             _inject.pid64));

        sk_install_state_s& sk_install =
          pTargetApp->specialk.injection;

        wchar_t     wszDLLPath [MAX_PATH];
        wcsncpy_s ( wszDLLPath, MAX_PATH,
                      sk_install.injection.dll_path.c_str (),
                              _TRUNCATE );

        cache.dll.full_path = SK_WideCharToUTF8 (wszDLLPath);

        PathStripPathW (                         wszDLLPath);
        cache.dll.shorthand = SK_WideCharToUTF8 (wszDLLPath);
        cache.dll.version   = SK_WideCharToUTF8 (sk_install.injection.dll_ver);

        wchar_t     wszConfigPath [MAX_PATH];
        wcsncpy_s ( wszConfigPath, MAX_PATH,
                      sk_install.config.file.c_str (),
                              _TRUNCATE );

        auto& cfg           = cache.config;
        cfg.root_dir        = SK_WideCharToUTF8 (sk_install.config.dir);
        cfg.full_path
                            = SK_WideCharToUTF8 (wszConfigPath);
        PathStripPathW (                         wszConfigPath);
        cfg.shorthand       = SK_WideCharToUTF8 (wszConfigPath);

        //if (! PathFileExistsW (sk_install.config.file.c_str ()))
        //  cfg.shorthand.clear ();

        //if (! PathFileExistsA (cache.dll.full_path.c_str ()))
        //  cache.dll.shorthand.clear ();

        cache.injection.type        = "None";
        cache.injection.status.text.clear ();
        cache.injection.hover_text.clear  ();

        switch (sk_install.injection.type)
        {
          case sk_install_state_s::Injection::Type::Local:
            cache.injection.type = "Local";
            break;

          case sk_install_state_s::Injection::Type::Global:
          default: // Unknown injection strategy, but let's assume global would work

            if ( _inject.bHasServlet )
            {
              cache.injection.type         = "Global";
              cache.injection.status.text  = 
                         (cache.service)   ? (_inject.bAckInj) ? "Waiting for game..." : "Running"
                                           : "                                "; //"Service Status";

              cache.injection.status.color =
                         (cache.service)   ? ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success)  // HSV (0.3F,  0.99F, 1.F)
                                           : ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info); // HSV (0.08F, 0.99F, 1.F);
              cache.injection.status.color_hover =
                         (cache.service)   ? ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success) * ImVec4(0.8f, 0.8f, 0.8f, 1.0f)
                                           : ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info) * ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
              cache.injection.hover_text   =
                         (cache.service)   ? "Click to stop the service"
                                           : "Click to start the service";
            }
            break;
        }

        switch (sk_install.config.type)
        {
          case ConfigType::Centralized:
            cache.config_repo = "Centralized"; break;
          case ConfigType::Localized:
            cache.config_repo = "Localized";   break;
          default:
            cache.config_repo = "Unknown";
            cache.config.shorthand.clear ();   break;
        }
      }

      auto __IsOutdatedLocalDLLFile = [&](void) -> bool
      {
        bool ret = false;

        if ((pTargetApp->store != "Steam") ||
            (pTargetApp->store == "Steam"  && // Exclude the check for games with known older versions
              cache.app_id != 405900       && // Disgaea PC
              cache.app_id != 359870       && // FFX/X-2 HD Remaster
            //cache.app_id != 578330       && // LEGO City Undercover // Do not exclude from the updater as its a part of mainline SK
              cache.app_id != 429660       && // Tales of Berseria
              cache.app_id != 372360       && // Tales of Symphonia
              cache.app_id != 738540       && // Tales of Vesperia DE
              cache.app_id != 351970          // Tales of Zestiria: 
            ))
        {
          if (SKIF_Util_CompareVersionStrings (SK_UTF8ToWideChar(_inject.SKVer32), SK_UTF8ToWideChar(cache.dll.version)) > 0)
          {
            ret = true;
          }
        }
        
        return ret;
      };

      auto __UpdateLocalDLLFile = [&](void) -> void
      {
        int iBinaryType = SKIF_Util_GetBinaryType (SK_UTF8ToWideChar (cache.dll.full_path).c_str());
        if (iBinaryType > 0)
        {
          wchar_t                       wszPathToGlobalDLL [MAX_PATH + 2] = { };
          GetModuleFileNameW  (nullptr, wszPathToGlobalDLL, MAX_PATH);
          PathRemoveFileSpecW (         wszPathToGlobalDLL);
          PathAppendW         (         wszPathToGlobalDLL, (iBinaryType == 2) ? L"SpecialK64.dll" : L"SpecialK32.dll");

          if (CopyFile (wszPathToGlobalDLL, SK_UTF8ToWideChar (cache.dll.full_path).c_str(), FALSE))
          {
            PLOG_INFO << "Successfully updated " << SK_UTF8ToWideChar (cache.dll.full_path) << " from v " << SK_UTF8ToWideChar (cache.dll.version) << " to v " << SK_UTF8ToWideChar (_inject.SKVer32);

            // Force an update of the injection strategy
            updateInjStrat = true;
            cache.app_id = 0;
          }

          else {
            PLOG_ERROR << "Failed to copy " << wszPathToGlobalDLL << " to " << SK_UTF8ToWideChar (cache.dll.full_path);
            PLOG_ERROR << SKIF_Util_GetErrorAsWStr();
          }
        }

        else {
          PLOG_ERROR << "Failed to retrieve binary type from " << SK_UTF8ToWideChar (cache.dll.full_path) << " -- returned: " << iBinaryType;
          PLOG_ERROR << SKIF_Util_GetErrorAsWStr();
        }
      };

      static constexpr float
           num_lines = 4.0f;
      auto line_ht   =
        ImGui::GetTextLineHeightWithSpacing ();

      auto frame_id =
        ImGui::GetID ("###Injection_Summary_Frame");

      SKIF_ImGui_BeginChildFrame ( frame_id,
                                     ImVec2 ( _WIDTH - ImGui::GetStyle ().FrameBorderSize * 2.0f,
                                                                                num_lines * line_ht ),
                                        ImGuiWindowFlags_NavFlattened      |
                                        ImGuiWindowFlags_NoScrollbar       |
                                        ImGuiWindowFlags_NoScrollWithMouse |
                                        ImGuiWindowFlags_AlwaysAutoResize  |
                                        ImGuiWindowFlags_NoBackground
                                 );

      ImGui::BeginGroup       ();

      // Column 1
      ImGui::BeginGroup       ();
      ImGui::PushStyleColor   (ImGuiCol_Text, ImVec4 (0.5f, 0.5f, 0.5f, 1.f));
      //ImGui::NewLine          ();
      ImGui::TextUnformatted  ("Injection:");
      //ImGui::TextUnformatted  ("Injection DLL:");
      ImGui::TextUnformatted  (u8"���ø�Ŀ¼:");
      ImGui::TextUnformatted  (u8"�����ļ�:");
      ImGui::PopStyleColor    ();
      ImGui::ItemSize         (ImVec2 (110.f * SKIF_ImGui_GlobalDPIScale,
                                         0.f)
                              ); // Column should have min-width 130px (scaled with the DPI)
      ImGui::EndGroup         ();

      ImGui::SameLine         ();

      // Column 2
      ImGui::BeginGroup       ();
      // Injection Strategy
      //ImGui::TextUnformatted  (cache.injection.type.c_str   ());
      //ImGui::NewLine          ();

      // Injection DLL
      if (! cache.dll.shorthand.empty ())
      {
        //ImGui::TextUnformatted  (cache.dll.shorthand.c_str  ());
        ImGuiSelectableFlags flags = ImGuiSelectableFlags_AllowItemOverlap;

        if (cache.injection.type._Equal("Global"))
          flags |= ImGuiSelectableFlags_Disabled;

        bool openLocalMenu = false;

        std::string uiInjectionText = cache.injection.type._Equal("Local")
          ? SK_FormatString (R"(%s v %s (%s))", cache.injection.type.c_str(), cache.dll.version.c_str(), cache.dll.shorthand.c_str())
          : SK_FormatString (R"(%s v %s)", cache.injection.type.c_str(), cache.dll.version.c_str());

        ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption));
        if (ImGui::Selectable (uiInjectionText.c_str(), false, flags))
        {
          openLocalMenu = true;
        }
        ImGui::PopStyleColor();

        if (cache.injection.type._Equal("Local"))
        {
          SKIF_ImGui_SetMouseCursorHand ( );

          if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            openLocalMenu = true;
        }

        SKIF_ImGui_SetHoverText       (cache.dll.full_path.c_str  ());
        
        if (openLocalMenu && ! ImGui::IsPopupOpen ("LocalDLLMenu"))
          ImGui::OpenPopup    ("LocalDLLMenu");

        if (ImGui::BeginPopup ("LocalDLLMenu"))
        {
          if (__IsOutdatedLocalDLLFile ( ))
          {
            if (ImGui::Selectable (("Update to v " + _inject.SKVer32).c_str( )))
            {
              __UpdateLocalDLLFile ( );
            }

            ImGui::Separator ( );
          }

          if (ImGui::Selectable ("Uninstall"))
          {

            if (DeleteFile (SK_UTF8ToWideChar(cache.dll.full_path).c_str()))
            {
              PLOG_INFO << "Successfully uninstalled local DLL v " << SK_UTF8ToWideChar(cache.dll.version) << " from " << SK_UTF8ToWideChar(cache.dll.full_path);

              // Force an update of the injection strategy
              updateInjStrat = true;
              cache.app_id = 0;
            }
          }

          ImGui::EndPopup ( );
        }
      }

      else
        ImGui::TextUnformatted ("N/A");

      // Config Root
      // Config File
      if (! cache.config.shorthand.empty ())
      {
        // Config Root
        if (ImGui::Selectable         (cache.config_repo.c_str ()))
        {
          std::wstring wsRootDir =
            SK_UTF8ToWideChar (cache.config.root_dir);

          std::error_code ec;
          // Create any missing directories
          if (! std::filesystem::exists (            wsRootDir, ec))
                std::filesystem::create_directories (wsRootDir, ec);

          SKIF_Util_ExplorePath       (wsRootDir);
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       (cache.config.root_dir.c_str ());

        // Config File
        if (ImGui::Selectable         (cache.config.shorthand.c_str ()))
        {
          std::wstring wsRootDir =
            SK_UTF8ToWideChar (cache.config.root_dir);

          std::error_code ec;
          // Create any missing directories
          if (! std::filesystem::exists (            wsRootDir, ec))
                std::filesystem::create_directories (wsRootDir, ec);

          HANDLE h = CreateFile ( SK_UTF8ToWideChar (cache.config.full_path).c_str(),
                         GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL,
                               CREATE_NEW,
                                 FILE_ATTRIBUTE_NORMAL,
                                   NULL );

          // We need to close the handle as well, as otherwise Notepad will think the file
          //   is still in use (trigger Save As dialog on Save) until SKIF gets closed
          if (h != INVALID_HANDLE_VALUE)
            CloseHandle (h);

          SKIF_Util_OpenURI (SK_UTF8ToWideChar (cache.config.full_path).c_str(), SW_SHOWNORMAL, NULL);
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       (cache.config.full_path.c_str ());


        if ( ! ImGui::IsPopupOpen ("ConfigFileMenu") &&
               ImGui::IsItemClicked (ImGuiMouseButton_Right))
          ImGui::OpenPopup      ("ConfigFileMenu");

        if (ImGui::BeginPopup ("ConfigFileMenu"))
        {
          ImGui::TextColored (
            ImColor::HSV (0.11F, 1.F, 1.F),
              "Troubleshooting:"
          );

          ImGui::Separator ( );

          struct Preset
          {
            std::string  Name;
            std::wstring Path;

            Preset (std::wstring n, std::wstring p)
            {
              Name = SK_WideCharToUTF8 (n);
              Path = p;
            };
          };

          // Static stuff :D
          static SKIF_DirectoryWatch SKIF_GlobalWatch;
          static SKIF_DirectoryWatch SKIF_CustomWatch;
          static std::vector<Preset> DefaultPresets;
          static std::vector<Preset> CustomPresets;
          static bool runOnceDefaultPresets = true;
          static bool runOnceCustomPresets = true;

          // Directory watches -- updates the vectors automatically
          if (SKIF_GlobalWatch.isSignaled (LR"(Global)", false) || runOnceDefaultPresets)
          {
            runOnceDefaultPresets = false;

            HANDLE hFind = INVALID_HANDLE_VALUE;
            WIN32_FIND_DATA ffd;
            std::vector<Preset> tmpPresets;
            std::wstring PresetFolder = SK_FormatStringW (LR"(%ws\Global\)", path_cache.specialk_userdata);

            hFind = FindFirstFile((PresetFolder + L"default_*.ini").c_str(), &ffd);

            if (INVALID_HANDLE_VALUE != hFind)
            {
              do {
                Preset newPreset = { PathFindFileName(ffd.cFileName), SK_FormatStringW (LR"(%ws\Global\%ws)", path_cache.specialk_userdata, ffd.cFileName) };
                tmpPresets.push_back(newPreset);
              } while (FindNextFile (hFind, &ffd));

              DefaultPresets = tmpPresets;
              FindClose (hFind);
            }
          }

          if (SKIF_CustomWatch.isSignaled (LR"(Global\Custom)", false) || runOnceCustomPresets)
          {
            runOnceCustomPresets = false;

            HANDLE hFind = INVALID_HANDLE_VALUE;
            WIN32_FIND_DATA ffd;
            std::vector<Preset> tmpPresets;
            std::wstring PresetFolder = SK_FormatStringW (LR"(%ws\Global\Custom\)", path_cache.specialk_userdata);

            hFind = FindFirstFile((PresetFolder + L"*.ini").c_str(), &ffd);

            if (INVALID_HANDLE_VALUE != hFind)
            {
              do {
                Preset newPreset = { PathFindFileName(ffd.cFileName), SK_FormatStringW (LR"(%ws\Global\Custom\%ws)", path_cache.specialk_userdata, ffd.cFileName) };
                tmpPresets.push_back(newPreset);
              } while (FindNextFile (hFind, &ffd));

              CustomPresets = tmpPresets;
              FindClose (hFind);
            }
          }
          
          if ((! DefaultPresets.empty() || ! CustomPresets.empty()))
          {
            if (ImGui::BeginMenu("Apply Preset"))
            {
              // Default Presets
              if (! DefaultPresets.empty())
              {
                for (auto preset : DefaultPresets)
                {
                  if (ImGui::Selectable (preset.Name.c_str()))
                  {
                    CopyFile (preset.Path.c_str(), SK_UTF8ToWideChar(cache.config.full_path).c_str(), FALSE);
                    PLOG_VERBOSE << "Copying " << preset.Path << " over to " << SK_UTF8ToWideChar(cache.config.full_path) << ", overwriting any existing file in the process.";
                  }
                }

                if (! CustomPresets.empty())
                  ImGui::Separator ( );
              }

              // Custom Presets
              if (! CustomPresets.empty())
              {
                for (auto preset : CustomPresets)
                {
                  if (ImGui::Selectable (preset.Name.c_str()))
                  {
                    CopyFile (preset.Path.c_str(), SK_UTF8ToWideChar(cache.config.full_path).c_str(), FALSE);
                    PLOG_VERBOSE << "Copying " << preset.Path << " over to " << SK_UTF8ToWideChar(cache.config.full_path) << ", overwriting any existing file in the process.";
                  }
                }
              }

              ImGui::EndMenu ( );
            }

            ImGui::Separator ( );
          }

          if (ImGui::Selectable ("Apply Compatibility Config"))
          {
            std::wofstream config_file(SK_UTF8ToWideChar (cache.config.full_path).c_str());

            if (config_file.is_open())
            {
              std::wstring out_text =
LR"([SpecialK.System]
ShowEULA=false
EnableCEGUI=false
GlobalInjectDelay=0.0

[API.Hook]
d3d9=true
d3d9ex=true
d3d11=true
OpenGL=true
d3d12=true
Vulkan=true

[Steam.Log]
Silent=true

[Input.XInput]
Enable=false

[Input.Gamepad]
EnableDirectInput7=false
EnableDirectInput8=false
EnableHID=false
EnableNativePS4=false
AllowHapticUI=false

[Input.Keyboard]
CatchAltF4=false
BypassAltF4Handler=false

[Textures.D3D11]
Cache=false)";

              config_file.write(out_text.c_str(),
                out_text.length());

              config_file.close();
            }
          }

          SKIF_ImGui_SetHoverTip ("Known as the \"sledgehammer\" config within the community as it disables\n"
                                  "various features of Special K in an attempt to improve compatibility.");

          if (ImGui::Selectable ("Reset Config File"))
          {
            HANDLE h = CreateFile ( SK_UTF8ToWideChar (cache.config.full_path).c_str(),
                        GENERIC_READ | GENERIC_WRITE,
                          FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL,
                              TRUNCATE_EXISTING,
                                FILE_ATTRIBUTE_NORMAL,
                                  NULL );

            // We need to close the handle as well, as otherwise Notepad will think the file
            //   is still in use (trigger Save As dialog on Save) until SKIF gets closed
            if (h != INVALID_HANDLE_VALUE)
              CloseHandle (h);
          }

          ImGui::EndPopup ( );
        }
      }

      else
      {
        ImGui::TextUnformatted        (cache.config_repo.c_str ());
        ImGui::TextUnformatted        ("N/A");
      }

      // Column should have min-width 100px (scaled with the DPI)
      ImGui::ItemSize         (
        ImVec2 ( 130.0f * SKIF_ImGui_GlobalDPIScale,
                   0.0f
               )                );
      ImGui::EndGroup         ( );
      ImGui::SameLine         ( );

      // Column 3
      ImGui::BeginGroup       ( );

      static bool quickServiceHover = false;

      // Service quick toogle / Waiting for game...
      if (cache.injection.type._Equal ("Global") && ! _inject.isPending())
      {
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImColor(0, 0, 0, 0).Value);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImColor(0, 0, 0, 0).Value);
        ImGui::PushStyleColor(ImGuiCol_Text, (quickServiceHover) ? cache.injection.status.color_hover.Value
                                                                 : cache.injection.status.color.Value);

        if (ImGui::Selectable (cache.injection.status.text.c_str(), false, ImGuiSelectableFlags_SpanAllColumns))
        {
          _inject._StartStopInject (cache.service, _registry.bStopOnInjection, pApp->launch_configs[0].isElevated(pApp->id));

          cache.app_id = 0;
        }

        ImGui::PopStyleColor (3);

        quickServiceHover = ImGui::IsItemHovered ();

        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverTip        (
          cache.injection.hover_text.c_str ()
        );

        if ( ! ImGui::IsPopupOpen ("ServiceMenu") &&
               ImGui::IsItemClicked (ImGuiMouseButton_Right))
          ServiceMenu = PopupState::Open;
      }

      else {
        ImGui::NewLine ( );
      }
      

      if (cache.injection.type._Equal ("Local"))
      {
        if (__IsOutdatedLocalDLLFile ( ))
        {
          ImGui::SameLine        ( );

          ImGui::PushStyleColor  (ImGuiCol_Button, ImVec4 (.1f, .1f, .1f, .5f));
          if (ImGui::SmallButton (ICON_FA_ARROW_UP))
          {
            __UpdateLocalDLLFile ( );
          }
          ImGui::PopStyleColor ( );

          SKIF_ImGui_SetHoverTip (("The local DLL file is outdated.\n"
                                    "Click to update it to v " + _inject.SKVer32 + "."));
        }
      }

      ImGui::EndGroup         ();

      // End of columns
      ImGui::EndGroup         ();

      ImGui::EndChildFrame    ();

      ImGui::Separator ();

      auto frame_id2 =
        ImGui::GetID ("###Injection_Play_Button_Frame");

      ImGui::PushStyleVar (
        ImGuiStyleVar_FramePadding,
          ImVec2 ( 120.0f * SKIF_ImGui_GlobalDPIScale,
                    40.0f * SKIF_ImGui_GlobalDPIScale)
      );

      SKIF_ImGui_BeginChildFrame (
        frame_id2, ImVec2 (  0.0f,
                            110.f * SKIF_ImGui_GlobalDPIScale ),
          ImGuiWindowFlags_NavFlattened      |
          ImGuiWindowFlags_NoScrollbar       |
          ImGuiWindowFlags_NoScrollWithMouse |
          ImGuiWindowFlags_AlwaysAutoResize  |
          ImGuiWindowFlags_NoBackground
      );

      ImGui::PopStyleVar ();

      std::string      buttonLabel = ICON_FA_GAMEPAD "  Launch ";// + pTargetApp->type;
      ImGuiButtonFlags buttonFlags = ImGuiButtonFlags_None;

      if (pTargetApp->_status.running)
      {
          buttonLabel = "Running...";
          buttonFlags = ImGuiButtonFlags_Disabled;
          ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle ().Alpha * 0.5f);
      }

      // Disable the button for global injection types if the servlets are missing
      if ( ! _inject.bHasServlet && ! cache.injection.type._Equal ("Local") )
      {
        ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle ().Alpha * 0.5f);
      }

      // This captures two events -- launching through context menu + large button
      if ( ImGui::ButtonEx (
                  buttonLabel.c_str (),
                      ImVec2 ( 150.0f * SKIF_ImGui_GlobalDPIScale,
                                50.0f * SKIF_ImGui_GlobalDPIScale ), buttonFlags )
           ||
        clickedGameLaunch
           ||
        clickedGameLaunchWoSK )
      {

        if ( pTargetApp->store != "Steam" && pTargetApp->store != "EGS" &&
             pTargetApp->launch_configs[0].getExecutableFullPath(pApp->id).find(L"InvalidPath") != std::wstring::npos )
        {
          confirmPopupText = "Could not launch game due to missing executable:\n\n" + SK_WideCharToUTF8(pTargetApp->launch_configs[0].getExecutableFullPath(pApp->id, false));
          ConfirmPopup     = PopupState::Open;
        }

        else {
          // Launch preparations for Global
          if (! cache.injection.type._Equal ("Local"))
          {
            std::string fullPath    = SK_WideCharToUTF8(pTargetApp->launch_configs[0].getExecutableFullPath (pTargetApp->id));
            bool isLocalBlacklisted  = pTargetApp->launch_configs[0].isBlacklisted (pTargetApp->id),
                 isGlobalBlacklisted = _inject._TestUserList (fullPath.c_str (), false);

            if (! clickedGameLaunchWoSK &&
                ! isLocalBlacklisted    &&
                ! isGlobalBlacklisted
               )
            {
              // Whitelist the path if it haven't been already
              if (pTargetApp->store == "Xbox")
              {
                if (! _inject._TestUserList (SK_WideCharToUTF8 (pTargetApp->Xbox_AppDirectory).c_str(), true))
                {
                  if (_inject._AddUserList  (pTargetApp->Xbox_PackageName, true))
                    _inject._StoreList(true);
                }
              }

              else
              {
                if (_inject._WhitelistBasedOnPath (fullPath))
                  _inject._StoreList (true);
              }
            }

            // Kickstart service if it is currently not running
            if (! _inject.bCurrentState && ! clickedGameLaunchWoSK && ! isLocalBlacklisted && ! isGlobalBlacklisted )
              _inject._StartStopInject (false, true, pApp->launch_configs[0].isElevated(pApp->id));

            // Stop the service if the user attempts to launch without SK
            else if ( clickedGameLaunchWoSK && _inject.bCurrentState )
              _inject._StartStopInject (true);
          }

          extern bool GOGGalaxy_Installed;

          // Launch game
          if (pTargetApp->store == "GOG" && GOGGalaxy_Installed && _registry.bPreferGOGGalaxyLaunch && ! clickedGameLaunch && ! clickedGameLaunchWoSK)
          {
            extern std::wstring GOGGalaxy_Path;

            // "D:\Games\GOG Galaxy\GalaxyClient.exe" /command=runGame /gameId=1895572517 /path="D:\Games\GOG Games\AI War 2"

            std::wstring launchOptions = SK_FormatStringW(LR"(/command=runGame /gameId=%d /path="%ws")", pApp->id, pApp->install_dir.c_str());

            SHELLEXECUTEINFOW
            sexi              = { };
            sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
            sexi.lpVerb       = L"OPEN";
            sexi.lpFile       = GOGGalaxy_Path.c_str();
            sexi.lpParameters = launchOptions.c_str();
          //sexi.lpDirectory  = NULL;
            sexi.nShow        = SW_SHOWDEFAULT;
            sexi.fMask        = SEE_MASK_FLAG_NO_UI |
                                SEE_MASK_ASYNCOK    | SEE_MASK_NOZONECHECKS;

            ShellExecuteExW (&sexi);
          }

          else if (pTargetApp->store == "EGS")
          {
            // com.epicgames.launcher://apps/CatalogNamespace%3ACatalogItemId%3AAppName?action=launch&silent=true
            SKIF_Util_OpenURI ((L"com.epicgames.launcher://apps/" + pTargetApp->launch_configs[0].launch_options + L"?action=launch&silent=true").c_str());
          }

          else if (pTargetApp->store == "Steam") {
            //SKIF_Util_OpenURI_Threaded ((L"steam://run/" + std::to_wstring(pTargetApp->id)).c_str()); // This is seemingly unreliable
            SKIF_Util_OpenURI ((L"steam://run/" + std::to_wstring(pTargetApp->id)).c_str());
            pTargetApp->_status.invalidate();
          }
          
          else { // SKIF Custom, GOG without Galaxy, Xbox

            std::wstring wszPath = (pTargetApp->store == "Xbox")
                                  ? pTargetApp->launch_configs[0].executable_helper
                                  : pTargetApp->launch_configs[0].getExecutableFullPath(pTargetApp->id);

            SHELLEXECUTEINFOW
            sexi              = { };
            sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
            sexi.lpVerb       = L"OPEN";
            sexi.lpFile       = wszPath.c_str();
            sexi.lpParameters = pTargetApp->launch_configs[0].launch_options.c_str();
            sexi.lpDirectory  = pTargetApp->launch_configs[0].working_dir   .c_str();
            sexi.nShow        = SW_SHOWDEFAULT;
            sexi.fMask        = SEE_MASK_FLAG_NO_UI |
                                SEE_MASK_ASYNCOK    | SEE_MASK_NOZONECHECKS;

            ShellExecuteExW (&sexi);
          }

          if (_registry.bMinimizeOnGameLaunch)
            SKIF_bSuppressServiceNotification = ShowWindow (SKIF_hWnd, SW_MINIMIZE) == TRUE;
        }

        clickedGameLaunch = clickedGameLaunchWoSK = false;
      }
      
      // Disable the button for global injection types if the servlets are missing
      if ( ! _inject.bHasServlet && ! cache.injection.type._Equal ("Local") )
      {
        ImGui::PopStyleVar ();
        ImGui::PopItemFlag ();
      }

      if (pTargetApp->_status.running)
        ImGui::PopStyleVar ();

      if (ImGui::IsItemClicked(ImGuiMouseButton_Right) &&
          ! openedGameContextMenu)
      {
        openedGameContextMenu = true;
      }

      ImGui::EndChildFrame ();
    }

    return 0.0f;
  };


  ImGui::PushStyleColor      (ImGuiCol_ScrollbarBg, ImVec4(0,0,0,0));
  ImGui::BeginChild          ( "###AppListInset",
                                ImVec2 ( _WIDTH2,
                                         _HEIGHT ), (! _registry.bDisableBorders),
                                    ImGuiWindowFlags_NavFlattened | ImGuiWindowFlags_AlwaysUseWindowPadding );
  ImGui::BeginGroup          ( );

  auto _HandleItemSelection = [&](bool isIconMenu = false) ->
  bool
  {
    bool _GamePadRightClick =
      ( ImGui::IsItemFocused ( ) && ( io.NavInputsDownDuration     [ImGuiNavInput_Input] != 0.0f &&
                                      io.NavInputsDownDurationPrev [ImGuiNavInput_Input] == 0.0f &&
                                            ImGui::GetCurrentContext ()->NavInputSource == ImGuiInputSource_NavGamepad ) );

    static constexpr float _LONG_INTERVAL = .15f;

    bool _NavLongActivate =
      ( ImGui::IsItemFocused ( ) && ( io.NavInputsDownDuration     [ImGuiNavInput_Activate] >= _LONG_INTERVAL &&
                                      io.NavInputsDownDurationPrev [ImGuiNavInput_Activate] <= _LONG_INTERVAL ) );

    bool ret =
      ImGui::IsItemActivated (                      ) ||
      ImGui::IsItemClicked   (ImGuiMouseButton_Left ) ||
      ImGui::IsItemClicked   (ImGuiMouseButton_Right) ||
      _GamePadRightClick                              ||
      _NavLongActivate;

    // If the item is activated, but not visible, scroll to it
    if (ret)
    {
      if (! ImGui::IsItemVisible    (    )) {
        ImGui::SetScrollHereY       (0.5f);
      }
    }

    if (isIconMenu)
    {
      if ( IconMenu != PopupState::Opened &&
           ImGui::IsItemClicked (ImGuiMouseButton_Right))
           IconMenu = PopupState::Open;
    }

    else {
      if ( ! openedGameContextMenu)
      {
        if ( ImGui::IsItemClicked (ImGuiMouseButton_Right) ||
             _GamePadRightClick                            ||
             _NavLongActivate)
        {
          openedGameContextMenu = true;
        }
      }
    }

    return ret;
  };

  static constexpr float __ICON_HEIGHT = 32.0f;

  bool  dontcare     = false;
  float fScale       =
    ( ImGui::GetTextLineHeightWithSpacing () / __ICON_HEIGHT ),

        _ICON_HEIGHT =
    std::min (1.0f, std::max (0.1f, fScale)) * __ICON_HEIGHT;

  ImVec2 f0 = ImGui::GetCursorPos (  );
    ImGui::Selectable ("###zero", &dontcare, ImGuiSelectableFlags_Disabled);
  ImVec2 f1 = ImGui::GetCursorPos (  );
    ImGui::SameLine (                );
    SKIF_ImGui_OptImage (nullptr, ImVec2 (_ICON_HEIGHT, _ICON_HEIGHT));
  ImVec2 f2 = ImGui::GetCursorPos (  );
    ImGui::SameLine (                );
  ImVec2 f3 = ImGui::GetCursorPos (  );
              ImGui::SetCursorPos (ImVec2 (f2.x, f0.y));

  float fOffset =
    ( std::max (f2.y, f1.y) - std::min (f2.y, f1.y) -
           ImGui::GetStyle ().ItemSpacing.y / 2.0f ) * SKIF_ImGui_GlobalDPIScale / 2.0f + (1.0f * SKIF_ImGui_GlobalDPIScale);

  static bool deferred_update = false;


  // Start populating the whole list

  for (auto& app : apps)
  {
    // ID = 0 is assigned to corrupted entries, do not list these.
    if (app.second.id == 0)
      continue;
    
    bool selected = (selection.appid == app.second.id &&
                     selection.store == app.second.store);
    bool change   = false;

    app.second._status.refresh (&app.second);

    float fOriginalY =
      ImGui::GetCursorPosY ();


    // Start Icon + Selectable row

    ImGui::BeginGroup      ();
    ImGui::PushID          (SK_FormatString("%s%i", app.second.store.c_str(), app.second.id).c_str());

    SKIF_ImGui_OptImage    (app.second.textures.icon.p,
                              ImVec2 ( _ICON_HEIGHT,
                                       _ICON_HEIGHT )
                            );

    change |=
      _HandleItemSelection (true);

    ImGui::SameLine        ();

    ImVec4 _color =
      ( app.second._status.updating != 0x0 )
                  ? ImVec4 (ImColor::HSV (0.6f, .6f, 1.f)) :
      ( app.second._status.running  != 0x0 )
                  ? ImVec4 (ImColor::HSV (0.3f, 1.f, 1.f)) :
                    ImGui::GetStyleColorVec4(ImGuiCol_Text);

    // Game Title
    ImGui::PushStyleColor  (ImGuiCol_Text, _color);
    ImGui::PushStyleColor  (ImGuiCol_NavHighlight, ImVec4(0,0,0,0));
    ImGui::SetCursorPosY   (fOriginalY + fOffset);
    ImGui::Selectable      (app.second.ImGuiLabelAndID.c_str(), &selected, ImGuiSelectableFlags_SpanAvailWidth); // (app.first + "###" + app.second.store + std::to_string(app.second.id)).c_str()
    ImGui::PopStyleColor   (2                    );

    static DWORD timeClicked = 0;

    if ( ImGui::IsItemHovered ( ) )
    {
      if ( ImGui::IsMouseDoubleClicked (ImGuiMouseButton_Left) &&
           timeClicked != 0 )
      {
        timeClicked = 0;

        if ( pApp     != nullptr          &&
             pApp->id != SKIF_STEAM_APPID &&
           ! pApp->_status.running
          )
        {
          clickedGameLaunch = true;
        }
      }
      
      else if (ImGui::IsMouseClicked (ImGuiMouseButton_Left) )
      {
        timeClicked = SKIF_Util_timeGetTime ( );
      }
    }

    // Show full title in tooltip if the title spans longer than the width of the Selectable row
    // Old: (app.first.length() > 48)
    // New: ImGui::CalcTextSize  (app.first.c_str()).x > (ImGui::GetContentRegionMax().x - f3.x + f1.x + f1.x)
    if (ImGui::CalcTextSize  (app.first.c_str()).x >= (ImGui::GetContentRegionMax().x - f3.x + f1.x + f1.x))
      SKIF_ImGui_SetHoverTip (app.first);

    // Handle search input
    if (manual_selection.id    == app.second.id &&
        manual_selection.store == app.second.store)
    {
      // Set focus on current row
      ImGui::ActivateItem (ImGui::GetID(app.second.ImGuiLabelAndID.c_str()));
      ImGui::SetFocusID   (ImGui::GetID(app.second.ImGuiLabelAndID.c_str()), ImGui::GetCurrentWindow());

      // Clear stuff
      selection.appid     = 0;
      selection.store.clear();
      manual_selection.id = 0;
      manual_selection.store.clear();
      change           = true;
    }

    change |=
      _HandleItemSelection ();

    ImGui::SetCursorPosY   (fOriginalY - ImGui::GetStyle ().ItemSpacing.y);

    ImGui::PopID           ();
    ImGui::EndGroup        ();

    // End Icon + Selectable row


    //change |=
    //  _HandleItemSelection ();

    // FIXME; Bug that doesn't actually change focus on search
    /*
    if (manual_selection.id    == app.second.id &&
        manual_selection.store == app.second.store)
    {
      selection.appid     = 0;
      selection.store.clear();
      manual_selection.id = 0;
      manual_selection.store.clear();
      change           = true;
    }
    */

    if ( app.second.id == selection.appid &&
                   sort_changed &&
        (! ImGui::IsItemVisible ()) )
    {
      selection.appid  = 0;
      change = true;
    }

    if (change)
    {
      update = (selection.appid != app.second.id ||
                selection.store != app.second.store);

      selection.appid      = app.second.id;
      selection.store      = app.second.store;
      selected   = true;
      _registry.iLastSelectedGame   = selection.appid;
      _registry.wsLastSelectedStore = SK_UTF8ToWideChar (selection.store);

      if (update)
      {
        timeClicked = SKIF_Util_timeGetTime ( );

        app.second._status.invalidate ();

        if (! ImGui::IsMouseDown (ImGuiMouseButton_Right))
        {
          // Activate the row of the current game
          ImGui::ActivateItem (ImGui::GetID(app.second.ImGuiLabelAndID.c_str()));

          if (! ImGui::IsItemVisible    (    )) {
            ImGui::SetScrollHereY       (0.5f);
          } ImGui::SetKeyboardFocusHere (    );

          // This fixes ImGui not allowing the GameContextMenu to be opened on first search
          //   without an additional keyboard input
          ImGuiContext& g = *ImGui::GetCurrentContext();
          g.NavDisableHighlight = false;
        }

        //ImGui::SetFocusID(ImGui::GetID(app.first.c_str()), ImGui::GetCurrentWindow());

        deferred_update = true;
      }
    }

    if (selected)
    {
      // This allows the scroll to reset on DPI changes, to keep the selected item on-screen
      if (SKIF_ImGui_GlobalDPIScale != SKIF_ImGui_GlobalDPIScale_Last)
        ImGui::SetScrollHereY (0.5f);

      if (! update)
      {
        if (std::exchange (deferred_update, false))
        {
          ImGui::SameLine ();
          /*
          auto _ClipMouseToWindowX = [&](void) -> float
          {
            float fWindowX   = ImGui::GetWindowPos              ().x,
                  fScrollX   = ImGui::GetScrollX                (),
                  fContentX0 = ImGui::GetWindowContentRegionMin ().x,
                  fContentX1 = ImGui::GetWindowContentRegionMax ().x,
                  fLocalX    = io.MousePos.x - fWindowX - fScrollX;

            return
              fWindowX + fScrollX +
                std::max   ( fContentX0,
                  std::min ( fContentX1, fLocalX ) );
          };

          // Span the entire width of the list, not just the part with text
          ImVec2 vMax (
            ImGui::GetItemRectMin ().x + ImGui::GetWindowContentRegionWidth (),
            ImGui::GetItemRectMax ().y
          );
          */
          /*
          if (ImGui::IsItemVisible ())
          {
            auto _IsMouseInWindow = [&](void) -> bool
            {
              HWND  hWndActive = ::GetForegroundWindow ();
              RECT  rcClient   = { };
              POINT ptMouse    = { };

              GetClientRect  (hWndActive, &rcClient);
              GetCursorPos   (            &ptMouse );
              ScreenToClient (hWndActive, &ptMouse );

              return PtInRect (&rcClient, ptMouse);
            };

            if (_IsMouseInWindow ())
            {
              if (! ImGui::IsMouseHoveringRect (
                            ImGui::GetItemRectMin (),
                              vMax
                    )
                 ) io.MousePos.y = ImGui::GetCursorScreenPos ().y;
              io.MousePos.x      =       _ClipMouseToWindowX ();
              io.WantSetMousePos = true;
            }
          }
          */

          ImGui::Dummy    (ImVec2 (0,0));
        }
      }

      pApp = &app.second;
    }
  }

  float fOriginalY =
    ImGui::GetCursorPosY ( );

  if (_registry.bDisableStatusBar)
  {
    ImGui::BeginGroup      ( );

    static bool btnHovered = false;
    ImGui::PushStyleColor (ImGuiCol_Header,        ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg));
    ImGui::PushStyleColor (ImGuiCol_HeaderHovered, ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg)); //ImColor (64,  69,  82).Value);
    ImGui::PushStyleColor (ImGuiCol_HeaderActive,  ImGui::GetStyleColorVec4 (ImGuiCol_WindowBg)); //ImColor (56, 60, 74).Value);

    if (btnHovered)
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    else
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.f));

    ImGui::SetCursorPosY   (fOriginalY + fOffset     + ( 1.0f * SKIF_ImGui_GlobalDPIScale));
    ImGui::SetCursorPosX   (ImGui::GetCursorPosX ( ) + ( 3.0f * SKIF_ImGui_GlobalDPIScale));
    ImGui::Text            (ICON_FA_PLUS_SQUARE);
    ImGui::SetCursorPosY   (fOriginalY + fOffset);
    ImGui::SetCursorPosX   (ImGui::GetCursorPosX ( ) + (30.0f * SKIF_ImGui_GlobalDPIScale));

    if (ImGui::Selectable      ("Add Game"))
      AddGamePopup = PopupState::Open;

    btnHovered = ImGui::IsItemHovered() || ImGui::IsItemActive();

    ImGui::PopStyleColor(4);

    ImGui::SetCursorPosY   (fOriginalY - ImGui::GetStyle ().ItemSpacing.y);

    ImGui::EndGroup        ();
  }

  // Stop populating the whole list

  // This ensures the next block gets run when launching SKIF with a last selected item
  SK_RunOnce (update = true);

  // Update the injection strategy for the game
  if ((update && pApp != nullptr) || (updateInjStrat && pApp != nullptr))
  {
    updateInjStrat = false;

    // Handle GOG, EGS, and SKIF Custom games
    if (pApp->store != "Steam")
    {
      DWORD dwBinaryType = MAXDWORD;
      if ( GetBinaryTypeW (pApp->launch_configs[0].getExecutableFullPath(pApp->id).c_str (), &dwBinaryType) )
      {
        if (dwBinaryType == SCS_32BIT_BINARY)
          pApp->specialk.injection.injection.bitness = InjectionBitness::ThirtyTwo;
        else if (dwBinaryType == SCS_64BIT_BINARY)
          pApp->specialk.injection.injection.bitness = InjectionBitness::SixtyFour;
      }

      std::wstring test_paths[] = { 
        pApp->launch_configs[0].getExecutableDir(pApp->id, false),
        pApp->launch_configs[0].working_dir
      };

      if (test_paths[0] == test_paths[1])
        test_paths[1] = L"";

      struct {
        InjectionBitness bitness;
        InjectionPoint   entry_pt;
        std::wstring     name;
        std::wstring     path;
      } test_dlls [] = {
        { pApp->specialk.injection.injection.bitness, InjectionPoint::D3D9,    L"d3d9",     L"" },
        { pApp->specialk.injection.injection.bitness, InjectionPoint::DXGI,    L"dxgi",     L"" },
        { pApp->specialk.injection.injection.bitness, InjectionPoint::D3D11,   L"d3d11",    L"" },
        { pApp->specialk.injection.injection.bitness, InjectionPoint::OpenGL,  L"OpenGL32", L"" },
        { pApp->specialk.injection.injection.bitness, InjectionPoint::DInput8, L"dinput8",  L"" }
      };

      // Assume Global 32-bit if we don't know otherwise
      bool bIs64Bit =
        ( pApp->specialk.injection.injection.bitness ==
                         InjectionBitness::SixtyFour );

      pApp->specialk.injection.config.type =
        ConfigType::Centralized;

      wchar_t                 wszPathToSelf [MAX_PATH] = { };
      GetModuleFileNameW  (0, wszPathToSelf, MAX_PATH);
      PathRemoveFileSpecW (   wszPathToSelf);
      PathAppendW         (   wszPathToSelf,
                                bIs64Bit ? L"SpecialK64.dll"
                                         : L"SpecialK32.dll" );

      pApp->specialk.injection.injection.dll_path = wszPathToSelf;
      pApp->specialk.injection.injection.dll_ver  =
      SKIF_GetSpecialKDLLVersion (       wszPathToSelf);

      pApp->specialk.injection.injection.type =
        InjectionType::Global;
      pApp->specialk.injection.injection.entry_pt =
        InjectionPoint::CBTHook;
      pApp->specialk.injection.config.file =
        L"SpecialK.ini";

      bool breakOuterLoop = false;
      for ( auto& test_path : test_paths)
      {
        if (test_path.empty())
          continue;

        for ( auto& dll : test_dlls )
        {
          dll.path =
            ( test_path + LR"(\)" ) +
             ( dll.name + L".dll" );

          if (PathFileExistsW (dll.path.c_str ()))
          {
            std::wstring dll_ver =
              SKIF_GetSpecialKDLLVersion (dll.path.c_str ());

            if (! dll_ver.empty ())
            {
              pApp->specialk.injection.injection = {
                dll.bitness,
                dll.entry_pt, InjectionType::Local,
                dll.path,     dll_ver
              };

              if (PathFileExistsW ((test_path + LR"(\SpecialK.Central)").c_str ()))
              {
                pApp->specialk.injection.config.type =
                  ConfigType::Centralized;
              }

              else
              {
                pApp->specialk.injection.config = {
                  ConfigType::Localized,
                  test_path
                };
              }

              pApp->specialk.injection.config.file =
                dll.name + L".ini";

              breakOuterLoop = true;
              break;
            }
          }
        }

        if (breakOuterLoop)
          break;
      }

      if (pApp->specialk.injection.config.type == ConfigType::Centralized)
      {
        pApp->specialk.injection.config.dir =
          SK_FormatStringW(LR"(%ws\Profiles\%ws)",
            path_cache.specialk_userdata,
            pApp->specialk.profile_dir.c_str());
      }

      pApp->specialk.injection.config.file =
        ( pApp->specialk.injection.config.dir + LR"(\)" ) +
          pApp->specialk.injection.config.file;

    }

    // Handle Steam games
    else {
      pApp->specialk.injection =
        SKIF_InstallUtils_GetInjectionStrategy (pApp->id);

      // Scan Special K configuration, etc.
      if (pApp->specialk.profile_dir.empty ())
      {
        pApp->specialk.profile_dir = pApp->specialk.injection.config.dir;

        if (! pApp->specialk.profile_dir.empty ())
        {
          SK_VirtualFS profile_vfs;

          int files =
            SK_VFS_ScanTree ( profile_vfs,
                                pApp->specialk.profile_dir.data (), 2 );

          UNREFERENCED_PARAMETER (files);

          //SK_VirtualFS::vfsNode* pFile =
          //  profile_vfs;

          // 4/15/21: Temporarily disable Screenshot Browser, it's not functional enough
          //            to have it distract users yet.
          //
          /////for (const auto& it : pFile->children)
          /////{
          /////  if (it.second->type_ == SK_VirtualFS::vfsNode::type::Directory)
          /////  {
          /////    if (it.second->name.find (LR"(\Screenshots)") != std::wstring::npos)
          /////    {
          /////      for ( const auto& it2 : it.second->children )
          /////      {
          /////        pApp->specialk.screenshots.emplace (
          /////          SK_WideCharToUTF8 (it2.second->getFullPath ())
          /////        );
          /////      }
          /////    }
          /////  }
          /////}
        }
      }
    }
  }


  if (IconMenu == PopupState::Open)
  {
    ImGui::OpenPopup    ("IconMenu");
    IconMenu = PopupState::Closed;
  }

  if (ImGui::BeginPopup ("IconMenu"))
  {
    if (pApp != nullptr)
    {
      // Column 1: Icons

      ImGui::BeginGroup  ( );
      ImVec2 iconPos = ImGui::GetCursorPos();

      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_FILE_IMAGE)       .x, ImGui::GetTextLineHeight()));
      if (pApp->textures.isCustomIcon)
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_UNDO_ALT)         .x, ImGui::GetTextLineHeight()));
      ImGui::PushStyleColor (ImGuiCol_Separator, ImVec4(0, 0, 0, 0));
      ImGui::Separator  (  );
      ImGui::PopStyleColor (  );
      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_EXTERNAL_LINK_ALT).x, ImGui::GetTextLineHeight()));

      ImGui::EndGroup   (  );

      ImGui::SameLine   (  );

      // Column 2: Items
      ImGui::BeginGroup (  );
      bool dontCare = false;
      if (ImGui::Selectable ("Set Custom Icon",    dontCare, ImGuiSelectableFlags_SpanAllColumns))
      {
        LPWSTR pwszFilePath = NULL;
        if (SK_FileOpenDialog(&pwszFilePath, COMDLG_FILTERSPEC{ L"Images", L"*.jpg;*.png;*.ico" }, 1, FOS_FILEMUSTEXIST, FOLDERID_Pictures))
        {
          std::wstring targetPath = L"";
          std::wstring ext        = std::filesystem::path(pwszFilePath).extension().wstring();

          if (pApp->id == SKIF_STEAM_APPID)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\)",           path_cache.specialk_userdata);
          else if (pApp->store == "SKIF")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Custom\%i\)", path_cache.specialk_userdata, pApp->id);
          else if (pApp->store == "EGS")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\EGS\%ws\)",   path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->EGS_AppName).c_str());
          else if (pApp->store == "GOG")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\GOG\%i\)",    path_cache.specialk_userdata, pApp->id);
          else if (pApp->store == "Xbox")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Xbox\%ws\)",  path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->Xbox_PackageName).c_str());
          else if (pApp->store == "Steam")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)",  path_cache.specialk_userdata, pApp->id);

          if (targetPath != L"")
          {
            std::error_code ec;
            // Create any missing directories
            if (! std::filesystem::exists (            targetPath, ec))
                  std::filesystem::create_directories (targetPath, ec);

            targetPath += L"icon";

            DeleteFile ((targetPath + L".png").c_str());
            DeleteFile ((targetPath + L".jpg").c_str());
            DeleteFile ((targetPath + L".ico").c_str());

            CopyFile(pwszFilePath, (targetPath + ext).c_str(), false);
            
            ImVec2 dontCare1, dontCare2;

            // Reload the icon
            LoadLibraryTexture (LibraryTexture::Icon,
                                  pApp->id,
                                    pApp->textures.icon,
                                      (pApp->store == "GOG")
                                      ? pApp->install_dir + L"\\goggame-" + std::to_wstring(pApp->id) + L".ico"
                                      : SK_FormatStringW (LR"(%ws\appcache\librarycache\%i_icon.jpg)", SK_GetSteamDir(), pApp->id), //L"_icon.jpg",
                                          dontCare1,
                                            dontCare2,
                                              pApp );
          }
        }
      }
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
      }

      if (pApp->textures.isCustomIcon)
      {
        if (ImGui::Selectable ("Clear Custom Icon",  dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          std::wstring targetPath = L"";

          if (pApp->id == SKIF_STEAM_APPID)
            targetPath = SK_FormatStringW (LR"(%ws\Assets\)",           path_cache.specialk_userdata);
          else if (pApp->store == "SKIF")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Custom\%i\)", path_cache.specialk_userdata, pApp->id);
          else if (pApp->store == "EGS")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\EGS\%ws\)",   path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->EGS_AppName).c_str());
          else if (pApp->store == "GOG")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\GOG\%i\)",    path_cache.specialk_userdata, pApp->id);
          else if (pApp->store == "Xbox")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Xbox\%ws\)",  path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->Xbox_PackageName).c_str());
          else if (pApp->store == "Steam")
            targetPath = SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)",  path_cache.specialk_userdata, pApp->id);

          if (PathFileExists(targetPath.c_str()))
          {
            targetPath += L"icon";

            bool d1 = DeleteFile ((targetPath + L".png").c_str()),
                 d2 = DeleteFile ((targetPath + L".jpg").c_str()),
                 d3 = DeleteFile ((targetPath + L".ico").c_str());

            // If any file was removed
            if (d1 || d2 || d3)
            {
              ImVec2 dontCare1, dontCare2;

              // Reload the icon
              LoadLibraryTexture (LibraryTexture::Icon,
                                    pApp->id,
                                      pApp->textures.icon,
                                       (pApp->store == "GOG")
                                        ? pApp->install_dir + L"\\goggame-" + std::to_wstring(pApp->id) + L".ico"
                                        : SK_FormatStringW (LR"(%ws\appcache\librarycache\%i_icon.jpg)", SK_GetSteamDir(), pApp->id), //L"_icon.jpg",
                                            dontCare1,
                                              dontCare2,
                                                pApp );
            }
          }
        }
        else
        {
          SKIF_ImGui_SetMouseCursorHand ( );
        }
      }

      ImGui::Separator();

      // Strip (recently added) from the game name
      std::string name = pApp->names.normal;
      try {
        name = std::regex_replace(name, std::regex(R"( \(recently added\))"), "");
      }
      catch (const std::exception& e)
      {
        UNREFERENCED_PARAMETER(e);
      }

      std::string linkGridDB = (pApp->store == "Steam")
                             ? SK_FormatString("https://www.steamgriddb.com/steam/%lu/icons", pApp->id)
                             : SK_FormatString("https://www.steamgriddb.com/search/icons?term=%s", name.c_str());

      if (ImGui::Selectable ("Browse SteamGridDB",   dontCare, ImGuiSelectableFlags_SpanAllColumns))
      {
        SKIF_Util_OpenURI   (SK_UTF8ToWideChar(linkGridDB).c_str());
      }
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
        SKIF_ImGui_SetHoverText       (linkGridDB);
      }

      ImGui::EndGroup   (  );

      ImGui::SetCursorPos (iconPos);

      ImGui::TextColored (
          (_registry.iStyle == 2) ? ImColor (0, 0, 0) : ImColor (255, 255, 255),
                ICON_FA_FILE_IMAGE
                            );

      if (pApp->textures.isCustomIcon)
        ImGui::TextColored (
          (_registry.iStyle == 2) ? ImColor (0, 0, 0) : ImColor (255, 255, 255),
                  ICON_FA_UNDO_ALT
                              );

      ImGui::Separator   ( );

      ImGui::TextColored (
              ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                ICON_FA_EXTERNAL_LINK_ALT
                            );
    }

    ImGui::EndPopup      ( );
  }

  ImGui::EndGroup        ( );
  ImGui::EndChild        ( );
  ImGui::PopStyleColor   ( );

  if (! ImGui::IsAnyPopupOpen   ( ) &&
      ! ImGui::IsAnyItemHovered ( ) &&
        ImGui::IsItemClicked    (ImGuiMouseButton_Right))
  {
    ImGui::OpenPopup      ("GameListEmptySpaceMenu");
  }

  if (ImGui::BeginPopup   ("GameListEmptySpaceMenu"))
  {
    bool dontCare = false;
    
    ImGui::BeginGroup     ( );
    ImVec2 iconPos = ImGui::GetCursorPos();
    ImGui::ItemSize       (ImVec2 (ImGui::CalcTextSize (ICON_FA_PLUS_SQUARE).x, ImGui::GetTextLineHeight()));
    ImGui::PushStyleColor (ImGuiCol_Separator, ImVec4(0, 0, 0, 0));
    ImGui::Separator      ( );
    ImGui::PopStyleColor  ( );
    ImGui::ItemSize       (ImVec2 (ImGui::CalcTextSize (ICON_FA_REDO).x, ImGui::GetTextLineHeight()));
    ImGui::EndGroup       ( );

    ImGui::SameLine       ( );

    ImGui::BeginGroup     ( );
     if (ImGui::Selectable ("Add Game", dontCare, ImGuiSelectableFlags_SpanAllColumns))
       AddGamePopup = PopupState::Open;
    ImGui::PushStyleColor (ImGuiCol_Separator, ImVec4(0, 0, 0, 0));
    ImGui::Separator      ( );
    ImGui::PopStyleColor  ( );
    if (ImGui::Selectable ("Refresh",  dontCare, ImGuiSelectableFlags_SpanAllColumns))
      RepopulateGames = true;
    ImGui::EndGroup       ( );

    ImGui::SetCursorPos   (iconPos);
    ImGui::Text           (ICON_FA_PLUS_SQUARE);
    ImGui::Separator      ( );
    ImGui::Text           (ICON_FA_REDO);
    ImGui::EndPopup       ( );
  }

  // Applies hover text on the whole AppListInset1
  //if (SKIF_StatusBarText.empty ()) // Prevents the text from overriding the keyboard search hint
    //SKIF_ImGui_SetHoverText ("Right click for more options");

  ImGui::BeginChild (
    "###AppListInset2",
      ImVec2 ( _WIDTH2,
               _HEIGHT2 ), (! _registry.bDisableBorders),
        ImGuiWindowFlags_NoScrollbar       |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NavFlattened      |
        ImGuiWindowFlags_AlwaysUseWindowPadding
  );
  ImGui::BeginGroup ();

  if ( pApp     == nullptr       ||
       pApp->id == SKIF_STEAM_APPID )
  {
    _inject._GlobalInjectionCtl ();
  }

  else if (pApp != nullptr)
  {
    _PrintInjectionSummary (pApp);

    if ( pApp->extended_config.vac.enabled == 1 )
    {
        SKIF_StatusBarText = "Warning: ";
        SKIF_StatusBarHelp = "VAC protected game - Injection is not recommended!";
    }

    if ( pApp->specialk.injection.injection.type != InjectionType::Local )
    {
      ImGui::SetCursorPosY (
        ImGui::GetWindowHeight () - fBottomDist -
        ImGui::GetStyle        ().ItemSpacing.y
      );

      ImGui::Separator     ( );

      SKIF_ImGui_BeginChildFrame  ( ImGui::GetID ("###launch_cfg"),
                                    ImVec2 (ImGui::GetContentRegionAvail ().x,
                                  std::max (ImGui::GetContentRegionAvail ().y,
                                            ImGui::GetTextLineHeight () + ImGui::GetStyle ().FramePadding.y * 2.0f + ImGui::GetStyle ().ItemSpacing.y * 2
                                           )),
                                    ImGuiWindowFlags_NavFlattened      |
                                    ImGuiWindowFlags_NoScrollbar       |
                                    ImGuiWindowFlags_NoScrollWithMouse |
                                    ImGuiWindowFlags_NoBackground
      );

      auto _BlacklistCfg =
      [&](app_record_s::launch_config_s& launch_cfg, bool menu = false) ->
      void
      {
        /*
        if (pApp->extended_config.vac.enabled == 1)
        {
          launch_cfg.setBlacklisted (pApp->id, true);
        }
        */

        bool blacklist =
          launch_cfg.isBlacklisted (pApp->id);
          //|| _inject._TestUserList(SK_WideCharToUTF8(launch_cfg.getExecutableFullPath(pApp->id)).c_str(), false);

        char          szButtonLabel [256] = { };
        if (menu)
        {
          sprintf_s ( szButtonLabel, 255,
                        " for \"%ws\"###DisableLaunch%d",
                          launch_cfg.description.empty ()
                            ? launch_cfg.executable .c_str ()
                            : launch_cfg.description.c_str (),
                          launch_cfg.id);
          
          if (ImGui::Checkbox (szButtonLabel,   &blacklist))
            launch_cfg.setBlacklisted (pApp->id, blacklist);

          SKIF_ImGui_SetHoverText (
                      SK_FormatString (
                        menu
                          ? R"(%ws    )"
                          : R"(%ws)",
                        launch_cfg.executable.c_str  ()
                      ).c_str ()
          );
        }
        else
        {
          sprintf_s ( szButtonLabel, 255,
                        " Disable Special K###DisableLaunch%d",
                          launch_cfg.id );
          
          if (ImGui::Checkbox (szButtonLabel,   &blacklist))
            launch_cfg.setBlacklisted (pApp->id, blacklist);
         
          /*
          if (ImGui::Checkbox (szButtonLabel,   &blacklist))
            _inject._BlacklistBasedOnPath(SK_WideCharToUTF8(launch_cfg.getExecutableFullPath(pApp->id)));
          */
        }
      };

      if ( ! _inject.bHasServlet )
      {
        ImGui::TextColored    (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), "����ȱ���ļ������񲻿���.");
      }

      else if ( ! pApp->launch_configs.empty() )
      {
        bool elevate =
          pApp->launch_configs[0].isElevated (pApp->id);

        if (ImGui::Checkbox (u8"��������Ȩ�� (beta)###ElevatedLaunch",   &elevate))
          pApp->launch_configs[0].setElevated (pApp->id, elevate);

        ImGui::SameLine ( );

        // Set horizontal position
        ImGui::SetCursorPosX (
          ImGui::GetCursorPosX  () +
          ImGui::GetColumnWidth () -
          ImGui::CalcTextSize   ("Disable Special K >").x -
          ImGui::GetScrollX     () -
          ImGui::GetStyle       ().ItemSpacing.x * 2
        );

        // If there is only one launch option
        if ( pApp->launch_configs.size  () == 1 )
        {
          ImGui::SetCursorPosY (
            ImGui::GetCursorPosY () - 1.0f
          );

          // Only if it is a valid one
          if ( pApp->launch_configs.begin()->second.valid)
          {
            _BlacklistCfg          (
                 pApp->launch_configs.begin ()->second );
          }
        }

        // If there are more than one launch option
        else
        {
          ImGui::SetCursorPosY (
            ImGui::GetCursorPosY () +
            ImGui::GetStyle      ().ItemSpacing.y / 2.0f
          );

          if (ImGui::BeginMenu ("Disable Special K"))
          {
            std::set <std::wstring> _used_launches;
            for ( auto& launch : pApp->launch_configs )
            {
              // TODO: Secondary-Launch-Options: Need to ensure launch options that share an executable only gets listed once.
              if (! launch.second.valid ) /* ||
                  ! _used_launches.emplace (launch.second.blacklist_file).second ) */
                continue;

              _BlacklistCfg (launch.second, true);
            }

            ImGui::EndMenu       ();
          }
        }
      }

      ImGui::EndChildFrame     ();

      fBottomDist = ImGui::GetItemRectSize().y;
    }
  }

  ImGui::EndGroup     (                  );
  ImGui::EndChild     (                  );
  ImGui::EndGroup     (                  );

  

  // Special handling at the bottom for Special K
  if ( selection.appid == SKIF_STEAM_APPID ) {
    ImGui::SetCursorPos  (                           ImVec2 ( fX + ImGui::GetStyle().FrameBorderSize,
                                                              fY - floorf((204.f * SKIF_ImGui_GlobalDPIScale) + ImGui::GetStyle().FrameBorderSize) ));
    ImGui::BeginGroup    ();
    static bool hoveredPatButton  = false,
                hoveredPatCredits = false;

    // Set all button styling to transparent
    ImGui::PushStyleColor (ImGuiCol_Button,        ImVec4 (0, 0, 0, 0));
    ImGui::PushStyleColor (ImGuiCol_ButtonActive,  ImVec4 (0, 0, 0, 0));
    ImGui::PushStyleColor (ImGuiCol_ButtonHovered, ImVec4 (0, 0, 0, 0));

    // Remove frame border
    ImGui::PushStyleVar (ImGuiStyleVar_FrameBorderSize, 0.0f);

    bool        clicked =
    ImGui::ImageButton   ((ImTextureID)pPatTexSRV.p, ImVec2 (200.0F * SKIF_ImGui_GlobalDPIScale,
                                                             200.0F * SKIF_ImGui_GlobalDPIScale),
                                                     ImVec2 (0.f,       0.f),
                                                     ImVec2 (1.f,       1.f),     0,
                                                     ImVec4 (0, 0, 0, 0), // Use a transparent background
                                  hoveredPatButton ? ImVec4 (  1.f,  1.f,  1.f, 1.0f)
                                                   : ImVec4 (  .8f,  .8f,  .8f, .66f));

    // Restore frame border
    ImGui::PopStyleVar   ( );

    // Restore the custom button styling
    ImGui::PopStyleColor (3);

    hoveredPatButton =
    ImGui::IsItemHovered ( );

    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText ("https://www.patreon.com/Kaldaien");
    //SKIF_ImGui_SetHoverTip  ("Click to help support the project");

    if (clicked)
      SKIF_Util_OpenURI (
        L"https://www.patreon.com/Kaldaien"
      );

    ImGui::SetCursorPos  (ImVec2 (fZ - (238.0f * SKIF_ImGui_GlobalDPIScale),
                                  fY - (204.0f * SKIF_ImGui_GlobalDPIScale)) );

    ImGui::PushStyleColor     (ImGuiCol_ChildBg,        hoveredPatCredits ? ImGui::GetStyleColorVec4(ImGuiCol_WindowBg)
                                                                          : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg) * ImVec4(.8f, .8f, .8f, .66f));
    ImGui::BeginChild         ("###PatronsChild", ImVec2 (230.0f * SKIF_ImGui_GlobalDPIScale,
                                                          200.0f * SKIF_ImGui_GlobalDPIScale),
                                                                      (! _registry.bDisableBorders),
                                                      ImGuiWindowFlags_NoScrollbar            |
                                                      ImGuiWindowFlags_AlwaysUseWindowPadding |
                        ((pApp->textures.isCustomCover || _registry.iStyle == 2) ? 0x0 : ImGuiWindowFlags_NoBackground));

    ImGui::TextColored        (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption) * ImVec4 (0.8f, 0.8f, 0.8f, 1.0f), u8"�ر�������ǵ�������:");

    std::string patrons_ = SKIF_GetPatrons ( );

    ImGui::Spacing            ( );
    ImGui::SameLine           ( );
    ImGui::Spacing            ( );
    ImGui::SameLine           ( );
    
    ImGui::PushStyleVar       (ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor     (ImGuiCol_Text,           ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4  (0.6f, 0.6f, 0.6f, 1.0f));
    ImGui::PushStyleColor     (ImGuiCol_FrameBg,        ImColor (0, 0, 0, 0).Value);
    ImGui::PushStyleColor     (ImGuiCol_ScrollbarBg,    ImColor (0, 0, 0, 0).Value);
    ImGui::PushStyleColor     (ImGuiCol_TextSelectedBg, ImColor (0, 0, 0, 0).Value);
    ImGui::InputTextMultiline ("###Patrons", patrons_.data (), patrons_.length (),
                   ImVec2 (205.0f * SKIF_ImGui_GlobalDPIScale,
                           160.0f * SKIF_ImGui_GlobalDPIScale),
                                    ImGuiInputTextFlags_ReadOnly );
    ImGui::PopStyleColor      (4);
    ImGui::PopStyleVar        ( );

    hoveredPatCredits =
    ImGui::IsItemActive();

    ImGui::EndChild           ( );
    ImGui::PopStyleColor      ( );

    hoveredPatCredits = hoveredPatCredits ||
    ImGui::IsItemHovered      ( );

    ImGui::EndGroup           ( );
  }

  // Refresh running state of SKIF Custom, EGS, GOG, and Xbox titles
  static DWORD lastGameRefresh = 0;

  if (SKIF_Util_timeGetTime() > lastGameRefresh + 5000 && (! ImGui::IsAnyMouseDown ( ) || ! SKIF_ImGui_IsFocused ( )))
  {
    for (auto& app : apps)
    {
      if (app.second.store == "Steam")
        continue;
      app.second._status.running = false;
    }

    PROCESSENTRY32W none = { },
                    pe32 = { };

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
          std::wstring fullPath;

          SetLastError (NO_ERROR);
          CHandle hProcess (OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe32.th32ProcessID));
          // Use PROCESS_QUERY_LIMITED_INFORMATION since that allows us to retrieve exit code/full process name for elevated processes

          bool accessDenied =
            GetLastError ( ) == ERROR_ACCESS_DENIED;

          // Get exit code to filter out zombie processes
          DWORD dwExitCode = 0;
          GetExitCodeProcess (hProcess, &dwExitCode);

          if (! accessDenied)
          {
            // If the process is not active any longer, skip it (terminated or zombie process)
            if (dwExitCode != STILL_ACTIVE)
              continue;

            WCHAR szExePath[MAX_PATH];
            DWORD len = MAX_PATH;

            // See if we can retrieve the full path of the executable
            if (QueryFullProcessImageName (hProcess, 0, szExePath, &len))
              fullPath = std::wstring (szExePath);
          }

          for (auto& app : apps)
          {
            // Steam games are covered through separate registry monitoring
            if (app.second.store == "Steam")
            {
              continue;
            }

            // Workaround for Xbox games that run under the virtual folder, e.g. H:\Games\Xbox Games\Hades\Content\Hades.exe
            else if (app.second.store == "Xbox" && (! wcscmp (pe32.szExeFile, app.second.launch_configs[0].executable.c_str())))
            {
              app.second._status.running = true;
              break;
            }

            // EGS, GOG and SKIF Custom should be straight forward
            else if (fullPath == app.second.launch_configs[0].getExecutableFullPath(app.second.id, false)) // full patch
            {
              app.second._status.running = true;
              break;

              // One can also perform a partial match with the below OR clause in the IF statement, however from testing
              //   PROCESS_QUERY_LIMITED_INFORMATION gives us GetExitCodeProcess() and QueryFullProcessImageName() rights
              //     even to elevated processes, meaning the below OR clause is unnecessary.
              // 
              // (fullPath.empty() && ! wcscmp (pe32.szExeFile, app.second.launch_configs[0].executable.c_str()))
              //
            }
          }

        } while (Process32NextW (hProcessSnap, &pe32));
      }
    }

    lastGameRefresh = SKIF_Util_timeGetTime();
  }


  extern void SKIF_ImGui_ServiceMenu (void);

  SKIF_ImGui_ServiceMenu ( );



  if (openedGameContextMenu)
  {
    ImGui::OpenPopup    ("GameContextMenu");
    openedGameContextMenu = false;
  }


  if (ImGui::BeginPopup ("GameContextMenu"))
  {
    if (pApp != nullptr)
    {
      // Hide the Launch option for Special K
      if (pApp->id != SKIF_STEAM_APPID)
      {
        if ( ImGui::Selectable (("Launch " + pApp->type).c_str (), false,
                               ((pApp->_status.running != 0x0)
                                 ? ImGuiSelectableFlags_Disabled
                                 : ImGuiSelectableFlags_None)))
          clickedGameLaunch = true;

        if (pApp->specialk.injection.injection.type != sk_install_state_s::Injection::Type::Local)
        {
          if (! _inject.bCurrentState)
            SKIF_ImGui_SetHoverText ("Starts the global injection service as well.");

          ImGui::PushStyleColor  ( ImGuiCol_Text,
            ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4(1.0f, 1.0f, 1.0f, 0.7f)
          );

          if ( ImGui::Selectable (("Launch " + pApp->type + " without Special K").c_str(), false,
                                 ((pApp->_status.running != 0x0)
                                   ? ImGuiSelectableFlags_Disabled
                                   : ImGuiSelectableFlags_None)))
            clickedGameLaunchWoSK = true;

          ImGui::PopStyleColor   ( );
          if (_inject.bCurrentState)
            SKIF_ImGui_SetHoverText ("Stops the global injection service as well.");
        }

        extern std::wstring GOGGalaxy_Path;
        extern bool GOGGalaxy_Installed;

        if (GOGGalaxy_Installed && pApp->store == "GOG")
        {
          if (pApp->specialk.injection.injection.type != sk_install_state_s::Injection::Type::Local)
          {
            ImGui::Separator        ( );

            if (ImGui::BeginMenu    ("Launch using GOG Galaxy"))
            {
              if (ImGui::Selectable (("Launch " + pApp->type).c_str(), false,
                                    ((pApp->_status.running != 0x0)
                                      ? ImGuiSelectableFlags_Disabled
                                      : ImGuiSelectableFlags_None)))
                clickedGalaxyLaunch = true;

              ImGui::PushStyleColor ( ImGuiCol_Text,
                ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4(1.0f, 1.0f, 1.0f, 0.7f)
              );

              if (ImGui::Selectable (("Launch " + pApp->type + " without Special K").c_str(), false,
                                    ((pApp->_status.running != 0x0)
                                      ? ImGuiSelectableFlags_Disabled
                                      : ImGuiSelectableFlags_None)))
                clickedGalaxyLaunchWoSK = true;

              ImGui::PopStyleColor  ( );
              ImGui::EndMenu        ( );
            }
          }

          else {
            if (ImGui::Selectable   ("Launch using GOG Galaxy", false,
                                    ((pApp->_status.running != 0x0)
                                      ? ImGuiSelectableFlags_Disabled
                                      : ImGuiSelectableFlags_None)))
              clickedGalaxyLaunch = true;
          }

          if (clickedGalaxyLaunch ||
              clickedGalaxyLaunchWoSK)
          {
            // Launch preparations for Global
            if (pApp->specialk.injection.injection.type != sk_install_state_s::Injection::Type::Local)
            {
              std::string fullPath = SK_WideCharToUTF8 (pApp->launch_configs[0].getExecutableFullPath(pApp->id));
              bool isLocalBlacklisted  = pApp->launch_configs[0].isBlacklisted (pApp->id),
                   isGlobalBlacklisted = _inject._TestUserList (fullPath.c_str (), false);

              if (! clickedGalaxyLaunchWoSK &&
                  ! isLocalBlacklisted      &&
                  ! isGlobalBlacklisted
                 )
              {
                // Whitelist the path if it haven't been already
                if (_inject._WhitelistBasedOnPath (fullPath))
                  _inject._StoreList (true);
              }

              // Kickstart service if it is currently not running
              if (! _inject.bCurrentState && ! clickedGalaxyLaunchWoSK && ! isLocalBlacklisted && ! isGlobalBlacklisted )
                _inject._StartStopInject (false, true, pApp->launch_configs[0].isElevated(pApp->id));

              // Stop the service if the user attempts to launch without SK
              else if (clickedGalaxyLaunchWoSK && _inject.bCurrentState)
                _inject._StartStopInject (true);
            }

            // "D:\Games\GOG Galaxy\GalaxyClient.exe" /command=runGame /gameId=1895572517 /path="D:\Games\GOG Games\AI War 2"

            std::wstring launchOptions = SK_FormatStringW(LR"(/command=runGame /gameId=%d /path="%ws")", pApp->id, pApp->install_dir.c_str());

            SHELLEXECUTEINFOW
            sexi              = { };
            sexi.cbSize       = sizeof (SHELLEXECUTEINFOW);
            sexi.lpVerb       = L"OPEN";
            sexi.lpFile       = GOGGalaxy_Path.c_str();
            sexi.lpParameters = launchOptions.c_str();
          //sexi.lpDirectory  = NULL;
            sexi.nShow        = SW_SHOWDEFAULT;
            sexi.fMask        = SEE_MASK_FLAG_NO_UI |
                                SEE_MASK_ASYNCOK    | SEE_MASK_NOZONECHECKS;

            ShellExecuteExW (&sexi);

            if (_registry.bMinimizeOnGameLaunch)
              SKIF_bSuppressServiceNotification = ShowWindow (SKIF_hWnd, SW_MINIMIZE) == TRUE;

            clickedGalaxyLaunch = clickedGalaxyLaunchWoSK = false;
          }
        }
      }

      // Special K is selected -- relevant show quick links
      else
      {
        ImGui::BeginGroup  ( );
        ImVec2 iconPos = ImGui::GetCursorPos();

        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_BOOK_OPEN).x, ImGui::GetTextLineHeight()));
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_DISCORD)  .x, ImGui::GetTextLineHeight()));
        ImGui::PushStyleColor (ImGuiCol_Separator, ImVec4(0, 0, 0, 0));
        ImGui::Separator  (  );
        ImGui::PopStyleColor (  );
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_DISCOURSE).x, ImGui::GetTextLineHeight()));
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_PATREON)  .x, ImGui::GetTextLineHeight()));
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_GITHUB)   .x, ImGui::GetTextLineHeight()));

        ImGui::EndGroup   (  );
        ImGui::SameLine   (  );
        ImGui::BeginGroup (  );
        bool dontCare = false;

        if (ImGui::Selectable ("Wiki", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          SKIF_Util_OpenURI (
            L"https://wiki.special-k.info/"
          );
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       ("https://wiki.special-k.info/");


        if (ImGui::Selectable ("Discord", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          SKIF_Util_OpenURI (
            L"https://discord.gg/specialk"
          );
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       ("https://discord.gg/specialk");


        if (ImGui::Selectable ("Forum", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          SKIF_Util_OpenURI (
            L"https://discourse.differentk.fyi/"
          );
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       ("https://discourse.differentk.fyi/");


        if (ImGui::Selectable ("Patreon", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          SKIF_Util_OpenURI (
            L"https://www.patreon.com/Kaldaien"
          );
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       ("https://www.patreon.com/Kaldaien");

        if (ImGui::Selectable ("GitHub", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          SKIF_Util_OpenURI (
            L"https://github.com/SpecialKO"
          );
        }
        SKIF_ImGui_SetMouseCursorHand ();
        SKIF_ImGui_SetHoverText       ("https://github.com/SpecialKO");

        ImGui::EndGroup   ( );

        ImGui::SetCursorPos(iconPos);

        ImGui::TextColored (
               ImColor   (25, 118, 210),
                 ICON_FA_BOOK
                             );
        ImGui::TextColored (
               ImColor   (114, 137, 218),
                 ICON_FA_DISCORD
                             );
        ImGui::TextColored (
               (_registry.iStyle == 2) ? ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow) : ImVec4 (ImColor (247, 241, 169)),
                 ICON_FA_DISCOURSE
                             );
        ImGui::TextColored (
               ImColor   (249, 104,  84),
                 ICON_FA_PATREON
                             );
        ImGui::TextColored (
               (_registry.iStyle == 2) ? ImColor (0, 0, 0) : ImColor (255, 255, 255), // ImColor (226, 67, 40)
                 ICON_FA_GITHUB
                             );
      }

      if (pApp->store == "Steam" && (pApp->id != SKIF_STEAM_APPID ||
                                    (pApp->id == SKIF_STEAM_APPID && SKIF_STEAM_OWNER)))
        ImGui::Separator  ( );

      /*
      if (! pApp->specialk.screenshots.empty ())
      {
        if (ImGui::BeginMenu ("Screenshots"))
        {
          for (auto& screenshot : pApp->specialk.screenshots)
          {
            if (ImGui::Selectable (screenshot.c_str ()))
            {
              SKIF_GameManagement_ShowScreenshot (
                SK_UTF8ToWideChar (screenshot)
              );
            }

            SKIF_ImGui_SetMouseCursorHand ();
            SKIF_ImGui_SetHoverText       (screenshot.c_str ());
          }

          ImGui::EndMenu ();
        }
      }
      */

      if (! pApp->cloud_saves.empty ())
      {
        bool bMenuOpen =
          ImGui::BeginMenu (ICON_FA_SD_CARD "   Game Saves and Config");

        std::set <std::wstring> used_paths_;

        if (bMenuOpen)
        {
          if (! pApp->cloud_enabled)
          {
            ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),
                                   ICON_FA_EXCLAMATION_TRIANGLE " Auto-Cloud is not enabled" );
            ImGui::Separator   ( );
          }

          bool bCloudSaves = false;

          for (auto& cloud : pApp->cloud_saves)
          {
            if (! cloud.second.valid)
              continue;

            if ( app_record_s::Platform::Unknown == cloud.second.platforms ||
                 app_record_s::supports (           cloud.second.platforms,
                 app_record_s::Platform::Windows )
               )
            {
              wchar_t sel_path [MAX_PATH    ] = { };
              char    label    [MAX_PATH * 2] = { };

              swprintf ( sel_path, MAX_PATH, L"%ws",
                           cloud.second.evaluated_dir.c_str () );

              sprintf ( label, "%ws###CloudUFS.%d", sel_path,
                                     cloud.first );

              bool selected = false;

              if (used_paths_.emplace (sel_path).second)
              {
                if (ImGui::Selectable (label, &selected))
                {
                  SKIF_Util_ExplorePath (sel_path);

                  ImGui::CloseCurrentPopup ();
                }
                SKIF_ImGui_SetMouseCursorHand ();
                SKIF_ImGui_SetHoverText       (
                             SK_FormatString ( R"(%ws)",
                                        cloud.second.evaluated_dir.c_str ()
                                             ).c_str ()
                                              );
                bCloudSaves = true;
              }
            }
          }

          if (! bCloudSaves)
          {
            ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase), "No locations could be found.");
          }

          ImGui::EndMenu ();

        //SKIF_ImGui_SetHoverTip ("Browse files cloud-sync'd by Steam");
        }
      }

      if (! pApp->branches.empty ())
      {
        bool bMenuOpen =
          ImGui::BeginMenu (ICON_FA_CODE_BRANCH "   Software Branches");

        static
          std::set  < std::string >
                      used_branches_;

        using branch_ptr_t =
          std::pair <          std::string*,
             app_record_s::branch_record_s* >;

        static
          std::multimap <
            int64_t, branch_ptr_t
          > branches;

        // Clear the cache when changing selection
        if ( (! branches.empty ()) &&
                branches.begin ()->second.second->parent != pApp )
        {
          branches.clear       ();
          used_branches_.clear ();
        }

        if (bMenuOpen)
        {
          if (branches.empty ())
          {
            for ( auto& it : pApp->branches )
            {
              if (used_branches_.emplace (it.first).second)
              {
                auto& branch =
                  it.second;

                // Sort in descending order
                branches.emplace (
                  std::make_pair   (-(int64_t)branch.build_id,
                    std::make_pair (
                      const_cast <std::string                   *> (&it.first),
                      const_cast <app_record_s::branch_record_s *> (&it.second)
                    )
                  )
                );
              }
            }
          }

          for ( auto& it : branches )
          {
            auto& branch_name =
                  *(it.second.first);

            auto& branch =
                  *(it.second.second);

            ImGui::PushStyleColor (
              ImGuiCol_Text, branch.pwd_required ?
                               ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4(1.0f, 1.0f, 1.0f, 0.7f)
                                                 :
                               ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
            );

            bool bExpand =
              ImGui::BeginMenu (branch_name.c_str ());

            ImGui::PopStyleColor ();

            if (bExpand)
            {
              if (! branch.description.empty ())
              {
                ImGui::MenuItem ( "Description",
                            branch.getDescAsUTF8 ().c_str () );
              }

              ImGui::MenuItem ( "App Build #",
                                  std::to_string (
                                                  branch.build_id
                                                 ).c_str ()
              );

              if (branch.time_updated > 0)
              {
                ImGui::MenuItem ( "Last Update", branch.getTimeAsCStr ().c_str () );
              }

              ImGui::MenuItem ( "Accessibility", branch.pwd_required ?
                                       "Private (Password Required)" :
                                              "Public (No Password)" );

              ImGui::EndMenu ();
            }
          }

          ImGui::EndMenu ();
        }
      }

      // Manage [Custom] Game
      if (pApp->store == "SKIF" || pApp->store == "GOG")
      {
        ImGui::Separator ( );

        if (ImGui::BeginMenu ("Manage"))
        {
          if (pApp->store == "SKIF")
          {
            if (ImGui::Selectable ("Properties"))
              ModifyGamePopup = PopupState::Open;

            ImGui::Separator ( );
          }

          if (ImGui::Selectable ("Create shortcut"))
          {
            std::string name = pApp->names.normal;

            // Strip (recently added) from the desktop shortcuts
            try {
              name = std::regex_replace(name, std::regex(R"( \(recently added\))"), "");
            }
            catch (const std::exception& e)
            {
              UNREFERENCED_PARAMETER(e);
            }

            /* Old method
            // Strip invalid filename characters
            //const std::string forbidden = "\\/:?\"<>|";
            //std::transform(name.begin(), name.end(), name.begin(), [&forbidden](char c) { return forbidden.find(c) != std::string::npos ? ' ' : c; });

            // Remove double spaces
            //name = std::regex_replace(name, std::regex(R"(  )"), " ");
            */

            name = SKIF_Util_StripInvalidFilenameChars (name);

            std::wstring linkPath = SK_FormatStringW (LR"(%ws\%ws.lnk)", std::wstring(path_cache.desktop.path).c_str(), SK_UTF8ToWideChar(name).c_str());
            std::wstring linkArgs = SK_FormatStringW (LR"("%ws" %ws)", pApp->launch_configs[0].getExecutableFullPath(pApp->id).c_str(), pApp->launch_configs[0].launch_options.c_str());
            wchar_t wszPath[MAX_PATH + 2] = { };
            GetModuleFileNameW (hModSKIF, wszPath, MAX_PATH);

            confirmPopupTitle = "Create Shortcut";

            if (SKIF_Util_CreateShortcut (
                linkPath.c_str(),
                wszPath,
                linkArgs.c_str(),
                pApp->launch_configs[0].working_dir.c_str(),
                SK_UTF8ToWideChar(name).c_str(),
                pApp->launch_configs[0].getExecutableFullPath(pApp->id).c_str()
                )
              )
              confirmPopupText = "A desktop shortcut has been created.";
            else
              confirmPopupText = "Failed to create a desktop shortcut!";

            ConfirmPopup = PopupState::Open;
          }

          if (pApp->store == "SKIF")
          {
            if (ImGui::Selectable ("Remove"))
              RemoveGamePopup = PopupState::Open;
          }

          ImGui::EndMenu ( );
        }
      }

      ImGui::PushStyleColor ( ImGuiCol_Text,
        ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase) * ImVec4(1.0f, 1.0f, 1.0f, 0.7f) //(ImVec4)ImColor::HSV (0.0f, 0.0f, 0.75f)
      );

      ImGui::Separator ( );

      ImGui::BeginGroup  ( );
      ImVec2 iconPos = ImGui::GetCursorPos();

      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_FOLDER_OPEN) .x, ImGui::GetTextLineHeight()));
      ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_TOOLS)       .x, ImGui::GetTextLineHeight()));

      if (pApp->store == "GOG")
      {
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_DATABASE)    .x, ImGui::GetTextLineHeight()));
      }

      else if (pApp->store == "Steam" && (pApp->id != SKIF_STEAM_APPID ||
                                         (pApp->id == SKIF_STEAM_APPID && SKIF_STEAM_OWNER)))
      {
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_STEAM_SYMBOL).x, ImGui::GetTextLineHeight()));
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_STEAM_SYMBOL).x, ImGui::GetTextLineHeight()));
        ImGui::ItemSize   (ImVec2 (ImGui::CalcTextSize (ICON_FA_DATABASE)    .x, ImGui::GetTextLineHeight()));
      }
      ImGui::EndGroup    ( );
      ImGui::SameLine    ( );
      ImGui::BeginGroup  ( );
      bool dontCare = false;
      if (ImGui::Selectable ("Browse Install Folder", dontCare, ImGuiSelectableFlags_SpanAllColumns))
      {
        SKIF_Util_ExplorePath (pApp->install_dir);
      }
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
        SKIF_ImGui_SetHoverText       (
          SK_WideCharToUTF8 (pApp->install_dir)
                                        );
      }

      std::wstring pcgwLink =
        (pApp->store == "GOG") ? L"http://www.pcgamingwiki.com/api/gog.php?page=%ws"
                               : (pApp->store == "Steam") ? L"http://www.pcgamingwiki.com/api/appid.php?appid=%ws"
                                                          : L"https://www.pcgamingwiki.com/w/index.php?search=%ws";
      std::wstring pcgwValue =
        (pApp->store == "SKIF" || pApp->store == "EGS" || pApp->store == "Xbox")
                                ? SK_UTF8ToWideChar (pApp->names.normal)
                                : std::to_wstring(pApp->id);

      if (ImGui::Selectable  ("Browse PCGamingWiki", dontCare, ImGuiSelectableFlags_SpanAllColumns))
      {
        SKIF_Util_OpenURI_Formatted ( SW_SHOWNORMAL, pcgwLink.c_str(), pcgwValue.c_str() );
      }
      else
      {
        SKIF_ImGui_SetMouseCursorHand ( );
        SKIF_ImGui_SetHoverText       (
          SK_WideCharToUTF8 (
            SK_FormatStringW ( pcgwLink.c_str(), pcgwValue.c_str() )
          ).c_str()
        );
      }

      if (pApp->store == "GOG")
      {
        if (ImGui::Selectable  ("Browse GOG Database", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          SKIF_Util_OpenURI_Formatted ( SW_SHOWNORMAL,
            L"https://www.gogdb.org/product/%lu", pApp->id
          );
        }
        else
        {
          SKIF_ImGui_SetMouseCursorHand ( );
          SKIF_ImGui_SetHoverText       (
            SK_FormatString (
              "https://www.gogdb.org/product/%lu", pApp->id
            )
          );
        }
      }
      else if (pApp->store == "Steam" && (pApp->id != SKIF_STEAM_APPID ||
                                         (pApp->id == SKIF_STEAM_APPID && SKIF_STEAM_OWNER)))
      {
        if (ImGui::Selectable  ("Browse Steam", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          SKIF_Util_OpenURI_Formatted ( SW_SHOWNORMAL,
            L"steam://nav/games/details/%lu", pApp->id
                                        );
        }
        else
        {
          SKIF_ImGui_SetMouseCursorHand ( );
          SKIF_ImGui_SetHoverText       (
            SK_FormatString (
              "steam://nav/games/details/%lu", pApp->id
                            )
                                          );
        }

        if (ImGui::Selectable  ("Browse Steam Community", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          SKIF_Util_OpenURI_Formatted ( SW_SHOWNORMAL,
            L"https://steamcommunity.com/app/%lu", pApp->id
                                        );
        }
        else
        {
          SKIF_ImGui_SetMouseCursorHand ( );
          SKIF_ImGui_SetHoverText       (
            SK_FormatString (
              "https://steamcommunity.com/app/%lu", pApp->id
                            )
                                          );
        }

        if (ImGui::Selectable  ("Browse SteamDB", dontCare, ImGuiSelectableFlags_SpanAllColumns))
        {
          SKIF_Util_OpenURI_Formatted ( SW_SHOWNORMAL,
            L"https://steamdb.info/app/%lu", pApp->id
                                        );
        }
        else
        {
          SKIF_ImGui_SetMouseCursorHand ( );
          SKIF_ImGui_SetHoverText       (
            SK_FormatString (
              "https://steamdb.info/app/%lu", pApp->id
                            )
                                          );
        }
      }
      ImGui::EndGroup      ( );
      ImGui::PopStyleColor ( );

      ImGui::SetCursorPos  (iconPos);

      ImGui::TextColored (
               ImColor (255, 207, 72),
                 ICON_FA_FOLDER_OPEN
                           );
      ImGui::TextColored (
               ImColor   (200, 200, 200, 255),
                 ICON_FA_TOOLS
                           );

      if (pApp->store == "GOG")
      {
        ImGui::TextColored (
         ImColor   (155, 89, 182, 255),
           ICON_FA_DATABASE );
      }

      else if (pApp->store == "Steam" && (pApp->id != SKIF_STEAM_APPID ||
                                         (pApp->id == SKIF_STEAM_APPID && SKIF_STEAM_OWNER)))
      {

        ImGui::TextColored (
         (_registry.iStyle == 2) ? ImColor(0, 0, 0) : ImColor(255, 255, 255),
           ICON_FA_STEAM_SYMBOL );

        ImGui::TextColored (
         (_registry.iStyle == 2) ? ImColor(0, 0, 0) : ImColor(255, 255, 255),
           ICON_FA_STEAM_SYMBOL );

        ImGui::TextColored (
         ImColor   (101, 192, 244, 255).Value,
           ICON_FA_DATABASE );
      }

    }

    else if (! update)
    {
      ImGui::CloseCurrentPopup ();
    }

    ImGui::EndPopup ();
  }


  if (ConfirmPopup == PopupState::Open)
  {
    ImGui::OpenPopup("###ConfirmPopup");
    //ConfirmPopup = PopupState::Opened;
  }

  float fConfirmPopupWidth = ImGui::CalcTextSize(confirmPopupText.c_str()).x + 60.0f * SKIF_ImGui_GlobalDPIScale;
  ImGui::SetNextWindowSize (ImVec2 (fConfirmPopupWidth, 0.0f));

  if (ImGui::BeginPopupModal ((confirmPopupTitle + "###ConfirmPopup").c_str(), nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
  {
    if (ConfirmPopup == PopupState::Open)
    {
      // Set the popup as opened after it has appeared (fixes popup not opening from other tabs)
      ImGuiWindow* window = ImGui::FindWindowByName ("###ConfirmPopup");
      if (window != nullptr && ! window->Appearing)
        ConfirmPopup = PopupState::Opened;
    }

    ImGui::TreePush    ("");

    SKIF_ImGui_Spacing ( );

    ImGui::Text        (confirmPopupText.c_str());

    SKIF_ImGui_Spacing ( );
    SKIF_ImGui_Spacing ( );

    ImVec2 vButtonSize = ImVec2(80.0f * SKIF_ImGui_GlobalDPIScale, 0.0f);

    ImGui::SetCursorPosX (fConfirmPopupWidth / 2 - vButtonSize.x / 2);

    if (ImGui::Button  ("OK", vButtonSize))
    {
      confirmPopupText = "";
      ConfirmPopup = PopupState::Closed;
      ImGui::CloseCurrentPopup ( );
    }

    SKIF_ImGui_Spacing ( );

    ImGui::TreePop     ( );

    ImGui::EndPopup ( );
  }


  if (RemoveGamePopup == PopupState::Open)
  {
    ImGui::OpenPopup("###RemoveGamePopup");
    RemoveGamePopup = PopupState::Opened;
  }


  float fRemoveGamePopupWidth = 360.0f * SKIF_ImGui_GlobalDPIScale;
  ImGui::SetNextWindowSize (ImVec2 (fRemoveGamePopupWidth, 0.0f));

  if (ImGui::BeginPopupModal ("Remove Game###RemoveGamePopup", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::TreePush    ("");

    SKIF_ImGui_Spacing ( );

    ImGui::Text        ("Do you want to remove this game from SKIF?");

    SKIF_ImGui_Spacing ( );
    SKIF_ImGui_Spacing ( );

    ImVec2 vButtonSize = ImVec2(80.0f * SKIF_ImGui_GlobalDPIScale, 0.0f);

    ImGui::SetCursorPosX (fRemoveGamePopupWidth / 2 - vButtonSize.x - 20.0f * SKIF_ImGui_GlobalDPIScale);

    if (ImGui::Button  ("Yes", vButtonSize))
    {
      if (SKIF_RemoveCustomAppID(selection.appid))
      {
        // Hide entry
        pApp->id = 0;

        // Release the icon texture (the cover will be handled by LoadLibraryTexture on next frame
        if (pApp->textures.icon.p != nullptr)
        {
          extern concurrency::concurrent_queue <CComPtr <IUnknown>> SKIF_ResourcesToFree;
          PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << pApp->textures.icon.p << " to be released";
          SKIF_ResourcesToFree.push(pApp->textures.icon.p);
          pApp->textures.icon.p = nullptr;
        }

        // Reset selection to Special K
        selection.appid = SKIF_STEAM_APPID;
        selection.store = "Steam";

        for (auto& app : apps)
          if (app.second.id == selection.appid)
            pApp = &app.second;

        update = true;
      }

      RemoveGamePopup = PopupState::Closed;
      ImGui::CloseCurrentPopup ( );
    }

    ImGui::SameLine    ( );

    ImGui::SetCursorPosX (fRemoveGamePopupWidth / 2 + 20.0f * SKIF_ImGui_GlobalDPIScale);

    if (ImGui::Button  ("No", vButtonSize))
    {
      RemoveGamePopup = PopupState::Closed;
      ImGui::CloseCurrentPopup ( );
    }

    SKIF_ImGui_Spacing ( );

    ImGui::TreePop     ( );

    ImGui::EndPopup ( );
  }
  else {
    RemoveGamePopup = PopupState::Closed;
  }


  if (AddGamePopup == PopupState::Open && ! ImGui::IsAnyPopupOpen ( ))
  {
    ImGui::OpenPopup("###AddGamePopup");
    //AddGamePopup = PopupState::Opened; // Set as part of the BeginPopupModal() call below instead
  }

  float fAddGamePopupWidth = 544.0f * SKIF_ImGui_GlobalDPIScale;
  ImGui::SetNextWindowSize (ImVec2 (fAddGamePopupWidth, 0.0f));

  if (ImGui::BeginPopupModal ("Add Game###AddGamePopup", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
  {
    /*
      name          - String -- Title/Name
      exe           - String -- Full Path to executable
      launchOptions - String -- Cmd line args
      id            - Autogenerated
      installDir    - Autogenerated
      exeFileName   - Autogenerated
    */

    if (AddGamePopup == PopupState::Open)
    {
      // Set the popup as opened after it has appeared (fixes popup not opening from other tabs)
      ImGuiWindow* window = ImGui::FindWindowByName ("###AddGamePopup");
      if (window != nullptr && ! window->Appearing)
        AddGamePopup = PopupState::Opened;
    }

    static char charName     [MAX_PATH],
                charPath     [MAX_PATH],
                charArgs     [500];
    static bool error = false;

    ImGui::TreePush    ("");

    SKIF_ImGui_Spacing ( );

    ImVec2 vButtonSize = ImVec2(80.0f * SKIF_ImGui_GlobalDPIScale, 0.0f);

    if (ImGui::Button  ("Browse...", vButtonSize))
    {
      extern HWND SKIF_hWnd;

      LPWSTR pwszFilePath = NULL;

      if (SK_FileOpenDialog (&pwszFilePath, COMDLG_FILTERSPEC{ L"Executables", L"*.exe" }, 1, FOS_NODEREFERENCELINKS | FOS_NOVALIDATE | FOS_FILEMUSTEXIST))
      {
        error = false;
        std::filesystem::path path           = pwszFilePath; // Wide-string std::filesystem::path
        std::filesystem::path pathDiscard    = pwszFilePath; // Wide-string std::filesystem::path which will be discarded
        std::string           pathFullPath   = SK_WideCharToUTF8  (pathDiscard.wstring());
        std::wstring          pathExtension  = SKIF_Util_TowLower (pathDiscard.extension().wstring());
        std::string           pathFilename   = SK_WideCharToUTF8  (pathDiscard.replace_extension().filename().wstring()); // This removes the extension from pathDiscard

        if (pathExtension == L".lnk")
        {
          WCHAR wszTarget    [MAX_PATH];
          WCHAR wszArguments [MAX_PATH];

          SKIF_Util_ResolveShortcut (SKIF_hWnd, path.c_str(), wszTarget, wszArguments, MAX_PATH);

          if (! PathFileExists (wszTarget))
          {
            error = true;
            strncpy (charPath, "\0", MAX_PATH);
          }

          else {
            std::wstring productName = SKIF_GetProductName (wszTarget);
            productName.erase (std::find_if (productName.rbegin(), productName.rend(), [](wchar_t ch) {return ! std::iswspace(ch);}).base(), productName.end());
          
            strncpy (charPath, SK_WideCharToUTF8 (wszTarget).c_str(),                  MAX_PATH);
            strncpy (charArgs, SK_WideCharToUTF8 (wszArguments).c_str(),               500);
            strncpy (charName, (productName != L"")
                                ? SK_WideCharToUTF8 (productName).c_str()
                                : pathFilename.c_str(), MAX_PATH);
          }
        }

        else if (pathExtension == L".exe") {
          std::wstring productName = SKIF_GetProductName (path.c_str());
          productName.erase (std::find_if (productName.rbegin(), productName.rend(), [](wchar_t ch) {return ! std::iswspace(ch);}).base(), productName.end());

          strncpy (charPath, pathFullPath.c_str(),                                  MAX_PATH);
          strncpy (charName, (productName != L"")
                              ? SK_WideCharToUTF8 (productName).c_str()
                              : pathFilename.c_str(), MAX_PATH);
        }

        else {
          error = true;
          strncpy (charPath, "\0", MAX_PATH);
        }
      }

      else {
        error = true;
        strncpy (charPath, "\0", MAX_PATH);
      }
    }
    ImGui::SameLine    ( );

    float fAddGamePopupX = ImGui::GetCursorPosX ( );

    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::InputText   ("###GamePath", charPath, MAX_PATH, ImGuiInputTextFlags_ReadOnly);
    ImGui::PopStyleColor ( );
    ImGui::SameLine    ( );
    ImGui::Text        ("Path");

    if (error)
    {
      ImGui::SetCursorPosX (fAddGamePopupX);
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), "Incompatible type! Please select another file.");
    }
    else {
      ImGui::NewLine   ( );
    }

    ImGui::SetCursorPosX (fAddGamePopupX);

    ImGui::InputText   ("###GameName", charName, MAX_PATH);
    ImGui::SameLine    ( );
    ImGui::Text        ("Name");

    ImGui::SetCursorPosX (fAddGamePopupX);

    ImGui::InputTextEx ("###GameArgs", "Leave empty if unsure", charArgs, 500, ImVec2(0,0), ImGuiInputTextFlags_None);
    ImGui::SameLine    ( );
    ImGui::Text        ("Launch Options");

    SKIF_ImGui_Spacing ( );
    SKIF_ImGui_Spacing ( );

    ImGui::SetCursorPosX (fAddGamePopupWidth / 2 - vButtonSize.x - 20.0f * SKIF_ImGui_GlobalDPIScale);

    bool disabled = false;

    if ((charName[0] == '\0' || std::isspace(charName[0])) ||
        (charPath[0] == '\0' || std::isspace(charPath[0])))
      disabled = true;

    if (disabled)
    {
      ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
      ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle ().Alpha * 0.5f);
    }

    if (ImGui::Button  ("Add Game", vButtonSize))
    {
      int newAppId = SKIF_AddCustomAppID(&apps, SK_UTF8ToWideChar(charName), SK_UTF8ToWideChar(charPath), SK_UTF8ToWideChar(charArgs));

      if (newAppId > 0)
        InterlockedExchange (&need_sort, 1);

      // Clear variables
      error = false;
      strncpy (charName, "\0", MAX_PATH);
      strncpy (charPath, "\0", MAX_PATH);
      strncpy (charArgs, "\0", 500);

      // Change selection to the new game
      selection.appid = newAppId;
      for (auto& app : apps)
        if (app.second.id == selection.appid && app.second.store == "SKIF")
          pApp = &app.second;

      ImVec2 dontCare1, dontCare2;

      // Load the new icon (hopefully)
      LoadLibraryTexture (LibraryTexture::Icon,
                            newAppId,
                              pApp->textures.icon,
                                L"icon",
                                    dontCare1,
                                      dontCare2,
                                        pApp );

      update = true;

      // Unload any current cover
      if (pTexSRV.p != nullptr)
      {
        extern concurrency::concurrent_queue <CComPtr <IUnknown>> SKIF_ResourcesToFree;
        PLOG_VERBOSE << "SKIF_ResourcesToFree: Pushing " << pTexSRV.p << " to be released";;
        SKIF_ResourcesToFree.push(pTexSRV.p);
        pTexSRV.p = nullptr;
      }

      AddGamePopup = PopupState::Closed;
      ImGui::CloseCurrentPopup ( );
    }

    if (disabled)
    {
      ImGui::PopItemFlag ( );
      ImGui::PopStyleVar ( );
    }

    ImGui::SameLine    ( );

    ImGui::SetCursorPosX (fAddGamePopupWidth / 2 + 20.0f * SKIF_ImGui_GlobalDPIScale);

    if (ImGui::Button  ("Cancel", vButtonSize))
    {
      // Clear variables
      error = false;
      strncpy (charName, "\0", MAX_PATH);
      strncpy (charPath, "\0", MAX_PATH);
      strncpy (charArgs, "\0", 500);

      AddGamePopup = PopupState::Closed;
      ImGui::CloseCurrentPopup ( );
    }

    SKIF_ImGui_Spacing ( );

    ImGui::TreePop     ( );

    ImGui::EndPopup    ( );
  }
  else {
   AddGamePopup = PopupState::Closed;
  }



  if (ModifyGamePopup == PopupState::Open)
    ImGui::OpenPopup ("###ModifyGamePopup");

  float fModifyGamePopupWidth = 544.0f * SKIF_ImGui_GlobalDPIScale;
  ImGui::SetNextWindowSize (ImVec2 (fModifyGamePopupWidth, 0.0f));

  if (ImGui::BeginPopupModal ("Manage Game###ModifyGamePopup", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
  {
    static char charName     [MAX_PATH],
                charPath     [MAX_PATH],
                charArgs     [500];
                //charProfile  [MAX_PATH];
    static bool error = false;

    if (ModifyGamePopup == PopupState::Open)
    {
      std::string name = pApp->names.normal;
      try {
        name = std::regex_replace(name, std::regex(R"( \(recently added\))"), "");
      }
      catch (const std::exception& e)
      {
        UNREFERENCED_PARAMETER(e);
      }

      strncpy (charName, name.c_str( ), MAX_PATH);
      strncpy (charPath, SK_WideCharToUTF8 (pApp->launch_configs[0].getExecutableFullPath(pApp->id, false)).c_str(), MAX_PATH);
      strncpy (charArgs, SK_WideCharToUTF8 (pApp->launch_configs[0].launch_options).c_str(), 500);
      //strncpy (charProfile, SK_WideCharToUTF8 (SK_FormatStringW(LR"(%s\Profiles\%s)", path_cache.specialk_userdata, pApp->specialk.profile_dir.c_str())).c_str(), MAX_PATH);
      
      // Set the popup as opened after it has appeared (fixes popup not opening from other tabs)
      ImGuiWindow* window = ImGui::FindWindowByName ("###ModifyGamePopup");
      if (window != nullptr && ! window->Appearing)
        ModifyGamePopup = PopupState::Opened;
    }

    ImGui::TreePush    ("");

    SKIF_ImGui_Spacing ( );

    ImVec2 vButtonSize = ImVec2(80.0f * SKIF_ImGui_GlobalDPIScale, 0.0f);

    if (ImGui::Button  ("Browse...", vButtonSize))
    {
      LPWSTR pwszFilePath = NULL;
      if (SK_FileOpenDialog (&pwszFilePath, COMDLG_FILTERSPEC{ L"Executables", L"*.exe" }, 1, FOS_NODEREFERENCELINKS | FOS_NOVALIDATE | FOS_FILEMUSTEXIST))
      {
        error = false;
        std::filesystem::path path           = pwszFilePath; // Wide-string std::filesystem::path
        std::filesystem::path pathDiscard    = pwszFilePath; // Wide-string std::filesystem::path which will be discarded
        std::string           pathFullPath   = SK_WideCharToUTF8  (pathDiscard.wstring());
        std::wstring          pathExtension  = SKIF_Util_TowLower (pathDiscard.extension().wstring());
        std::string           pathFilename   = SK_WideCharToUTF8  (pathDiscard.replace_extension().filename().wstring()); // This removes the extension from pathDiscard

        if (pathExtension == L".lnk")
        {
          WCHAR wszTarget    [MAX_PATH];
          WCHAR wszArguments [MAX_PATH];

          SKIF_Util_ResolveShortcut (SKIF_hWnd, path.c_str(), wszTarget, wszArguments, MAX_PATH);

          if (! PathFileExists (wszTarget))
          {
            error = true;
            strncpy (charPath, "\0", MAX_PATH);
          }

          else {
            std::wstring productName = SKIF_GetProductName (wszTarget);
            productName.erase (std::find_if (productName.rbegin(), productName.rend(), [](wchar_t ch) {return ! std::iswspace(ch);}).base(), productName.end());
          
            strncpy (charPath, SK_WideCharToUTF8 (wszTarget).c_str(),                  MAX_PATH);
          }
        }

        else if (pathExtension == L".exe") {
          std::wstring productName = SKIF_GetProductName (path.c_str());
          productName.erase (std::find_if (productName.rbegin(), productName.rend(), [](wchar_t ch) {return ! std::iswspace(ch);}).base(), productName.end());

          strncpy (charPath, pathFullPath.c_str(),                                  MAX_PATH);
        }

        else {
          error = true;
          strncpy (charPath, "\0", MAX_PATH);
        }
      }

      else {
        error = true;
        strncpy (charPath, "\0", MAX_PATH);
      }
    }
    ImGui::SameLine    ( );

    float fModifyGamePopupX = ImGui::GetCursorPosX ( );

    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::InputText   ("###GamePath", charPath, MAX_PATH, ImGuiInputTextFlags_ReadOnly);
    ImGui::PopStyleColor ( );
    ImGui::SameLine    ( );
    ImGui::Text        ("Path");

    if (error)
    {
      ImGui::SetCursorPosX (fModifyGamePopupX);
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), "Incompatible type! Please select another file.");
    }
    else {
      ImGui::NewLine   ( );
    }

    ImGui::SetCursorPosX (fModifyGamePopupX);

    ImGui::InputText   ("###GameName", charName, MAX_PATH);
    ImGui::SameLine    ( );
    ImGui::Text        ("Name");

    ImGui::SetCursorPosX (fModifyGamePopupX);

    ImGui::InputTextEx ("###GameArgs", "Leave empty if unsure", charArgs, 500, ImVec2(0,0), ImGuiInputTextFlags_None);
    ImGui::SameLine    ( );
    ImGui::Text        ("Launch Options");

    SKIF_ImGui_Spacing ( );
    SKIF_ImGui_Spacing ( );

    ImGui::SetCursorPosX (fModifyGamePopupWidth / 2 - vButtonSize.x - 20.0f * SKIF_ImGui_GlobalDPIScale);

    bool disabled = false;

    if ((charName[0] == '\0' || std::isspace(charName[0])) ||
        (charPath[0] == '\0' || std::isspace(charPath[0])))
      disabled = true;

    if (disabled)
    {
      ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
      ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle ().Alpha * 0.5f);
    }

    if (ImGui::Button  ("Update", vButtonSize))
    {
      if (SKIF_ModifyCustomAppID (pApp, SK_UTF8ToWideChar(charName), SK_UTF8ToWideChar(charPath), SK_UTF8ToWideChar(charArgs)))
      {
        for (auto& app : apps)
        {
          if (app.second.id == pApp->id && app.second.store == "SKIF")
          {
            app.first = pApp->names.normal;

            std::string all_upper;

            for (const char c : app.first)
            {
              if (! ( isalnum (c) || isspace (c)))
                continue;

              all_upper += (char)toupper (c);
            }

            static const
              std::string toSkip [] =
              {
                std::string ("A "),
                std::string ("THE ")
              };

            for ( auto& skip_ : toSkip )
            {
              if (all_upper.find (skip_) == 0)
              {
                all_upper =
                  all_upper.substr (
                    skip_.length ()
                  );
                break;
              }
            }

            std::string trie_builder;

            for ( const char c : all_upper)
            {
              trie_builder += c;

              labels.insert (trie_builder);
            }

            pApp->names.all_upper = trie_builder;
          }
        }

        InterlockedExchange (&need_sort, 1);

        // Clear variables
        error = false;
        strncpy (charName, "\0", MAX_PATH);
        strncpy (charPath, "\0", MAX_PATH);
        strncpy (charArgs, "\0", 500);

        update = true;

        ModifyGamePopup = PopupState::Closed;
        ImGui::CloseCurrentPopup();
      }
    }

    if (disabled)
    {
      ImGui::PopItemFlag ( );
      ImGui::PopStyleVar ( );
    }

    ImGui::SameLine    ( );

    ImGui::SetCursorPosX (fModifyGamePopupWidth / 2 + 20.0f * SKIF_ImGui_GlobalDPIScale);

    if (ImGui::Button  ("Cancel", vButtonSize))
    {
      // Clear variables
      error = false;
      strncpy (charName, "\0", MAX_PATH);
      strncpy (charPath, "\0", MAX_PATH);
      strncpy (charArgs, "\0", 500);

      ModifyGamePopup = PopupState::Closed;
      ImGui::CloseCurrentPopup ( );
    }

    SKIF_ImGui_Spacing ( );

    ImGui::TreePop     ( );

    ImGui::EndPopup    ( );
  }
  else {
    ModifyGamePopup = PopupState::Closed;
  }

  extern uint32_t SelectNewSKIFGame;

  if (SelectNewSKIFGame > 0)
  {
    // Change selection to the new game
    selection.appid = SelectNewSKIFGame;
    selection.store = "SKIF";
    for (auto& app : apps)
      if (app.second.id == selection.appid && app.second.store == "SKIF")
        pApp = &app.second;

    update = true;

    SelectNewSKIFGame = 0;
  }
}


#if 0
struct SKIF_Store {

};

struct SKIF_Anticheat {
  bool has;
};

enum class DRM_Authority {
  None,
  Steam,
  Origin,
  uPlay,
  MicrosoftStore,
  EpicGameStore,
  Bethesda_net,
  Galaxy
};

enum class AntiTamper_Type {
  None,
  Denuvo
};

struct SKIF_DRM {
  DRM_Authority   drm_auth;
  AntiTamper_Type anti_tamper;
};

struct SKIF_GameFeatures {
  bool vr;
  bool hdr;
  bool cloud;
  bool achievements;
  bool leaderboards;
  bool rich_presence;
//bool raytraced;
};
struct SKIF_Game {
  SKIF_Store     parent;
  std::wstring   name;
  std::wstring   executable;
  std::vector <
    std::wstring
  >              config_paths;
  bool           enable_injection;
  int            render_api;
  int            input_api;
  int            bitness;
  SKIF_DRM       drm;
};
#endif