#include <SKIF_imgui.h>
#include <font_awesome.h>
#include <sk_utility/utility.h>
#include <SKIF_utility.h>
#include <filesystem>
#include <fsutil.h>

#include <dxgi.h>
#include <d3d11.h>
#include <d3dkmthk.h>
#include "../../version.h"

// Registry Settings
#include <registry.h>

static SKIF_RegistrySettings& _registry = SKIF_RegistrySettings::GetInstance( );

struct Monitor_MPO_Support
{
  std::string                    Name;  // EDID names are limited to 13 characters, which is perfect for us
  UINT                           Index; // Doesn't really correspond to anything important...
  std::string                    DeviceNameGdi;
  std::string                    DevicePath;
  UINT                           MaxPlanes;
  UINT                           MaxRGBPlanes;
  UINT                           MaxYUVPlanes;
  float                          MaxStretchFactor;
  float                          MaxShrinkFactor;
  D3DKMT_MULTIPLANE_OVERLAY_CAPS OverlayCaps;
  std::string                    OverlayCapsAsString;
};

enum DrvInstallState {
  NotInstalled,
  Installed,
  OtherDriverInstalled,
  ObsoleteInstalled
};

std::vector <Monitor_MPO_Support> Monitors;
DrvInstallState driverStatus        = NotInstalled,
                driverStatusPending = NotInstalled;

// Check the MPO capabilities of the system
bool
GetMPOSupport (void)
{
  // D3DKMTGetMultiPlaneOverlayCaps (Windows 10+)
  using D3DKMTGetMultiPlaneOverlayCaps_pfn =
    NTSTATUS (WINAPI *)(D3DKMT_GET_MULTIPLANE_OVERLAY_CAPS*);

  static D3DKMTGetMultiPlaneOverlayCaps_pfn
    SKIF_D3DKMTGetMultiPlaneOverlayCaps =
        (D3DKMTGetMultiPlaneOverlayCaps_pfn)GetProcAddress (LoadLibraryEx (L"gdi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        "D3DKMTGetMultiPlaneOverlayCaps");

  // D3DKMTOpenAdapterFromLuid (Windows 8+)
  using D3DKMTOpenAdapterFromLuid_pfn =
    NTSTATUS (WINAPI *)(D3DKMT_OPENADAPTERFROMLUID*);

  static D3DKMTOpenAdapterFromLuid_pfn
    SKIF_D3DKMTOpenAdapterFromLuid =
        (D3DKMTOpenAdapterFromLuid_pfn)GetProcAddress (LoadLibraryEx (L"gdi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32),
        "D3DKMTOpenAdapterFromLuid");

  if (SKIF_D3DKMTOpenAdapterFromLuid      == nullptr ||
      SKIF_D3DKMTGetMultiPlaneOverlayCaps == nullptr)
    return false;

  std::vector<DISPLAYCONFIG_PATH_INFO> pathArray;
  std::vector<DISPLAYCONFIG_MODE_INFO> modeArray;
  LONG result = ERROR_SUCCESS;

  Monitors.clear();

  do
  {
    // Determine how many path and mode structures to allocate
    UINT32 pathCount, modeCount;
    result = GetDisplayConfigBufferSizes (QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);

    if (result != ERROR_SUCCESS)
    {
      PLOG_ERROR << "GetDisplayConfigBufferSizes failed: " << SKIF_Util_GetErrorAsWStr (result);
      return false;
    }

    // Allocate the path and mode arrays
    pathArray.resize(pathCount);
    modeArray.resize(modeCount);

    // Get all active paths and their modes
    result = QueryDisplayConfig ( QDC_ONLY_ACTIVE_PATHS, &pathCount, pathArray.data(),
                                                         &modeCount, modeArray.data(), nullptr);

    // The function may have returned fewer paths/modes than estimated
    pathArray.resize(pathCount);
    modeArray.resize(modeCount);

    // It's possible that between the call to GetDisplayConfigBufferSizes and QueryDisplayConfig
    // that the display state changed, so loop on the case of ERROR_INSUFFICIENT_BUFFER.
  } while (result == ERROR_INSUFFICIENT_BUFFER);

  if (result != ERROR_SUCCESS)
  {
    PLOG_ERROR << "QueryDisplayConfig failed: " << SKIF_Util_GetErrorAsWStr (result);
    return false;
  }

  // For each active path
  for (auto& path : pathArray)
  {
    // Find the target (monitor) friendly name
    DISPLAYCONFIG_TARGET_DEVICE_NAME targetName = {};
    targetName.header.adapterId            = path.targetInfo.adapterId;
    targetName.header.id                   = path.targetInfo.id;
    targetName.header.type                 = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
    targetName.header.size                 = sizeof (targetName);

    result = DisplayConfigGetDeviceInfo (&targetName.header);

    std::wstring monitorName = (targetName.flags.friendlyNameFromEdid ? targetName.monitorFriendlyDeviceName
                                                                      : L"Unknown");

    if (result != ERROR_SUCCESS)
    {
      PLOG_ERROR << "DisplayConfigGetDeviceInfo failed: " << SKIF_Util_GetErrorAsWStr (result);
      return false;
    }

    // Find the adapter device name
    DISPLAYCONFIG_ADAPTER_NAME adapterName = {};
    adapterName.header.adapterId           = path.targetInfo.adapterId;
    adapterName.header.type                = DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME;
    adapterName.header.size                = sizeof (adapterName);

    result = DisplayConfigGetDeviceInfo (&adapterName.header);

    if (result != ERROR_SUCCESS)
    {
      PLOG_ERROR << "DisplayConfigGetDeviceInfo failed: " << SKIF_Util_GetErrorAsWStr (result);
      return false;
    }

    // Find the source device name
    DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName = {};
    sourceName.header.adapterId            = path.sourceInfo.adapterId;
    sourceName.header.id                   = path.sourceInfo.id;
    sourceName.header.type                 = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    sourceName.header.size                 = sizeof (sourceName);

    result = DisplayConfigGetDeviceInfo (&sourceName.header);

    if (result != ERROR_SUCCESS)
    {
      PLOG_ERROR << "DisplayConfigGetDeviceInfo failed: " << SKIF_Util_GetErrorAsWStr (result);
      return false;
    }

    PLOG_VERBOSE << SKIF_LOG_SEPARATOR;
    PLOG_VERBOSE << "Monitor Name: " << monitorName;
    PLOG_VERBOSE << "Adapter Path: " << adapterName.adapterDevicePath;
    PLOG_VERBOSE << "Source Name:  " << sourceName.viewGdiDeviceName;

    //PLOG_VERBOSE << "Target: "       << path.targetInfo.id;

    // Open a handle to the adapter using its LUID
    D3DKMT_OPENADAPTERFROMLUID openAdapter = {};
    openAdapter.AdapterLuid = adapterName.header.adapterId;
    if (SKIF_D3DKMTOpenAdapterFromLuid (&openAdapter) == (NTSTATUS)0x00000000L) // STATUS_SUCCESS
    {
      D3DKMT_GET_MULTIPLANE_OVERLAY_CAPS caps = {};
      caps.hAdapter      = openAdapter.hAdapter;
      caps.VidPnSourceId = path.sourceInfo.id;

      if (SKIF_D3DKMTGetMultiPlaneOverlayCaps (&caps) == (NTSTATUS)0x00000000L) // STATUS_SUCCESS
      {
        PLOG_VERBOSE << "MPO MaxPlanes: "    << caps.MaxPlanes;
        PLOG_VERBOSE << "MPO MaxRGBPlanes: " << caps.MaxRGBPlanes; // MaxRGBPlanes seems to be the number that best corresponds to dxdiag's reporting? Or is it?
        PLOG_VERBOSE << "MPO MaxYUVPlanes: " << caps.MaxYUVPlanes;
        PLOG_VERBOSE << "MPO Stretch: "      << caps.MaxStretchFactor << "x - " << caps.MaxShrinkFactor << "x";
        PLOG_VERBOSE << SKIF_LOG_SEPARATOR;
          
        Monitor_MPO_Support monitor;
        monitor.Name                = SK_WideCharToUTF8 (monitorName);
        monitor.Index               = path.sourceInfo.cloneGroupId;
        monitor.DevicePath          = SK_WideCharToUTF8 (adapterName.adapterDevicePath);
        monitor.DeviceNameGdi       = SK_WideCharToUTF8 (sourceName.viewGdiDeviceName);
        monitor.MaxPlanes           = caps.MaxPlanes;
        monitor.MaxRGBPlanes        = caps.MaxRGBPlanes;
        monitor.MaxYUVPlanes        = caps.MaxYUVPlanes;
        monitor.MaxStretchFactor    = caps.MaxStretchFactor;
        monitor.MaxShrinkFactor     = caps.MaxShrinkFactor;
        monitor.OverlayCaps         = caps.OverlayCaps;
        monitor.OverlayCapsAsString = "";

        /*
              UINT Rotation                        : 1;    // Full rotation
              UINT RotationWithoutIndependentFlip  : 1;    // Rotation, but without simultaneous IndependentFlip support
              UINT VerticalFlip                    : 1;    // Can flip the data vertically
              UINT HorizontalFlip                  : 1;    // Can flip the data horizontally
              UINT StretchRGB                      : 1;    // Supports stretching RGB formats
              UINT StretchYUV                      : 1;    // Supports stretching YUV formats
              UINT BilinearFilter                  : 1;    // Bilinear filtering
              UINT HighFilter                      : 1;    // Better than bilinear filtering
              UINT Shared                          : 1;    // MPO resources are shared across VidPnSources
              UINT Immediate                       : 1;    // Immediate flip support
              UINT Plane0ForVirtualModeOnly        : 1;    // Stretching plane 0 will also stretch the HW cursor and should only be used for virtual mode support
              UINT Version3DDISupport              : 1;    // Driver supports the 2.2 MPO DDIs
        */

        // "RGB" and "YUV" capabilities seems inferred from the MaxRGBPlanes and MaxYUVPlanes variables
        // The uppercase titles is how the capability seems to be reported through dxdiag.exe / dxdiagn.dll (educated guess)

        // See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dxgiddi/ne-dxgiddi-dxgi_ddi_multiplane_overlay_feature_caps

        if (monitor.MaxRGBPlanes > 1) // Dxdiag doesn't seem to report this if there's only 1 plane supported
          monitor.OverlayCapsAsString += "Supports " + std::to_string(monitor.MaxRGBPlanes) + " planes containing RGB data. [RGB]\n";

        if (monitor.MaxYUVPlanes > 1) // Dxdiag doesn't seem to report this if there's only 1 plane supported
          monitor.OverlayCapsAsString += "Supports " + std::to_string(monitor.MaxYUVPlanes) + " planes containing YUV data. [YUV]\n";

        if (monitor.OverlayCaps.Rotation)
          monitor.OverlayCapsAsString += "Supports full rotation of the MPO plane with Independent Flip. [ROTATION]\n";

        if (monitor.OverlayCaps.RotationWithoutIndependentFlip)
          monitor.OverlayCapsAsString += "Supports full rotation of the MPO plane, but without Independent Flip. [ROTATION_WITHOUT_INDEPENDENT_FLIP]\n";

        if (monitor.OverlayCaps.VerticalFlip)
          monitor.OverlayCapsAsString += "Supports flipping the data vertically. [VERTICAL_FLIP]\n";

        if (monitor.OverlayCaps.HorizontalFlip)
          monitor.OverlayCapsAsString += "Supports flipping the data horizontally. [HORIZONTAL_FLIP]\n";

        if (monitor.OverlayCaps.StretchRGB && monitor.MaxRGBPlanes > 1) // Dxdiag doesn't seem to report this if there's only 1 plane supported
          monitor.OverlayCapsAsString += "Supports stretching any plane containing RGB data. [STRETCH_RGB]\n";

        if (monitor.OverlayCaps.StretchYUV && monitor.MaxYUVPlanes > 1) // Dxdiag doesn't seem to report this if there's only 1 plane supported
          monitor.OverlayCapsAsString += "Supports stretching any plane containing YUV data. [STRETCH_YUV]\n";

        if (monitor.OverlayCaps.BilinearFilter)
          monitor.OverlayCapsAsString += "Supports bilinear filtering. [BILINEAR]\n";

        if (monitor.OverlayCaps.HighFilter)
          monitor.OverlayCapsAsString += "Supports better than bilinear filtering. [HIGH_FILTER]\n";

        if (monitor.OverlayCaps.Shared)
          monitor.OverlayCapsAsString += "MPO resources are shared across video present network (VidPN) sources. [SHARED]\n";

        if (monitor.OverlayCaps.Immediate)
          monitor.OverlayCapsAsString += "Supports immediate flips (allows tearing) of the MPO plane. [IMMEDIATE]\n";
        // When TRUE, the HW supports immediate flips of the MPO plane.
        // If the flip contains changes that cannot be performed as an immediate flip,
        //  the driver can promote the flip to a VSYNC flip using the new HSync completion infrastructure.

        if (monitor.OverlayCaps.Plane0ForVirtualModeOnly)
          monitor.OverlayCapsAsString += "Will always apply the stretch factor of plane 0 to the hardware cursor as well as the plane. [PLANE0_FOR_VIRTUAL_MODE_ONLY]\n";
        // When TRUE, the hardware will always apply the stretch factor of plane 0 to the hardware cursor as well as the plane.
        //  This implies that stretching/shrinking of plane 0 should only occur when plane 0 is the desktop plane and when the
        //   stretching/shrinking is used for virtual mode support.

        // Monitor supports the new DDIs, though we have no idea how to further query that kind of support yet...
        if (monitor.OverlayCaps.Version3DDISupport)
          monitor.OverlayCapsAsString += "Driver supports the WDDM 2.2 MPO (multi-plane overlay) DDIs.";

        Monitors.emplace_back (monitor);
      }
    }
  }

  return true;
}

// Check if the SK_WinRing0 driver service is installed or not
std::wstring
GetDrvInstallState (DrvInstallState& ptrStatus, std::wstring svcName = L"SK_WinRing0")
{
  std::wstring       binaryPath = L"";
  SC_HANDLE        schSCManager = NULL,
                    svcWinRing0 = NULL;
  LPQUERY_SERVICE_CONFIG   lpsc = {  };
  DWORD                    dwBytesNeeded,
                            cbBufSize {};

  // Reset the current status to not installed.
  ptrStatus = NotInstalled;

  // Retrieve the install folder.
  static std::wstring dirNameInstall  = std::filesystem::path (path_cache.specialk_install ).filename();
  static std::wstring dirNameUserdata = std::filesystem::path (path_cache.specialk_userdata).filename();

  // Get a handle to the SCM database.
  schSCManager =
    OpenSCManager (
      nullptr,             // local computer
      nullptr,             // servicesActive database
      STANDARD_RIGHTS_READ // enumerate services
    );

  if (nullptr != schSCManager)
  {
    // Get a handle to the service.
    svcWinRing0 =
      OpenService (
        schSCManager,        // SCM database
        svcName.c_str(),     // name of service - Old: WinRing0_1_2_0, New: SK_WinRing0
        SERVICE_QUERY_CONFIG // query config
      );

    if (nullptr != svcWinRing0)
    {
      // Attempt to get the configuration information to get an idea of what buffer size is required.
      if (! QueryServiceConfig (
              svcWinRing0,
                nullptr, 0,
                  &dwBytesNeeded )
          )
      {
        if (ERROR_INSUFFICIENT_BUFFER == GetLastError ( ))
        {
          cbBufSize = dwBytesNeeded;
          lpsc      = (LPQUERY_SERVICE_CONFIG)LocalAlloc (LMEM_FIXED, cbBufSize);

          // Get the configuration information with the necessary buffer size.
          if (lpsc != nullptr && 
                QueryServiceConfig (
                  svcWinRing0,
                    lpsc, cbBufSize,
                      &dwBytesNeeded )
              )
          {
            // Store the binary path of the installed driver.
            binaryPath = std::wstring (lpsc->lpBinaryPathName);
            binaryPath = binaryPath.substr(4); // Strip \??\\

            PLOG_VERBOSE << "Driver " << svcName << " supposedly installed at : " << binaryPath;

            if (svcName == L"SK_WinRing0" &&
                PathFileExists (binaryPath.c_str()))
            {
              ptrStatus = Installed; // File exists, so driver is installed
              PLOG_INFO << "Found driver " << svcName << " installed at : " << binaryPath;
            }

            // Method used to detect the old copy
            else {
              PLOG_VERBOSE << "dirNameInstall:  " << dirNameInstall;
              PLOG_VERBOSE << "dirNameUserdata: " << dirNameUserdata;

              // Check if the installed driver exists, and it's in SK's folder
              if (PathFileExists      (binaryPath.c_str()) &&
                (std::wstring::npos != binaryPath.find (dirNameInstall ) ||
                 std::wstring::npos != binaryPath.find (dirNameUserdata)))
              {
                ptrStatus = ObsoleteInstalled; // File exists, so obsolete driver is installed
                PLOG_INFO << "Found obsolete driver " << svcName << " installed at : " << binaryPath;
              }
            }
          }
          else {
            PLOG_ERROR << "QueryServiceConfig failed with exception: " << SKIF_Util_GetErrorAsWStr ( );
          }

          LocalFree (lpsc);
        }
        else {
          PLOG_WARNING << "Unexpected behaviour occurred: " << SKIF_Util_GetErrorAsWStr ( );
        }
      }
      else {
        PLOG_WARNING << "Unexpected behaviour occurred: " << SKIF_Util_GetErrorAsWStr();
      }

      CloseServiceHandle (svcWinRing0);
    }
    else if (ERROR_SERVICE_DOES_NOT_EXIST == GetLastError ( ))
    {
      //PLOG_INFO << "SK_WinRing0 has not been installed.";

      static bool checkObsoleteOnce = true;
      // Check if WinRing0_1_2_0 have been installed, but only on the very first check
      if (checkObsoleteOnce && svcName == L"SK_WinRing0")
      {
        checkObsoleteOnce = false;
        GetDrvInstallState (ptrStatus, L"WinRing0_1_2_0");
      }
    }
    else {
      PLOG_ERROR << "OpenService failed with exception: " << SKIF_Util_GetErrorAsWStr ( );
    }

    CloseServiceHandle (schSCManager);
  }
  else {
    PLOG_ERROR << "OpenSCManager failed with exception: " << SKIF_Util_GetErrorAsWStr ( );
  }

  return binaryPath;
};

void
SKIF_UI_Tab_DrawSettings (void)
{
  static std::wstring
            driverBinaryPath    = L"",
            SKIFdrvFolder = SK_FormatStringW (LR"(%ws\Drivers\WinRing0\)", path_cache.specialk_install),
            SKIFdrv       = SKIFdrvFolder + L"SKIFdrv.exe", // TODO: Should be reworked to support a separate install location as well
            SYSdrv        = SKIFdrvFolder + L"WinRing0x64.sys"; // TODO: Should be reworked to support a separate install location as well
  
  static SKIF_DirectoryWatch SKIF_DriverWatch;
  static bool HDRSupported = false;

  // Driver is supposedly getting a new state -- check if its time for an
  //  update on each frame until driverStatus matches driverStatusPending
  if (driverStatusPending != driverStatus)
  {
    static DWORD dwLastDrvRefresh = 0;

    // Refresh once every 500 ms
    if (dwLastDrvRefresh < SKIF_Util_timeGetTime() && (!ImGui::IsAnyMouseDown() || !SKIF_ImGui_IsFocused()))
    {
      dwLastDrvRefresh = SKIF_Util_timeGetTime() + 500;
      driverBinaryPath = GetDrvInstallState (driverStatus);
    }
  }

  // Refresh things when visiting from another tab or when forced
  if (SKIF_Tab_Selected != UITab_Settings || RefreshSettingsTab || SKIF_DriverWatch.isSignaled (SKIFdrvFolder, true))
  {
    GetMPOSupport            (    );
    SKIF_Util_IsHDRSupported (true);
    driverBinaryPath    = GetDrvInstallState (driverStatus);
    driverStatusPending =                     driverStatus;
    RefreshSettingsTab  = false;
  }

  SKIF_Tab_Selected = UITab_Settings;
  if (SKIF_Tab_ChangeTo == UITab_Settings)
      SKIF_Tab_ChangeTo  = UITab_None;

#pragma region Section: Top / General
  // SKIF Options
  //if (ImGui::CollapsingHeader (u8"Frontend v " SKIF_VERSION_STR_A " (u8" __DATE__ ")###SKIF_SettingsHeader-1", ImGuiTreeNodeFlags_DefaultOpen))
  //{
  ImGui::PushStyleColor   (
    ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                            );

  //ImGui::Spacing    ( );

  SKIF_ImGui_Spacing      ( );

  SKIF_ImGui_Columns      (2, nullptr, true);

  SK_RunOnce(
    ImGui::SetColumnWidth (0, 510.0f * SKIF_ImGui_GlobalDPIScale) //SKIF_vecCurrentMode.x / 2.0f)
  );
          
  if ( ImGui::Checkbox ( u8"�ʹ���ģʽ",                          &_registry.bLowBandwidthMode ) )
    _registry.regKVLowBandwidthMode.putData (                                      _registry.bLowBandwidthMode );
          
  ImGui::SameLine        ( );
  ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXCLAMATION_CIRCLE);
  SKIF_ImGui_SetHoverTip (
    u8"��������Ϸ/���棬�ͷֱ���ͼ��ȸ߷ֱ���ͼ����ܻ�ӭ.\n"
    u8"��ֻ��Ӱ�쵽�����صķ��档������Ӱ���Ѿ����صķ���.\n"
    u8"��Ҳ�������¸��µ��Զ����� Special K."
  );

  if ( ImGui::Checkbox (u8"��ϲ��ͨ��Galaxy����GOG��Ϸ", &_registry.bPreferGOGGalaxyLaunch) )
    _registry.regKVPreferGOGGalaxyLaunch.putData (_registry.bPreferGOGGalaxyLaunch);

  if ( ImGui::Checkbox (u8"��ס�ϴ�ѡ�����Ϸ",         &_registry.bRememberLastSelected ) )
    _registry.regKVRememberLastSelected.putData (                    _registry.bRememberLastSelected );
            
  if ( ImGui::Checkbox (u8"��������Ϸʱ��С��",             &_registry.bMinimizeOnGameLaunch ) )
    _registry.regKVMinimizeOnGameLaunch.putData (                                      _registry.bMinimizeOnGameLaunch );
            
  if ( ImGui::Checkbox (u8"����֪ͨ����", &_registry.bCloseToTray ) )
    _registry.regKVCloseToTray.putData (                                               _registry.bCloseToTray );

  _inject._StartAtLogonCtrl ( );

  ImGui::NextColumn    ( );

  ImGui::TreePush      ( );

  // New column
          
  ImGui::BeginGroup    ( );
            
  ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXCLAMATION_CIRCLE);
  SKIF_ImGui_SetHoverTip (u8"���������������Ϸʱ���񽫳������ж೤ʱ��.\n"
    u8"������Ƶ�ÿ��ѡ�����Ի�ȡ������Ϣ");
  ImGui::SameLine        ( );
  ImGui::TextColored (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
    u8"������Ϸʱ���Զ�ֹͣ��Ϊ:"
  );
  ImGui::TreePush        ("_registry.iAutoStopBehavior");

  //if (ImGui::RadioButton (u8"Never",           &_registry.iAutoStopBehavior, 0))
  //  regKVAutoStopBehavior.putData (           _registry.iAutoStopBehavior);
  // 
  //ImGui::SameLine        ( );

  if (ImGui::RadioButton (u8"ֹͣע��",    &_registry.iAutoStopBehavior, 1))
    _registry.regKVAutoStopBehavior.putData (             _registry.iAutoStopBehavior);

  SKIF_ImGui_SetHoverTip (u8"�� Special K�ɹ�ע����Ϸʱ������ֹͣ.");

  ImGui::SameLine        ( );
  if (ImGui::RadioButton (u8"��Ϸ�˳�ʱֹͣ",      &_registry.iAutoStopBehavior, 2))
    _registry.regKVAutoStopBehavior.putData (             _registry.iAutoStopBehavior);

  SKIF_ImGui_SetHoverTip (u8"��Special K��⵽��Ϸ���ر�ʱ�����񽫱�ֹͣ.");

  ImGui::TreePop         ( );

  ImGui::Spacing         ( );

  ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXCLAMATION_CIRCLE);
  SKIF_ImGui_SetHoverTip (u8"���õʹ���ģʽ�󣬴�������Ч.");
  ImGui::SameLine        ( );
  ImGui::TextColored (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
    u8"��� Special K�ĸ���:"
  );

  if (_registry.bLowBandwidthMode)
  {
    // Disable buttons
    ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
    ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle ().Alpha * 0.5f);
  }

  ImGui::BeginGroup    ( );

  ImGui::TreePush        (u8"_registry.iCheckForUpdates");
  if (ImGui::RadioButton (u8"�Ӳ�",                 &_registry.iCheckForUpdates, 0))
    _registry.regKVCheckForUpdates.putData (                  _registry.iCheckForUpdates);
  ImGui::SameLine        ( );
  if (ImGui::RadioButton (u8"ÿ��",                &_registry.iCheckForUpdates, 1))
    _registry.regKVCheckForUpdates.putData (                  _registry.iCheckForUpdates);
  ImGui::SameLine        ( );
  if (ImGui::RadioButton (u8"ÿ������ʱ",        &_registry.iCheckForUpdates, 2))
    _registry.regKVCheckForUpdates.putData (                  _registry.iCheckForUpdates);
  ImGui::TreePop         ( );

  ImGui::EndGroup      ( );

  extern std::vector <std::pair<std::string, std::string>> updateChannels;

  if (! updateChannels.empty())
  {
    ImGui::TreePush        ("Push_UpdateChannel");

    ImGui::BeginGroup    ( );

    static std::pair<std::string, std::string>  empty           = std::pair("", "");
    static std::pair<std::string, std::string>* selectedChannel = &empty;

    static bool
        firstRun = true;
    if (firstRun)
    {   firstRun = false;
      for (auto& updateChannel : updateChannels)
        if (updateChannel.first == SK_WideCharToUTF8 (_registry.wsUpdateChannel))
          selectedChannel = &updateChannel;
    }

    if (ImGui::BeginCombo ("##SKIF_wzUpdateChannel", selectedChannel->second.c_str()))
    {
      for (auto& updateChannel : updateChannels)
      {
        bool is_selected = (selectedChannel->first == updateChannel.first);

        if (ImGui::Selectable (updateChannel.second.c_str(), is_selected) && updateChannel.first != selectedChannel->first)
        {
          // Update selection
          selectedChannel = &updateChannel;

          // Update channel
          _registry.wsUpdateChannel = SK_UTF8ToWideChar (selectedChannel->first);
          _registry.wsIgnoreUpdate  = L"";
          _registry.regKVFollowUpdateChannel.putData (_registry.wsUpdateChannel);
          _registry.regKVIgnoreUpdate       .putData (_registry.wsIgnoreUpdate);

          // Trigger a new check for updates
          extern bool changedUpdateChannel, SKIF_UpdateReady, showUpdatePrompt;
          extern std::atomic<int> update_thread_new;
          extern SKIF_UpdateCheckResults newVersion;

          changedUpdateChannel = true;
          SKIF_UpdateReady     = showUpdatePrompt = false;
          newVersion.filename.clear();
          newVersion.description.clear();
          update_thread_new.store (1);
        }

        if (is_selected)
            ImGui::SetItemDefaultFocus ( );
      }

      ImGui::EndCombo  ( );
    }

    ImGui::EndGroup      ( );

    ImGui::TreePop       ( );
  }

  else if (_registry.iCheckForUpdates > 0) {
    ImGui::TreePush      ("Push_UpdateChannel");
    ImGui::BeginGroup    ( );
    ImGui::TextColored   (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),
      u8"��Ҫ������������������ͨ��.");
    ImGui::EndGroup      ( );
    ImGui::TreePop       ( );
  }

  if (_registry.bLowBandwidthMode)
  {
    ImGui::PopStyleVar ();
    ImGui::PopItemFlag ();
  }

  ImGui::Spacing       ( );
            
  ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXCLAMATION_CIRCLE);
  SKIF_ImGui_SetHoverTip (u8"������������ֹͣʱ����Windows���ṩ������֪ͨ.");
  ImGui::SameLine        ( );
  ImGui::TextColored (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
    u8"��ʾWindows֪ͨ:"
  );
  ImGui::TreePush        ("_registry.iNotifications");
  if (ImGui::RadioButton (u8"�Ӳ�",          &_registry.iNotifications, 0))
    _registry.regKVNotifications.putData (             _registry.iNotifications);
  ImGui::SameLine        ( );
  if (ImGui::RadioButton (u8"ÿ��",         &_registry.iNotifications, 1))
    _registry.regKVNotifications.putData (             _registry.iNotifications);
  ImGui::SameLine        ( );
  if (ImGui::RadioButton (u8"��δ�۽�", &_registry.iNotifications, 2))
    _registry.regKVNotifications.putData (             _registry.iNotifications);
  ImGui::TreePop         ( );

  ImGui::Spacing       ( );
            
  ImGui::TextColored (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
    u8"��ѡ����ƽ̨������Ϸ:"
  );
  ImGui::TreePush      (u8"");

  if (ImGui::Checkbox        ("Epic", &_registry.bDisableEGSLibrary))
  {
    _registry.regKVDisableEGSLibrary.putData    (_registry.bDisableEGSLibrary);
    RepopulateGames = true;
  }

  ImGui::SameLine ( );
  ImGui::Spacing  ( );
  ImGui::SameLine ( );

  if (ImGui::Checkbox         ("GOG", &_registry.bDisableGOGLibrary))
  {
    _registry.regKVDisableGOGLibrary.putData    (_registry.bDisableGOGLibrary);
    RepopulateGames = true;
  }

  ImGui::SameLine ( );
  ImGui::Spacing  ( );
  ImGui::SameLine ( );

  if (ImGui::Checkbox       ("Steam", &_registry.bDisableSteamLibrary))
  {
    _registry.regKVDisableSteamLibrary.putData  (_registry.bDisableSteamLibrary);
    RepopulateGames = true;
  }

  ImGui::SameLine ( );
  ImGui::Spacing  ( );
  ImGui::SameLine ( );

  if (ImGui::Checkbox        ("Xbox", &_registry.bDisableXboxLibrary))
  {
    _registry.regKVDisableXboxLibrary.putData   (_registry.bDisableXboxLibrary);
    RepopulateGames = true;
  }

  ImGui::TreePop       ( );

  ImGui::TreePop    ( );

  ImGui::Columns    (1);

  ImGui::PopStyleColor();

  ImGui::Spacing ();
  ImGui::Spacing ();
#pragma endregion

#pragma region Section: Appearances
  if (ImGui::CollapsingHeader (u8"���###SKIF_SettingsHeader-1"))
  {
    ImGui::PushStyleColor   (
      ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                              );

    //ImGui::Spacing    ( );

    SKIF_ImGui_Spacing      ( );

    SKIF_ImGui_Columns      (2, nullptr, true);

    SK_RunOnce(
      ImGui::SetColumnWidth (0, 510.0f * SKIF_ImGui_GlobalDPIScale) //SKIF_vecCurrentMode.x / 2.0f)
    );

    extern bool RecreateSwapChains;

    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXCLAMATION_CIRCLE);
    SKIF_ImGui_SetHoverTip (u8"����Ӧ�õ���ɫ���.");
    ImGui::SameLine        ( );
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      u8"��ɫ���:"
    );
    
    static int placeholder = 0;
    static int* pointer = nullptr;

    if (_registry.iHDRMode > 0 && SKIF_Util_IsHDRSupported ( ))
    {
      // Disable buttons
      ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
      ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle ().Alpha * 0.5f);

      pointer = &_registry.iHDRMode;
    }
    else
      pointer = &_registry.iSDRMode;
    
    ImGui::TreePush        ("iSDRMode");
    if (ImGui::RadioButton ("8 bpc", pointer, 0))
    {
      _registry.regKVSDRMode.putData (_registry.iSDRMode);
      RecreateSwapChains = true;
    }
    if (SKIF_Util_IsWindows8Point1OrGreater ( ))
    {
      ImGui::SameLine        ( );
      if (ImGui::RadioButton ("10 bpc", pointer, 1))
      {
        _registry.regKVSDRMode.putData (_registry.iSDRMode);
        RecreateSwapChains = true;
      }
    }
    ImGui::SameLine        ( );
    if (ImGui::RadioButton ("16 bpc", pointer, 2))
    {
      _registry.regKVSDRMode.putData (_registry.iSDRMode);
      RecreateSwapChains = true;
    }
    ImGui::TreePop         ( );

    if (_registry.iHDRMode > 0 && SKIF_Util_IsHDRSupported ( ))
    {
      ImGui::PopStyleVar ();
      ImGui::PopItemFlag ();
    }

    ImGui::Spacing         ( );

    if (SKIF_Util_IsHDRSupported ( ))
    {
      ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXCLAMATION_CIRCLE);
      SKIF_ImGui_SetHoverTip (u8"ʹӦ�ó�����HDR��ʾ���ϸ�����.");
      ImGui::SameLine        ( );
      ImGui::TextColored (
        ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
          u8"�߶�̬��Χ:"
      );
      ImGui::TreePush        ("iHDRMode");
      if (ImGui::RadioButton ("No",             &_registry.iHDRMode, 0))
      {
        _registry.regKVHDRMode.putData (         _registry.iHDRMode);
        RecreateSwapChains = true;
      }
      ImGui::SameLine        ( );
      if (ImGui::RadioButton ("HDR10 (10 bpc)", &_registry.iHDRMode, 1))
      {
        _registry.regKVHDRMode.putData (         _registry.iHDRMode);
        RecreateSwapChains = true;
      }
      ImGui::SameLine        ( );
      if (ImGui::RadioButton ("scRGB (16 bpc)", &_registry.iHDRMode, 2))
      {
        _registry.regKVHDRMode.putData (         _registry.iHDRMode);
        RecreateSwapChains = true;
      }

      ImGui::Spacing         ( );

      if (_registry.iHDRMode == 0)
      {
        // Disable buttons
        ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle ().Alpha * 0.5f);
      }

      if (ImGui::SliderInt(u8"HDR����", &_registry.iHDRBrightness, 100, 400, "%d nits"))
        _registry.regKVHDRBrightness.putData (_registry.iHDRBrightness);

      if (_registry.iHDRMode == 0)
      {
        ImGui::PopStyleVar ();
        ImGui::PopItemFlag ();
      }

      ImGui::TreePop         ( );

      ImGui::Spacing         ( );
    }

    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXCLAMATION_CIRCLE);
    SKIF_ImGui_SetHoverTip (u8"����㷢�������İ�ɫ������һ������.");
    ImGui::SameLine        ( );
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        u8"��������Ϸ���� 25%%:"
    );
    ImGui::TreePush        ("iDimCovers");
    if (ImGui::RadioButton (u8"�Ӳ�",                 &_registry.iDimCovers, 0))
      _registry.regKVDimCovers.putData (                        _registry.iDimCovers);
    ImGui::SameLine        ( );
    if (ImGui::RadioButton (u8"����",                &_registry.iDimCovers, 1))
      _registry.regKVDimCovers.putData (                        _registry.iDimCovers);
    ImGui::SameLine        ( );
    if (ImGui::RadioButton (u8"���������r", &_registry.iDimCovers, 2))
      _registry.regKVDimCovers.putData (                        _registry.iDimCovers);
    ImGui::TreePop         ( );

    ImGui::Spacing         ( );
          
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXCLAMATION_CIRCLE);
    SKIF_ImGui_SetHoverTip (u8"������Ƶ�ÿ��ѡ�����Ի�ȡ������Ϣ.");
    ImGui::SameLine        ( );
    ImGui::TextColored     (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      u8"����UIԪ��:"
    );
    ImGui::TreePush        ("");

    if (ImGui::Checkbox ("HiDPI scaling", &_registry.bDisableDPIScaling))
    {
      ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;

      if (_registry.bDisableDPIScaling)
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_DpiEnableScaleFonts;

      _registry.regKVDisableDPIScaling.putData      (_registry.bDisableDPIScaling);
    }

    SKIF_ImGui_SetHoverTip (
      u8"���Ӧ�ó�����HiDPI��ʾ���ϻ��Եø�С."
    );

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    if (ImGui::Checkbox (u8"������ʾ", &_registry.bDisableTooltips))
      _registry.regKVDisableTooltips.putData (  _registry.bDisableTooltips);

    if (ImGui::IsItemHovered ())
      SKIF_StatusBarText = "Info: ";

    SKIF_ImGui_SetHoverText (u8"������ʾ��Ϣ�ĵط�.");
    SKIF_ImGui_SetHoverTip  (u8"��Ϣ����ʾ�ڵײ���״̬����."
      u8"\n��ע�⣬��Щ��������޷�Ԥ��.");

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    if (ImGui::Checkbox (u8"״̬��", &_registry.bDisableStatusBar))
      _registry.regKVDisableStatusBar.putData (   _registry.bDisableStatusBar);

    SKIF_ImGui_SetHoverTip (
      u8"���˹�������õ�UI������ʾ���ϣ����������л��������ĵ���Ϣ����ʾ."
    );

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    if (ImGui::Checkbox (u8"�߽�", &_registry.bDisableBorders))
    {
      _registry.regKVDisableBorders.putData (  _registry.bDisableBorders);
      if (_registry.bDisableBorders)
      {
        ImGui::GetStyle().TabBorderSize   = 0.0F;
        ImGui::GetStyle().FrameBorderSize = 0.0F;
      }
      else {
        ImGui::GetStyle().TabBorderSize   = 1.0F * SKIF_ImGui_GlobalDPIScale;
        ImGui::GetStyle().FrameBorderSize = 1.0F * SKIF_ImGui_GlobalDPIScale;
      }
      if (_registry.iStyle == 0)
        SKIF_ImGui_StyleColorsDark ( );
    }

    if (_registry.bDisableTooltips &&
        _registry.bDisableStatusBar)
    {
      ImGui::BeginGroup     ( );
      ImGui::TextColored    (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXCLAMATION_CIRCLE);
      ImGui::SameLine       ( );
      ImGui::TextColored    (ImColor(0.68F, 0.68F, 0.68F, 1.0f), u8"���������ĵ���Ϣ����ʾ���������!");
      ImGui::EndGroup       ( );
    }

    ImGui::TreePop       ( );

    ImGui::NextColumn    ( );

    ImGui::TreePush      ( );
            
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXCLAMATION_CIRCLE);
    SKIF_ImGui_SetHoverTip (u8"ÿ��UI��Ⱦһ��֡ʱ��Shelly the Ghost�����ƶ�һ��.");
    ImGui::SameLine        ( );
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Show Shelly the Ghost:"
    );
    ImGui::TreePush        ("_registry.iGhostVisibility");
    if (ImGui::RadioButton (u8"�Ӳ�",                    &_registry.iGhostVisibility, 0))
      _registry.regKVGhostVisibility.putData (                     _registry.iGhostVisibility);
    ImGui::SameLine        ( );
    if (ImGui::RadioButton (u8"����",                   &_registry.iGhostVisibility, 1))
      _registry.regKVGhostVisibility.putData (                     _registry.iGhostVisibility);
    ImGui::SameLine        ( );
    if (ImGui::RadioButton (u8"��������ʱ", &_registry.iGhostVisibility, 2))
      _registry.regKVGhostVisibility.putData (                     _registry.iGhostVisibility);
    ImGui::TreePop         ( );

    ImGui::Spacing       ( );

    // Only show if OS supports tearing in windowed mode
    extern bool SKIF_bCanAllowTearing;
    if (SKIF_bCanAllowTearing)
    {
      ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXCLAMATION_CIRCLE);
      SKIF_ImGui_SetHoverTip (u8"������Ƶ�ÿ��ѡ�����Ի�ȡ������Ϣ");
      ImGui::SameLine        ( );
      ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                              u8"UIˢ��ģʽ:(��Ҫ����)"
      );

      enum SKIF_SyncModes {
        Sync_VRR_Compat = 0,
        Sync_None       = 1
      };

      int SKIF_iSyncMode =
        _registry.bDisableVSYNC ? Sync_None
                           : Sync_VRR_Compat;

      ImGui::TreePush        ("SKIF_iSyncMode");
      if (ImGui::RadioButton (u8"VRR ������", &SKIF_iSyncMode, Sync_VRR_Compat))
        _registry.regKVDisableVSYNC.putData ((_registry.bDisableVSYNC = false));
      SKIF_ImGui_SetHoverTip (
        u8"����VRR��ʾ���ϵ��źŶ�ʧ����˸"
      );
      ImGui::SameLine        ( );
      if (ImGui::RadioButton (u8"������",         &SKIF_iSyncMode, Sync_None))
        _registry.regKVDisableVSYNC.putData ((_registry.bDisableVSYNC = true));
      SKIF_ImGui_SetHoverTip (
        u8"���Ƶ͹̶�ˢ������ʾ����UI��Ӧ"
      );
      ImGui::TreePop         ( );
      ImGui::Spacing         ( );
    }

    const char* StyleItems[] = { "SKIF Dark",
                                 "ImGui Dark",
                                 "ImGui Light",
                                 "ImGui Classic"
    };
    static const char* StyleItemsCurrent = StyleItems[_registry.iStyle];
          
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        u8"��ɫ����: (��������Ҫ��)"
    );
    ImGui::TreePush      ("");

    if (ImGui::BeginCombo ("##_registry.iStyleCombo", StyleItemsCurrent)) // The second parameter is the label previewed before opening the combo.
    {
        for (int n = 0; n < IM_ARRAYSIZE (StyleItems); n++)
        {
            bool is_selected = (StyleItemsCurrent == StyleItems[n]); // You can store your selection however you want, outside or inside your objects
            if (ImGui::Selectable (StyleItems[n], is_selected))
            {
              _registry.iStyle = n;
              _registry.regKVStyle.putData  (_registry.iStyle);
              StyleItemsCurrent = StyleItems[_registry.iStyle];
              // Apply the new Dear ImGui style
              //SKIF_SetStyle ( );
            }
            if (is_selected)
                ImGui::SetItemDefaultFocus ( );   // You may set the initial focus when opening the combo (scrolling + for keyboard navigation support)
        }
        ImGui::EndCombo  ( );
    }

    ImGui::TreePop       ( );

    ImGui::TreePop       ( );

    ImGui::Columns       (1);

    ImGui::PopStyleColor ( );
  }

  ImGui::Spacing ();
  ImGui::Spacing ();
#pragma endregion

#pragma region Section: Advanced
  if (ImGui::CollapsingHeader (u8"�߼�###SKIF_SettingsHeader-2"))
  {
    ImGui::PushStyleColor   (
      ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                              );

    SKIF_ImGui_Spacing      ( );

    SKIF_ImGui_Columns      (2, nullptr, true);

    SK_RunOnce(
      ImGui::SetColumnWidth (0, 510.0f * SKIF_ImGui_GlobalDPIScale) //SKIF_vecCurrentMode.x / 2.0f)
    );

    if ( ImGui::Checkbox ( u8"ʼ�����������ͬ����ʾ���ϴ򿪴�Ӧ�ó���", &_registry.bOpenAtCursorPosition ) )
      _registry.regKVOpenAtCursorPosition.putData (                                            _registry.bOpenAtCursorPosition );

    if ( ImGui::Checkbox (
            u8"�����Ӧ�ó���Ķ��ʵ��",
              &_registry.bAllowMultipleInstances )
        )
    {
      if (! _registry.bAllowMultipleInstances)
      {
        // Immediately close out any duplicate instances, they're undesirables
        EnumWindows ( []( HWND   hWnd,
                          LPARAM lParam ) -> BOOL
        {
          wchar_t                         wszRealWindowClass [64] = { };
          if (RealGetWindowClassW (hWnd,  wszRealWindowClass, 64))
          {
            if (StrCmpIW ((LPWSTR)lParam, wszRealWindowClass) == 0)
            {
              if (SKIF_hWnd != hWnd)
                PostMessage (  hWnd, WM_QUIT,
                                0x0, 0x0  );
            }
          }
          return TRUE;
        }, (LPARAM)SKIF_WindowClass);
      }

      _registry.regKVAllowMultipleInstances.putData (
        _registry.bAllowMultipleInstances
        );
    }

    ImGui::NextColumn       ( );

    ImGui::TreePush         ( );

    if (ImGui::Checkbox  (u8"��Ҫ��Ӧ�ùر�ʱֹͣע�����",
                                            &_registry.bAllowBackgroundService))
      _registry.regKVAllowBackgroundService.putData (  _registry.bAllowBackgroundService);

    const char* LogSeverity[] = { "None",
                                  "Fatal",
                                  "Error",
                                  "Warning",
                                  "Info",
                                  "Debug",
                                  "Verbose" };
    static const char* LogSeverityCurrent = LogSeverity[_registry.iLogging];
          
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Logging:"
    );

    ImGui::SameLine();

    if (ImGui::BeginCombo ("##_registry.iLoggingCombo", LogSeverityCurrent)) // The second parameter is the label previewed before opening the combo.
    {
      for (int n = 0; n < IM_ARRAYSIZE (LogSeverity); n++)
      {
        bool is_selected = (LogSeverityCurrent == LogSeverity[n]); // You can store your selection however you want, outside or inside your objects
        if (ImGui::Selectable (LogSeverity[n], is_selected))
        {
          _registry.iLogging = n;
          _registry.regKVLogging.putData  (_registry.iLogging);
          LogSeverityCurrent = LogSeverity[_registry.iLogging];
          plog::get()->setMaxSeverity((plog::Severity)_registry.iLogging);
        }
        if (is_selected)
            ImGui::SetItemDefaultFocus ( );   // You may set the initial focus when opening the combo (scrolling + for keyboard navigation support)
      }
      ImGui::EndCombo  ( );
    }

    ImGui::TreePop          ( );

    ImGui::Columns          (1);

    ImGui::PopStyleColor    ( );
  }

  ImGui::Spacing ();
  ImGui::Spacing ();
#pragma endregion

#pragma region Section: Whitelist / Blacklist
  if (ImGui::CollapsingHeader (u8"������ / ������###SKIF_SettingsHeader-5")) //, ImGuiTreeNodeFlags_DefaultOpen)) // Disabled auto-open for this section
  {
    static bool white_edited = false,
                black_edited = false,
                white_stored = true,
                black_stored = true;

    auto _CheckWarnings = [](char* szList)->void
    {
      static int i, count;

      if (strchr (szList, '\"') != nullptr)
      {
        ImGui::BeginGroup ();
        ImGui::Spacing    ();
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow),     ICON_FA_EXCLAMATION_TRIANGLE);
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),    "Please remove all double quotes");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Failure), R"( " )");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),    "from the list.");
        ImGui::EndGroup   ();
      }

      // Loop through the list, checking the existance of a lone \ not proceeded or followed by other \.
      // i == 0 to prevent szList[i - 1] from executing when at the first character.
      for (i = 0, count = 0; szList[i] != '\0' && i < MAX_PATH * 128 * 2; i++)
        count += (szList[i] == '\\' && szList[i + 1] != '\\' && (i == 0 || szList[i - 1] != '\\'));

      if (count > 0)
      {
        ImGui::BeginGroup ();
        ImGui::Spacing    ();
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),      ICON_FA_EXCLAMATION_CIRCLE);
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),   "Folders must be separated using two backslashes");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), R"( \\ )");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),   "instead of one");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Failure), R"( \ )");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),   "backslash.");
        ImGui::EndGroup   ();

        SKIF_ImGui_SetHoverTip (
          R"(e.g. C:\\Program Files (x86)\\Uplay\\games)"
        );
      }

      // Loop through the list, counting the number of occurances of a newline
      for (i = 0, count = 0; szList[i] != '\0' && i < MAX_PATH * 128 * 2; i++)
        count += (szList[i] == '\n');

      if (count >= 128)
      {
        ImGui::BeginGroup ();
        ImGui::Spacing    ();
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),      ICON_FA_EXCLAMATION_CIRCLE);
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),   "The list can only include");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Failure),   " 128 ");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),   "lines, though multiple can be combined using a pipe");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success),   " | ");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),   "character.");
        ImGui::EndGroup   ();

        SKIF_ImGui_SetHoverTip (
          R"(e.g. "NieRAutomataPC|Epic Games" will match any application"
                  "installed under a NieRAutomataPC or Epic Games folder.)"
        );
      }
    };

    ImGui::BeginGroup ();
    ImGui::Spacing    ();

    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase));
    ImGui::TextWrapped    (u8"�����б��������е�Special K����Ϊģʽ��ע����̵�����·����ƥ��.");

    ImGui::Spacing    ();
    ImGui::Spacing    ();

    ImGui::BeginGroup ();
    ImGui::Spacing    ();
    ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),   ICON_FA_EXCLAMATION_CIRCLE);
    ImGui::SameLine   (); ImGui::Text        (u8"��򵥵ķ�����ʹ����Ϸ�Ŀ�ִ���ļ����ļ��е�����.");
    ImGui::EndGroup   ();

    SKIF_ImGui_SetHoverTip (
      "e.g. a pattern like \"Assassin's Creed Valhalla\" will match an application at"
        "\nC:\\Games\\Uplay\\games\\Assassin's Creed Valhalla\\ACValhalla.exe"
    );

    ImGui::BeginGroup ();
    ImGui::Spacing    ();
    ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),   ICON_FA_EXCLAMATION_CIRCLE);
    ImGui::SameLine   (); ImGui::Text        (u8"���빲���ļ��е����ƽ�ƥ����ļ����µ�����Ӧ�ó���.");
    ImGui::EndGroup   ();

    SKIF_ImGui_SetHoverTip (
      "e.g. a pattern like \"Epic Games\" will match any"
        "\napplication installed under the Epic Games folder."
    );

    ImGui::Spacing    ();
    ImGui::Spacing    ();

    ImGui::BeginGroup ();
    ImGui::Spacing    ();
    ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), ICON_FA_EXCLAMATION_CIRCLE);
    ImGui::SameLine   (); ImGui::Text (u8"ע�⣬��Щ�б�������ֹSpecial K��ע�뵽������.");
    ImGui::EndGroup   ();

    if (_registry.bDisableTooltips)
    {
      SKIF_ImGui_SetHoverTip (
        u8"��Щ�б�����Ƿ�Ӧ������Special K(������)���ҹ�api��,"
        u8"\n����ע������б��ֽ���/����/����(������)."
      );
    }

    else
    {
      SKIF_ImGui_SetHoverTip (
        u8"ȫ��ע�����Special Kע�뵽�κδ���"
        u8"\n��ϵͳ�����ĳ�ִ��ڻ����/�������."
        "\n\n"


        u8"��Щ�б�����Ƿ�Ӧ������Special K(������),"
        u8"\n����ע������б��ֿ���/����(������)."
      );
    }

    /*
    ImGui::BeginGroup ();
    ImGui::Spacing    ();
    ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXTERNAL_LINK_ALT);
    ImGui::SameLine   (); ImGui::Text        (u8"More on the wiki.");
    ImGui::EndGroup   ();

    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText       (u8"https://wiki.special-k.info/en/SpecialK/Global#the-global-injector-and-multiplayer-games");

    if (ImGui::IsItemClicked ())
      SKIF_Util_OpenURI (L"https://wiki.special-k.info/en/SpecialK/Global#the-global-injector-and-multiplayer-games");
    */

    ImGui::PopStyleColor  ();

    ImGui::NewLine    ();

    // Whitelist section

    ImGui::BeginGroup ();

    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), ICON_FA_PLUS_CIRCLE);
    ImGui::SameLine    ( );
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      u8"������ģʽ:"
    );

    SKIF_ImGui_Spacing ();

    white_edited |=
      ImGui::InputTextEx ( "###WhitelistPatterns", "SteamApps\nEpic Games\\\\\nGOG Galaxy\\\\Games\nOrigin Games\\\\",
                              _inject.whitelist, MAX_PATH * 128 - 1,
                                ImVec2 ( 700 * SKIF_ImGui_GlobalDPIScale,
                                        150 * SKIF_ImGui_GlobalDPIScale ), // 120 // 150
                                  ImGuiInputTextFlags_Multiline );

    if (*_inject.whitelist == '\0')
    {
      SKIF_ImGui_SetHoverTip (
        u8"��Щ���ڲ�ʹ�õ�ģʽ������Ϊ��Щ�ض�ƽ̨����Special K."
        u8"\n���������ֻ����ΪǱ��ģʽ��ʾ��."
      );
    }

    _CheckWarnings (_inject.whitelist);

    ImGui::EndGroup   ();

    ImGui::SameLine   ();

    ImGui::BeginGroup ();

    ImGui::TextColored (ImColor(255, 207, 72), ICON_FA_FOLDER_PLUS);
    ImGui::SameLine    ( );
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        u8"��ӳ���ģʽ:"
    );

    SKIF_ImGui_Spacing ();

    ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase));
    ImGui::TextWrapped    (u8"�����������Ŀ�Խ�����ӵ��������У��������ͣ������ "
      u8"����ʾ�й�ģʽ���������ݵĸ�����Ϣ.");
    ImGui::PopStyleColor  ();

    SKIF_ImGui_Spacing ();
    SKIF_ImGui_Spacing ();

    ImGui::SameLine    ();
    ImGui::BeginGroup  ();
    ImGui::TextColored ((_registry.iStyle == 2) ? ImColor(0, 0, 0) : ImColor(255, 255, 255), ICON_FA_WINDOWS);
    //ImGui::TextColored ((_registry.iStyle == 2) ? ImColor(0, 0, 0) : ImColor(255, 255, 255), ICON_FA_XBOX);
    ImGui::EndGroup    ();

    ImGui::SameLine    ();

    ImGui::BeginGroup  ();
    if (ImGui::Selectable (u8"Games"))
    {
      white_edited = true;

      _inject._AddUserList(u8"Games", true);
    }

    SKIF_ImGui_SetHoverTip (
      "Whitelists games on most platforms, such as Uplay, as"
      "\nmost of them have 'games' in the full path somewhere."
    );

    /*
    if (ImGui::Selectable (u8"WindowsApps"))
    {
      white_edited = true;

      _inject._AddUserList(u8"WindowsApps", true);
    }

    SKIF_ImGui_SetHoverTip (
      "Whitelists games on the Microsoft Store or Game Pass."
    );
    */

    ImGui::EndGroup ();
    ImGui::EndGroup ();

    ImGui::Spacing  ();
    ImGui::Spacing  ();

    // Blacklist section

    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Failure), ICON_FA_MINUS_CIRCLE);
    ImGui::SameLine    ( );
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "������ģʽ:"
    );
    ImGui::SameLine    ( );
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase), u8"(����ע��!ʹ�ô�ѡ����Խ�ĳЩ�����ų��ڰ�����֮��)");

    SKIF_ImGui_Spacing ();

    black_edited |=
      ImGui::InputTextEx ( "###BlacklistPatterns", "launcher.exe",
                              _inject.blacklist, MAX_PATH * 128 - 1,
                                ImVec2 ( 700 * SKIF_ImGui_GlobalDPIScale,
                                          120 * SKIF_ImGui_GlobalDPIScale ),
                                  ImGuiInputTextFlags_Multiline );

    _CheckWarnings (_inject.blacklist);

    ImGui::Separator ();

    bool bDisabled =
      (white_edited || black_edited) ?
                                false : true;

    if (bDisabled)
    {
      ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
      ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle ().Alpha * 0.5f);
    }

    // Hotkey: Ctrl+S
    if (ImGui::Button (ICON_FA_SAVE " Save Changes") || ((!bDisabled) && ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeysDown['S']))
    {
      // Clear the active ID to prevent ImGui from holding outdated copies of the variable
      //   if saving succeeds, to allow _StoreList to update the variable successfully
      ImGui::ClearActiveID();

      if (white_edited)
      {
        white_stored = _inject._StoreList(true);

        if (white_stored)
          white_edited = false;
      }

      if (black_edited)
      {
        black_stored = _inject._StoreList (false);

        if (black_stored)
          black_edited = false;
      }
    }

    ImGui::SameLine ();

    if (ImGui::Button (ICON_FA_UNDO " Reset"))
    {
      if (white_edited)
      {
        _inject._LoadList (true);

        white_edited = false;
        white_stored = true;
      }

      if (black_edited)
      {
        _inject._LoadList(false);

        black_edited = false;
        black_stored = true;
      }
    }

    if (bDisabled)
    {
      ImGui::PopItemFlag  ( );
      ImGui::PopStyleVar  ( );
    }

    ImGui::Spacing();

    if (! white_stored)
    {
        ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),  (const char *)u8"\u2022 ");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), "The whitelist could not be saved! Please remove any non-Latin characters and try again.");
    }

    if (! black_stored)
    {
        ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),  (const char *)u8"\u2022 ");
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning), "The blacklist could not be saved! Please remove any non-Latin characters and try again.");
    }

    ImGui::EndGroup       ( );
  }

  ImGui::Spacing ();
  ImGui::Spacing ();
#pragma endregion
  
#pragma region Section: Extended CPU Hardware Reporting [64-bit only]
#ifdef _WIN64
  if (ImGui::CollapsingHeader (u8"��չCPUӲ������###SKIF_SettingsHeader-4"))
  {
    ImGui::PushStyleColor (
      ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                            );

    SKIF_ImGui_Spacing      ( );

    // WinRing0
    ImGui::BeginGroup  ();

    ImGui::TextWrapped    (
      u8"Special K����ʹ�ÿ�ѡ���ں�����������CPUС�������ṩ����Ķ���."
    );

    ImGui::Spacing     ();
    ImGui::Spacing     ();

    ImGui::BeginGroup  ();
    ImGui::Spacing     ();
    ImGui::SameLine    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
    ImGui::SameLine    ();
    ImGui::Text        (u8"���ִ�Ӳ������չCPUС�����������������;�ȷ��ʱ������.");
    ImGui::EndGroup    ();

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                        u8"Ҫ��:"
    );

    ImGui::BeginGroup  ();
    ImGui::Spacing     ();
    ImGui::SameLine    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
    ImGui::SameLine    ();
    ImGui::Text        (u8"�ں���������:");
    ImGui::SameLine    ();

    static std::string btnDriverLabel;
    static std::wstring wszDriverTaskCmd;
    //static LPCSTR szDriverTaskFunc;

    // Status is pending...
    if (driverStatus != driverStatusPending)
    {
      btnDriverLabel = ICON_FA_SPINNER " Please Wait...";
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), "Pending...");
    }

    // Driver is installed
    else if (driverStatus == Installed)
    {
      btnDriverLabel    = ICON_FA_SHIELD_ALT " Uninstall Driver";
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), "Installed");
      wszDriverTaskCmd = L"Uninstall";
    }

    // Other driver is installed
    else if (driverStatus == OtherDriverInstalled)
    {
      btnDriverLabel    = ICON_FA_BAN " Unavailable";
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), "Unsupported");
      ImGui::Spacing     ();
      ImGui::SameLine    ();
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
      ImGui::SameLine    ();
      ImGui::Text        (u8"Conflict With:");
      ImGui::SameLine    ();
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption), SK_WideCharToUTF8 (driverBinaryPath).c_str ());
    }

    // Obsolete driver is installed
    else if (driverStatus == ObsoleteInstalled)
    {
      btnDriverLabel    = ICON_FA_SHIELD_ALT " Migrate Driver";
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), "Obsolete driver installed");
      wszDriverTaskCmd = L"Migrate Install";
    }

    // Driver is not installed
    else {
      btnDriverLabel    = ICON_FA_SHIELD_ALT " Install Driver";
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), "Not Installed");
      wszDriverTaskCmd = L"Install";
    }

    ImGui::EndGroup ();

    ImGui::Spacing  ();
    ImGui::Spacing  ();

    // Disable button if the required files are missing, status is pending, or if another driver is installed
    if (  driverStatusPending  != driverStatus ||
          OtherDriverInstalled == driverStatus )
    {
      ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
      ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle ().Alpha * 0.5f);
    }

    // Show button
    bool driverButton =
      ImGui::ButtonEx (btnDriverLabel.c_str(), ImVec2(200 * SKIF_ImGui_GlobalDPIScale,
                                                       25 * SKIF_ImGui_GlobalDPIScale));
    SKIF_ImGui_SetHoverTip (
      "Administrative privileges are required on the system to enable this."
    );

    if ( driverButton )
    {
      if (PathFileExists (SKIFdrv.c_str()) && PathFileExists (SYSdrv.c_str()))
      {
        if (ShellExecuteW (nullptr, L"runas", SKIFdrv.c_str(), wszDriverTaskCmd.c_str(), nullptr, SW_SHOW) > (HINSTANCE)32)
          driverStatusPending =
                (driverStatus == Installed) ?
                              NotInstalled  : Installed;
      }
      else {
        SKIF_Util_OpenURI (L"https://wiki.special-k.info/en/SpecialK/Tools#extended-hardware-monitoring-driver");
      }
    }

    // Disabled button
    //   the 'else if' is only to prevent the code from being called on the same frame as the button is pressed
    else if (    driverStatusPending != driverStatus ||
                OtherDriverInstalled == driverStatus )
    {
      ImGui::PopStyleVar ();
      ImGui::PopItemFlag ();
    }

    // Show warning about another driver being installed
    if (OtherDriverInstalled == driverStatus)
    {
      ImGui::SameLine   ();
      ImGui::BeginGroup ();
      ImGui::Spacing    ();
      ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),
                                                "Option is unavailable as another application have already installed a copy of the driver."
      );
      ImGui::EndGroup   ();
    } 

    // Show warning about another driver being installed
    else if (ObsoleteInstalled == driverStatus)
    {
      ImGui::SameLine();
      ImGui::BeginGroup();
      ImGui::Spacing();
      ImGui::SameLine(); ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
        u8"��װ�˾ɰ汾����������."
      );
      ImGui::EndGroup();
    }

    ImGui::EndGroup ();

    ImGui::PopStyleColor ();
  }

  ImGui::Spacing ();
  ImGui::Spacing ();
#endif
#pragma endregion
  
#pragma region Section: SwapChain Presentation Monitor
  if (ImGui::CollapsingHeader (u8"SwapChain ��ʾ������###SKIF_SettingsHeader-3", ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImGui::PushStyleColor (
      ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                            );

    SKIF_ImGui_Spacing      ( );

    // PresentMon prerequisites
    ImGui::BeginGroup  ();

    SKIF_ImGui_Columns      (2, nullptr, true);

            
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                        u8"��һ�۾�֪���Ƿ�:"
    );

    ImGui::BeginGroup  ();
    ImGui::Spacing     ();
    ImGui::SameLine    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXTERNAL_LINK_ALT);
    ImGui::SameLine    ();
    ImGui::TextWrapped (u8"DirectFlip�������Ż��������ƹ����������(DWM).");
    ImGui::EndGroup    ();

    SKIF_ImGui_SetHoverTip(u8"��ʾΪ��Ӳ��:������ת����Ӳ�����:������ת��");
    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText       (u8"https://wiki.special-k.info/en/SwapChain#fse-fso-independent-flip-etc-sorry-but-what");

    if (ImGui::IsItemClicked      ())
      SKIF_Util_OpenURI           (L"https://wiki.special-k.info/en/SwapChain#fse-fso-independent-flip-etc-sorry-but-what");

    ImGui::BeginGroup  ();
    ImGui::Spacing     ();
    ImGui::SameLine    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXTERNAL_LINK_ALT);
    ImGui::SameLine    ();
    ImGui::TextWrapped (u8"��ͳ��ռȫ��(FSE)ģʽ�����ã�����ȫ���Ż�(FSO)��������.");
    ImGui::EndGroup    ();

    SKIF_ImGui_SetHoverTip(
                        u8"FSE��ʾΪ��Ӳ��:������ת����Ӳ��:����������ǰ����������"
                        u8"\nFSO��ʾΪ��Ӳ��:������ת����Ӳ�����:������ת��"
    );
    SKIF_ImGui_SetMouseCursorHand ();
    SKIF_ImGui_SetHoverText       (u8"https://www.pcgamingwiki.com/wiki/Windows#Fullscreen_optimizations");

    if (ImGui::IsItemClicked      ())
      SKIF_Util_OpenURI           (L"https://www.pcgamingwiki.com/wiki/Windows#Fullscreen_optimizations");

    ImGui::BeginGroup  ();
    ImGui::Spacing     ();
    ImGui::SameLine    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXCLAMATION_CIRCLE);
    ImGui::SameLine    ();
    ImGui::TextWrapped (u8"��Ϸ�Դ��ų���ģʽ����.");
    ImGui::EndGroup    ();

    SKIF_ImGui_SetHoverTip(u8"��ʾΪ '���: Flip', '���: Composition Atlas',"
                            "\n'���: Copy with CPU GDI', or '���: Copy with GPU GDI'");

    ImGui::Spacing();
    ImGui::Spacing();
            
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                        u8"Ҫ��:"
    );

    static BOOL  pfuAccessToken = FALSE;
    static BYTE  pfuSID[SECURITY_MAX_SID_SIZE];
    static BYTE  intSID[SECURITY_MAX_SID_SIZE];
    static DWORD pfuSize = sizeof(pfuSID);
    static DWORD intSize = sizeof(intSID);

    SK_RunOnce (CreateWellKnownSid   (WELL_KNOWN_SID_TYPE::WinBuiltinPerfLoggingUsersSid, NULL, &pfuSID, &pfuSize));
    SK_RunOnce (CreateWellKnownSid   (WELL_KNOWN_SID_TYPE::WinInteractiveSid,             NULL, &intSID, &intSize));
    SK_RunOnce (CheckTokenMembership (NULL, &pfuSID, &pfuAccessToken));

    enum pfuPermissions {
      Missing,
      Granted,
      Pending
    } static pfuState = (pfuAccessToken) ? Granted : Missing;

    ImGui::BeginGroup  ();
    ImGui::Spacing     ();
    ImGui::SameLine    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
    ImGui::SameLine    ();
    ImGui::Text        (u8"�����衰������־�û���Ȩ��?");
    ImGui::SameLine    ();
    if      (pfuState == Granted)
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), u8"��");
    else if (pfuState == Missing)
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), u8"��");
    else // (pfuState == Pending)
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), u8"��������Ҫ��"); //"Yes, but a sign out from Windows is needed to allow the changes to take effect.");
    ImGui::EndGroup    ();

    ImGui::Spacing  ();
    ImGui::Spacing  ();

    // Disable button for granted + pending states
    if (pfuState != Missing)
    {
      ImGui::PushItemFlag (ImGuiItemFlags_Disabled, true);
      ImGui::PushStyleVar (ImGuiStyleVar_Alpha, ImGui::GetStyle ().Alpha * 0.5f);
    }

    std::string btnPfuLabel = (pfuState == Granted) ?                                ICON_FA_CHECK " Permissions granted!" // Granted
                                                    : (pfuState == Missing) ?   ICON_FA_SHIELD_ALT " Grant permissions"    // Missing
                                                                            : ICON_FA_SIGN_OUT_ALT " Sign out to apply";   // Pending

    if ( ImGui::ButtonEx ( btnPfuLabel.c_str(), ImVec2( 200 * SKIF_ImGui_GlobalDPIScale,
                                                         25 * SKIF_ImGui_GlobalDPIScale)))
    {
      std::wstring exeArgs;

      TCHAR pfuName[MAX_PATH],
            intName[MAX_PATH];
      DWORD pfuNameLength = sizeof(pfuName),
            intNameLength = sizeof(intName);

      // Unused variables
      SID_NAME_USE pfuSnu, intSnu;
      TCHAR pfuDomainName[MAX_PATH], 
            intDomainName[MAX_PATH];
      DWORD pfuDomainNameLength = sizeof(pfuDomainName),
            intDomainNameLength = sizeof(intDomainName);

      // Because non-English languages has localized user and group names, we need to retrieve those first
      if (LookupAccountSid (NULL, pfuSID, pfuName, &pfuNameLength, pfuDomainName, &pfuDomainNameLength, &pfuSnu) &&
          LookupAccountSid (NULL, intSID, intName, &intNameLength, intDomainName, &intDomainNameLength, &intSnu))
      {
        exeArgs = LR"(localgroup ")" + std::wstring(pfuName) + LR"(" ")" + std::wstring(intName) + LR"(" /add)";

        // Use 'net' to grant the proper permissions
        if (ShellExecuteW (nullptr, L"runas", L"net", exeArgs.c_str(), nullptr, SW_SHOW) > (HINSTANCE)32)
          pfuState = Pending;
      }
    }

    // Disable button for granted + pending states
    else if (pfuState != Missing)
    {
      ImGui::PopStyleVar();
      ImGui::PopItemFlag();
    }

    else
    {
      SKIF_ImGui_SetHoverTip(
        u8"��Ҫϵͳ�ϵĹ���Ȩ�޲��ܽ����л�."
      );
    }

    ImGui::EndGroup ();

    ImGui::NextColumn  ();

    ImGui::TreePush    ();

    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), ICON_FA_THUMBS_UP);
    ImGui::SameLine    ( );
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), u8"��С���ӳ�:");

    ImGui::TreePush    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
    ImGui::SameLine    ();
    ImGui::Text        (u8"Ӳ��:������ת");

    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
    ImGui::SameLine    ();
    ImGui::Text        (u8"Ӳ�����:������ת");

    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
    ImGui::SameLine    ();
    ImGui::Text        (u8"Ӳ��:������ת");

    /* Extremely uncommon but included in the list anyway */
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
    ImGui::SameLine    ();
    ImGui::TextColored (ImColor      (0.68F, 0.68F, 0.68F), "Ӳ��:����������ǰ������");
    
    ImGui::TreePop     ();

    SKIF_ImGui_Spacing ();
            
    ImGui::TextColored (ImColor::HSV (0.11F, 1.F, 1.F), ICON_FA_THUMBS_DOWN);
    ImGui::SameLine    ();
    ImGui::TextColored (ImColor::HSV (0.11F, 1.F, 1.F), u8"��ϣ���ӳ�:");

    ImGui::TreePush    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
    ImGui::SameLine    ();
    ImGui::Text        (u8"���: Flip");

    /* Disabled as PresentMon doesn't detect this any longer as of May 2022.
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
    ImGui::SameLine    ();
    ImGui::Text        (u8"Composed: Composition Atlas");
    */

    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
    ImGui::SameLine    ();
    ImGui::Text        (u8"���: Copy with GPU GDI");

    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
    ImGui::SameLine    ();
    ImGui::Text        (u8"���: Copy with CPU GDI");
    ImGui::TreePop     ();

    ImGui::TreePop     ();

    ImGui::Columns     (1);
    
    ImGui::Spacing     ();
    ImGui::Spacing     ();

    ImGui::Separator   ();

    // Multi-Plane Overlay (MPO) section
    ImGui::BeginGroup  ();
    ImGui::Spacing     ();
            
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                        u8"Multi-Plane Overlay (MPO) Support"
    );

    ImGui::TextWrapped    (
      u8"��ƽ�渲��(mpos)�Ƕ����ר��Ӳ��ɨ��ƽ��s"
      u8" ʹGPU�ܹ����ֵش�DWM�ӹܹ�ͼ"
      u8" ʹ����Ϸ�����ڸ��ֻ�ϳ����򴰿�ģʽ���ƹ�DWM,"
      u8" �Ӷ������˳����ӳ�."
    );

    SKIF_ImGui_Spacing ();
    
    SKIF_ImGui_Columns (2, nullptr, true);

    ImGui::BeginGroup  ();

    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                        u8"������ʾ��֮���֧��:"
    );

    if (SKIF_Util_IsWindows10OrGreater ( ))
    {
      ImGui::Text        (u8"��ʾ");
      ImGui::SameLine    ( );
      ImGui::ItemSize    (ImVec2 (150.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::SameLine    ( );
      ImGui::Text        (u8"Planes");
      ImGui::SameLine    ( );
      ImGui::ItemSize    (ImVec2 (235.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::SameLine    ( );
      ImGui::Text        (u8"����");
      ImGui::SameLine    ( );
      ImGui::ItemSize    (ImVec2 (360.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
      ImGui::SameLine    ( );
      ImGui::Text        (u8"Capabilities");

      for (auto& monitor : Monitors)
      {
        std::string stretchFormat = (monitor.MaxStretchFactor < 10.0f) ? "  %.1fx - %.1fx" // two added spaces for sub-10.0x to align them vertically with other displays
                                                                       :   "%.1fx - %.1fx";
        ImVec4 colName            = (monitor.MaxPlanes > 1) ? ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success)
                                                            : ImVec4 (ImColor::HSV (0.11F, 1.F, 1.F));

        ImGui::BeginGroup    ( );
        //ImGui::Text        (u8"%u", monitor.Index);
        //ImGui::SameLine    ( );
        ImGui::TextColored   (colName, monitor.Name.c_str());
        SKIF_ImGui_SetHoverTip (monitor.DeviceNameGdi.c_str());
        ImGui::SameLine      ( );
        ImGui::ItemSize      (ImVec2 (170.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
        ImGui::SameLine      ( );
        ImGui::Text          ("%u", monitor.MaxPlanes);
        ImGui::SameLine      ( );
        ImGui::ItemSize      (ImVec2 (235.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
        ImGui::SameLine      ( );
        if (monitor.MaxStretchFactor != monitor.MaxShrinkFactor)
          ImGui::Text        (stretchFormat.c_str(), monitor.MaxStretchFactor, monitor.MaxShrinkFactor);
        else
          ImGui::Text        (u8"��֧��");
        ImGui::SameLine      ( );
        if (monitor.MaxPlanes > 1)
        {
          ImGui::ItemSize    (ImVec2 (390.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
          ImGui::SameLine    ( );
          ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_EXCLAMATION_CIRCLE);
          SKIF_ImGui_SetHoverTip (monitor.OverlayCapsAsString.c_str());
        }
        else {
          ImGui::ItemSize    (ImVec2 (360.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
          ImGui::SameLine    ( );
          ImGui::Text        (u8"��֧��");
        }
        ImGui::EndGroup      ( );
      }
    }
    else {
      ImGui::Text      (u8"����MPO������ҪWindows 10����°汾.");
    }

    ImGui::EndGroup    ();

    ImGui::NextColumn  ();

    ImGui::TreePush    ();

    ImGui::PushStyleColor (ImGuiCol_Text,
      ImGui::GetStyleColorVec4 (ImGuiCol_TextDisabled));

    ImGui::TextColored (
      ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption),
                        u8"���Ҫ��:"
    );

    ImGui::BeginGroup  ();
    ImGui::Spacing     ();
    ImGui::SameLine    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
    ImGui::SameLine    ();
    ImGui::Text        (u8"AMD: Radeon RX Vega + Adrenalin Edition 22.5.2 drivers");
    ImGui::EndGroup    ();
    // Exact hardware models are unknown, but a bunch of dxdiag.txt files dropped online suggests Radeon RX Vega and newer had MPO support.
    // ID3D13Sylveon on the DirectX Discord mentioned that driver support was added in 22.20, so AMD Software: Adrenalin Edition 22.5.2.

    ImGui::BeginGroup  ();
    ImGui::Spacing     ();
    ImGui::SameLine    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
    ImGui::SameLine    ();
    ImGui::Text        (u8"Intel: HD Graphics 510-515 (Core 6th gen)");
    ImGui::EndGroup    ();
    // From https://www.intel.com/content/www/us/en/developer/articles/training/tutorial-migrating-your-apps-to-directx-12-part-4.html

    ImGui::BeginGroup  ();
    ImGui::Spacing     ();
    ImGui::SameLine    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
    ImGui::SameLine    ();
    ImGui::Text        (u8"Nvidia: GTX 16/RTX 20 series (Turing) + R460 drivers");
    ImGui::EndGroup    ();
    // Official Nvidia requirement from their driver release notes is Volta and later GPUs and the Release 460 driver and later
    // As Volta only had the Titan V and Quadro GV100 models we can just say GTX 16/RTX 20 series
    
    ImGui::Spacing     ();

    ImGui::TextWrapped (u8"֧��ȡ����GPU����ʾ����."
      u8" ʹ�ò�Ѱ������ʾ����"
      u8" ����SDRģʽ�µ�10bpc"
      u8" ���ܻ���ֹMPO����������ʾ.");

    ImGui::PopStyleColor ();

    ImGui::TreePop     ();

    ImGui::Columns     (1);

    ImGui::Spacing     ();
    ImGui::Spacing     ();

    ImGui::EndGroup    ();

    ImGui::PopStyleColor ();
  }
#pragma endregion

}
