#include <stores/generic_library2.h>

#include "DirectXTex.h"
#include <utility/sk_utility.h>
#include <utility/utility.h>
#include <utility/fsutil.h>

#include <images/patreon.png.h>
#include <images/sk_icon.jpg.h>
#include <images/sk_boxart.png.h>

#include <concurrent_queue.h>
#include "stores/Steam/steam_library.h"

extern CComPtr <ID3D11Device> SKIF_D3D11_GetDevice (bool bWait = true);

bool app_generic_s::loadCoverFromFile (std::wstring path)
{
  PLOG_DEBUG << "Texture to load: " << path;
  bool succeeded = false;
  CComPtr <ID3D11Texture2D> pTex2D;
  DirectX::TexMetadata        meta = { };
  DirectX::ScratchImage        img = { };

  if (path != L"\0" &&
      SUCCEEDED(
        DirectX::LoadFromWICFile (
          path.c_str (),
            DirectX::WIC_FLAGS_FILTER_POINT | DirectX::WIC_FLAGS_IGNORE_SRGB, // WIC_FLAGS_IGNORE_SRGB solves some PNGs appearing too dark
              &meta, img
        )
      )
    )
  {
    succeeded = true;
  }

  if (succeeded)
  {
    //if (libTexToLoad == LibraryTexture::Cover)
    //  OutputDebugString((L"[App#" + std::to_wstring(appid) + L"] Loading the source image succeeded...\n").c_str());

    DirectX::ScratchImage* pImg   =
                                &img;
    DirectX::ScratchImage   converted_img;

    // Start aspect ratio
    ImVec2 vCoverUv0 = ImVec2(0.f, 0.f); // Top left corner
    ImVec2 vCoverUv1 = ImVec2(1.f, 1.f); // Bottom right corner
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
    }

    // Crop thinner aspect ratios by their height
    else if ((vecTex2D.x / vecTex2D.y) < (600.f / 900.f))
    {
      float newHeight = vecTex2D.y / vecTex2D.x * 600.0f;
      diff.y = (900.0f / newHeight);
      diff.y -= 1.0f;
      diff.y /= 2;
      
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
      return false;

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

      if (    pTex2D.p == nullptr ||
        FAILED (
          pDevice->CreateShaderResourceView (
              pTex2D.p, &srv_desc,
            &this->textures.cover
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

  return false;
}

bool app_generic_s::loadIconFromFile (std::wstring file)
{
  return false;
}


// OLD METHODS


void
LoadLibraryTexture (
        LibraryTexture                      libTexToLoad,
        uint32_t                            appid,
        CComPtr <ID3D11ShaderResourceView>& pLibTexSRV,
        const std::wstring&                 name,
        ImVec2&                             vCoverUv0,
        ImVec2&                             vCoverUv1,
        app_record_s*                       pApp)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static const int SKIF_STEAM_APPID = 1157970;

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
    SKIFCustomPath = SK_FormatStringW (LR"(%ws\Assets\)", _path_cache.specialk_userdata);

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
    SKIFCustomPath = SK_FormatStringW (LR"(%ws\Assets\Custom\%i\)", _path_cache.specialk_userdata, appid);

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
    std::wstring EGSAssetPath = SK_FormatStringW(LR"(%ws\Assets\EGS\%ws\)", _path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->EGS_AppName).c_str());
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
    SKIFCustomPath = SK_FormatStringW (LR"(%ws\Assets\GOG\%i\)", _path_cache.specialk_userdata, appid);

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
    std::wstring XboxAssetPath = SK_FormatStringW(LR"(%ws\Assets\Xbox\%ws\)", _path_cache.specialk_userdata, SK_UTF8ToWideChar(pApp->Xbox_PackageName).c_str());
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

    SKIFCustomPath  = SK_FormatStringW (LR"(%ws\Assets\Steam\%i\)", _path_cache.specialk_userdata, appid);
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