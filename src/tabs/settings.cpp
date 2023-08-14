
#include <utility/skif_imgui.h>
#include <fonts/fa_621.h>
#include <fonts/fa_621b.h>
#include <utility/sk_utility.h>
#include <utility/utility.h>
#include <filesystem>

#include <dxgi.h>
#include <d3d11.h>
#include <d3dkmthk.h>

#include <utility/fsutil.h>
#include <utility/registry.h>
#include <utility/injection.h>
#include <utility/updater.h>

extern bool allowShortcutCtrlA;

struct Monitor_MPO_Support
{
  std::string                    Name;  // EDID names are limited to 13 characters, which is perfect for us
  UINT                           Index; // cloneGroupId -- doesn't really correspond to anything important for us...
  std::string                    DeviceNameGdi;
  std::string                    DevicePath;
  UINT                           MaxPlanes;
  UINT                           MaxRGBPlanes;
  UINT                           MaxYUVPlanes;
  float                          MaxStretchFactor;
  float                          MaxShrinkFactor;
  D3DKMT_MULTIPLANE_OVERLAY_CAPS OverlayCaps;
  std::string                    OverlayCapsAsString;
  bool                           Supported = false; // Pure assumption by us based on various discoveries/experiences
};

enum DrvInstallState {
  NotInstalled,
  Installed,
  OtherDriverInstalled,
  ObsoleteInstalled
};

bool                              MPORegistryDisabled = false;
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
        PLOG_INFO << "+------------------+-------------------------------------+";
        PLOG_INFO << "| Monitor Name     | " << monitorName;
        PLOG_INFO << "| Adapter Path     | " << adapterName.adapterDevicePath;
        PLOG_INFO << "| Source Name      | " <<  sourceName.viewGdiDeviceName;
        PLOG_INFO << "+------------------+-------------------------------------+";
        PLOG_INFO << "| MPO MaxPlanes    | " << caps.MaxPlanes;
        PLOG_INFO << "| MPO MaxRGBPlanes | " << caps.MaxRGBPlanes; // MaxRGBPlanes seems to be the number that best corresponds to dxdiag's reporting? Or is it?
        PLOG_INFO << "| MPO MaxYUVPlanes:| " << caps.MaxYUVPlanes;
        PLOG_INFO << "| MPO Stretch:     | " << caps.MaxStretchFactor << "x - " << caps.MaxShrinkFactor << "x";
        PLOG_INFO << "+------------------+-------------------------------------+";
          
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

        // This is pure assumption from us based on discoveries/experiences and this line in the MSFT docs:
        // "At least one plane must support shrinking and stretching, independent from other planes that might be enabled."
        if (monitor.MaxStretchFactor  !=  monitor.MaxShrinkFactor  &&
            monitor.MaxPlanes  > 1    && (monitor.MaxRGBPlanes > 1 || monitor.MaxYUVPlanes > 1))
          monitor.Supported = true;

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
        //if (monitor.OverlayCaps.Version3DDISupport)
        //  monitor.OverlayCapsAsString += "Driver supports the WDDM 2.2 MPO (multi-plane overlay) DDIs.";

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
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );

  std::wstring       binaryPath = L"";
  SC_HANDLE        schSCManager = NULL,
                    svcWinRing0 = NULL;
  LPQUERY_SERVICE_CONFIG   lpsc = {  };
  DWORD                    dwBytesNeeded,
                            cbBufSize {};

  // Reset the current status to not installed.
  ptrStatus = NotInstalled;

  // Retrieve the install folder.
  static std::wstring dirNameInstall  = std::filesystem::path (_path_cache.specialk_install ).filename();
  static std::wstring dirNameUserdata = std::filesystem::path (_path_cache.specialk_userdata).filename();

  // Need a try-catch block as this can apparently crash due to some currently unknown reason
  try {
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
  }

  catch (const std::exception&)
  {
    PLOG_ERROR << "Unexpected exception was thrown!";
  }

  return binaryPath;
};

void
SKIF_UI_Tab_DrawSettings (void)
{
  static SKIF_CommonPathsCache& _path_cache = SKIF_CommonPathsCache::GetInstance ( );
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );
  static SKIF_InjectionContext& _inject     = SKIF_InjectionContext::GetInstance ( );

  static std::wstring
            driverBinaryPath    = L"",
            SKIFdrvFolder = SK_FormatStringW (LR"(%ws\Drivers\WinRing0\)", _path_cache.specialk_install),
            SKIFdrv       = SKIFdrvFolder + L"SKIFdrv.exe", // TODO: Should be reworked to support a separate install location as well
            SYSdrv        = SKIFdrvFolder + L"WinRing0x64.sys"; // TODO: Should be reworked to support a separate install location as well
  
  static SKIF_DirectoryWatch SKIF_DriverWatch;
  static bool HDRSupported = false;

  static DWORD dwTriggerNewRefresh = 0; // Set when to trigger a new refresh (0 = DISABLED)

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
  if (SKIF_Tab_Selected != UITab_Settings               ||
      RefreshSettingsTab                                ||
      SKIF_DriverWatch.isSignaled (SKIFdrvFolder, true) ||
      dwTriggerNewRefresh != 0 && dwTriggerNewRefresh < SKIF_Util_timeGetTime ( )    )
  {
    GetMPOSupport         (    );
    SKIF_Util_IsMPOsDisabledInRegistry (true);
    SKIF_Util_IsHDRActive (true);
    driverBinaryPath    = GetDrvInstallState (driverStatus);
    driverStatusPending =                     driverStatus;
    RefreshSettingsTab  = false;
    dwTriggerNewRefresh = 0;
  }

  SKIF_Tab_Selected = UITab_Settings;
  if (SKIF_Tab_ChangeTo == UITab_Settings)
      SKIF_Tab_ChangeTo  = UITab_None;

#pragma region Section: Top / General
  // SKIF Options
  //if (ImGui::CollapsingHeader ("Frontend v " SKIF_VERSION_STR_A " (" __DATE__ ")###SKIF_SettingsHeader-0", ImGuiTreeNodeFlags_DefaultOpen))
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
    _registry.regKVLowBandwidthMode.putData (                            _registry.bLowBandwidthMode );
          
  ImGui::SameLine        ( );
  ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
  SKIF_ImGui_SetHoverTip (
    u8"��������Ϸ/���棬�ͷֱ���ͼ��ȸ߷ֱ���ͼ����ܻ�ӭ.\n"
    u8"��ֻ��Ӱ�쵽�����صķ��档������Ӱ���Ѿ����صķ���.\n"
    u8"��Ҳ�������¸��µ��Զ����� Special K."
  );
            
  if ( ImGui::Checkbox ( "Minimize when launching a game",            &_registry.bMinimizeOnGameLaunch ) )
    _registry.regKVMinimizeOnGameLaunch.putData (                      _registry.bMinimizeOnGameLaunch );
            
  if ( ImGui::Checkbox ( "Restore after closing a game",              &_registry.bRestoreOnGameExit ) )
    _registry.regKVRestoreOnGameExit.putData    (                      _registry.bRestoreOnGameExit );

  ImGui::SameLine        ( );
  ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
  SKIF_ImGui_SetHoverTip (
    "Requires Special K injected to work as intended."
  );
  
  if (_registry.bAllowMultipleInstances)
  {
    ImGui::BeginGroup   ( );
    SKIF_ImGui_PushDisableState ( );
  }

  if ( ImGui::Checkbox (u8"����֪ͨ����", &_registry.bCloseToTray ) )
    _registry.regKVCloseToTray.putData (                    _registry.bCloseToTray );

  if (_registry.bAllowMultipleInstances)
  {
    SKIF_ImGui_PopDisableState  ( );
    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    ImGui::EndGroup        ( );
    SKIF_ImGui_SetHoverTip ("Requires 'Allow multiple instances of this app' to be disabled.");
  }

  _inject._StartAtLogonCtrl ( );

  ImGui::NextColumn    ( );

  ImGui::TreePush      ( );

  // New column
          
  ImGui::BeginGroup    ( );
            
  ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
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
    _registry.regKVAutoStopBehavior.putData (      _registry.iAutoStopBehavior);

  SKIF_ImGui_SetHoverTip (u8"�� Special K�ɹ�ע����Ϸʱ������ֹͣ.");

  ImGui::SameLine        ( );
  if (ImGui::RadioButton (u8"��Ϸ�˳�ʱֹͣ",      &_registry.iAutoStopBehavior, 2))
    _registry.regKVAutoStopBehavior.putData (        _registry.iAutoStopBehavior);

  SKIF_ImGui_SetHoverTip (u8"��Special K��⵽��Ϸ���ر�ʱ�����񽫱�ֹͣ.");

  ImGui::TreePop         ( );

  ImGui::Spacing         ( );

  ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
  SKIF_ImGui_SetHoverTip (u8"���õʹ���ģʽ�󣬴�������Ч.");
  ImGui::SameLine        ( );
  ImGui::TextColored (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
    u8"��� Special K�ĸ���:"
  );

  if (_registry.bLowBandwidthMode)
    SKIF_ImGui_PushDisableState ( );

  ImGui::BeginGroup    ( );

  ImGui::TreePush        (u8"_registry.iCheckForUpdates");
  if (ImGui::RadioButton (u8"�Ӳ�",                 &_registry.iCheckForUpdates, 0))
    _registry.regKVCheckForUpdates.putData (         _registry.iCheckForUpdates);
  ImGui::SameLine        ( );
  if (ImGui::RadioButton (u8"ÿ��",                &_registry.iCheckForUpdates, 1))
    _registry.regKVCheckForUpdates.putData (        _registry.iCheckForUpdates);
  ImGui::SameLine        ( );
  if (ImGui::RadioButton (u8"ÿ������ʱ",        &_registry.iCheckForUpdates, 2))
    _registry.regKVCheckForUpdates.putData (        _registry.iCheckForUpdates);

  ImGui::TreePop         ( );

  ImGui::EndGroup      ( );

  static SKIF_Updater& _updater = SKIF_Updater::GetInstance ( );

  bool channelsDisabled = false;
  if (_updater.IsRunning ( ) || _updater.GetChannels ( )->empty( ))
    channelsDisabled = true;

  ImGui::TreePush        ("Push_UpdateChannel");

  ImGui::BeginGroup    ( );

  if (channelsDisabled)
    SKIF_ImGui_PushDisableState ( );

  if (ImGui::BeginCombo ("##SKIF_wzUpdateChannel", _updater.GetChannel( )->second.c_str()))
  {
    for (auto& updateChannel : *_updater.GetChannels ( ))
    {
      bool is_selected = (_updater.GetChannel()->first == updateChannel.first);

      if (ImGui::Selectable (updateChannel.second.c_str(), is_selected) && updateChannel.first != _updater.GetChannel( )->first)
      {
        _updater.SetChannel (&updateChannel); // Update selection
        _updater.SetIgnoredUpdate (L"");      // Clear any ignored updates
        _updater.CheckForUpdates  ( );        // Trigger a new check for updates
      }

      if (is_selected)
        ImGui::SetItemDefaultFocus ( );
    }

    ImGui::EndCombo  ( );
  }

  ImGui::SameLine        ( );

  if (ImGui::Button      (ICON_FA_ROTATE))
  {
    _updater.SetIgnoredUpdate (L""); // Clear any ignored updates
    _updater.CheckForUpdates (true); // Trigger a forced check for updates/redownloads of repository.json and patrons.txt
  }

  SKIF_ImGui_SetHoverTip ("Check for updates");

  if (((_updater.GetState() & UpdateFlags_Rollback) == UpdateFlags_Rollback) || _updater.IsRollbackAvailable ( ))
  {
    ImGui::SameLine        ( );

    if (ImGui::Button      (ICON_FA_ROTATE_LEFT))
    {
      extern PopupState UpdatePromptPopup;

      _updater.SetIgnoredUpdate (SK_UTF8ToWideChar (_updater.GetResults().description_latest)); // Ignore the latest version

      if ((_updater.GetState() & UpdateFlags_Rollback) == UpdateFlags_Rollback)
        UpdatePromptPopup = PopupState_Open;
      else
        _updater.CheckForUpdates (false, true); // Trigger a rollback
    }

    SKIF_ImGui_SetHoverTip ("Roll back to the previous version");
  }

  if (channelsDisabled)
    SKIF_ImGui_PopDisableState  ( );

  ImGui::EndGroup      ( );

  ImGui::TreePop       ( );

  /*
  else if (_registry.iCheckForUpdates > 0) {
    ImGui::TreePush      ("Push_UpdateChannel");
    ImGui::BeginGroup    ( );
    ImGui::TextColored   (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Warning),
      u8"��Ҫ������������������ͨ��.");
    ImGui::EndGroup      ( );
    ImGui::TreePop       ( );
  }
  */

  if (_registry.bLowBandwidthMode)
    SKIF_ImGui_PopDisableState  ( );

  ImGui::Spacing       ( );
            
  ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
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

    SKIF_ImGui_Spacing      ( );

    SKIF_ImGui_Columns      (2, nullptr, true);

    SK_RunOnce(
      ImGui::SetColumnWidth (0, 510.0f * SKIF_ImGui_GlobalDPIScale) //SKIF_vecCurrentMode.x / 2.0f)
    );
  
    if ( ImGui::Checkbox ( "Ignore articles when sorting",                &_registry.bLibraryIgnoreArticles) )
    {
      _registry.regKVLibraryIgnoreArticles.putData (                       _registry.bLibraryIgnoreArticles);
      RepopulateGames = true;
    }

    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip (
      "Ignore articles like 'THE', 'A', and 'AN' when sorting games."
    );

    if ( ImGui::Checkbox ( u8"��ס���ѡ�����Ϸ",           &_registry.bRememberLastSelected ) )
      _registry.regKVRememberLastSelected.putData (                      _registry.bRememberLastSelected );

    if ( ImGui::Checkbox ( "Prefer launching GOG games through Galaxy", &_registry.bPreferGOGGalaxyLaunch) )
      _registry.regKVPreferGOGGalaxyLaunch.putData (_registry.bPreferGOGGalaxyLaunch);

    ImGui::NextColumn       ( );

    ImGui::TreePush         ( );
            
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        u8"����ѡ��ƽ̨�ϵ���Ϸ:"
    );
    ImGui::TreePush      ("");

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

    ImGui::TreePop          ( );

    ImGui::TreePop          ( );

    ImGui::Columns          (1);

    ImGui::PopStyleColor    ( );
  }

  ImGui::Spacing ();
  ImGui::Spacing ();



#pragma endregion


#pragma region Section: Appearances
  if (ImGui::CollapsingHeader ("Appearance###SKIF_SettingsHeader-2"))
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

    const char* StyleItems[] = { "SKIF Dark",
                                 "ImGui Dark",
                                 "SKIF Light",
                                 "ImGui Classic"
    };
    static const char*
      StyleItemsCurrent;
      StyleItemsCurrent = StyleItems[_registry.iStyle]; // Re-apply the value on every frame as it may have changed
          
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Color theme:"
    );
    ImGui::TreePush      ("");

    if (ImGui::BeginCombo ("##_registry.iStyleCombo", StyleItemsCurrent)) // The second parameter is the label previewed before opening the combo.
    {
        for (int n = 0; n < IM_ARRAYSIZE (StyleItems); n++)
        {
            bool is_selected = (StyleItemsCurrent == StyleItems[n]); // You can store your selection however you want, outside or inside your objects
            if (ImGui::Selectable (StyleItems[n], is_selected))
              _registry.iStyleTemp = n; // We apply the new style at the beginning of the next frame to prevent any PushStyleColor/Var from causing issues
            if (is_selected)
              ImGui::SetItemDefaultFocus ( );   // You may set the initial focus when opening the combo (scrolling + for keyboard navigation support)
        }
        ImGui::EndCombo  ( );
    }

    ImGui::TreePop       ( );

    ImGui::Spacing         ( );

    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
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

    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip ("Every time the UI renders a frame, Shelly the Ghost moves a little bit.");
    ImGui::SameLine        ( );
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        "Show Shelly the Ghost:"
    );
    ImGui::TreePush        ("_registry.iGhostVisibility");
    if (ImGui::RadioButton ("Never",                    &_registry.iGhostVisibility, 0))
      _registry.regKVGhostVisibility.putData (                     _registry.iGhostVisibility);
    ImGui::SameLine        ( );
    if (ImGui::RadioButton ("Always",                   &_registry.iGhostVisibility, 1))
      _registry.regKVGhostVisibility.putData (                     _registry.iGhostVisibility);
    ImGui::SameLine        ( );
    if (ImGui::RadioButton ("While service is running", &_registry.iGhostVisibility, 2))
      _registry.regKVGhostVisibility.putData (                     _registry.iGhostVisibility);
    ImGui::TreePop         ( );

    ImGui::Spacing         ( );

    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip (u8"������Ƶ�ÿ��ѡ�����Ի�ȡ������Ϣ.");
    ImGui::SameLine        ( );
    ImGui::TextColored     (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      u8"����UIԪ��:"
    );
    ImGui::TreePush        ("");

    if (ImGui::Checkbox ("Borders", &_registry.bDisableBorders))
    {
      _registry.regKVDisableBorders.putData (  _registry.bDisableBorders);

      ImGuiStyle            newStyle;
      SKIF_ImGui_SetStyle (&newStyle);
    }

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    if (ImGui::Checkbox (u8"������ʾ", &_registry.bDisableTooltips))
    {
      _registry.regKVDisableTooltips.putData (  _registry.bDisableTooltips);

      extern ImVec2 SKIF_vecAppModeDefault;
      extern ImVec2 SKIF_vecAppModeAdjusted;
      extern ImVec2 SKIF_vecAlteredSize;
      extern float  SKIF_fStatusBarHeight;
      extern float  SKIF_fStatusBarDisabled;
      extern float  SKIF_fStatusBarHeightTips;

      // Adjust the large mode size
      SKIF_vecAppModeAdjusted = SKIF_vecAppModeDefault;

      // Add the status bar if it is not disabled
      if ( ! _registry.bDisableStatusBar )
      {
        SKIF_vecAppModeAdjusted.y += SKIF_fStatusBarHeight;

        if (_registry.bDisableTooltips)
          SKIF_vecAppModeAdjusted.y += SKIF_fStatusBarHeightTips;
      }

      else
        SKIF_vecAppModeAdjusted.y += SKIF_fStatusBarDisabled;

      // Take the current display into account
      HMONITOR monitor =
        ::MonitorFromWindow (SKIF_ImGui_hWnd, MONITOR_DEFAULTTONEAREST);

      MONITORINFO
        info        = {                  };
        info.cbSize = sizeof (MONITORINFO);

      if (::GetMonitorInfo (monitor, &info))
      {
        ImVec2 WorkSize =
          ImVec2 ( (float)( info.rcWork.right  - info.rcWork.left ),
                    (float)( info.rcWork.bottom - info.rcWork.top  ) );

        if (SKIF_vecAppModeAdjusted.y * SKIF_ImGui_GlobalDPIScale > (WorkSize.y))
          SKIF_vecAlteredSize.y = (SKIF_vecAppModeAdjusted.y * SKIF_ImGui_GlobalDPIScale - (WorkSize.y));
      }
    }

    if (ImGui::IsItemHovered ())
      SKIF_StatusBarText = "Info: ";

    SKIF_ImGui_SetHoverText (u8"������ʾ��Ϣ�ĵط�.");
    SKIF_ImGui_SetHoverTip  (u8"��Ϣ����ʾ�ڵײ���״̬����."
      u8"\n��ע�⣬��Щ��������޷�Ԥ��.");

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    if (ImGui::Checkbox (u8"״̬��", &_registry.bDisableStatusBar))
    {
      _registry.regKVDisableStatusBar.putData (   _registry.bDisableStatusBar);

      extern ImVec2 SKIF_vecAppModeDefault;
      extern ImVec2 SKIF_vecAppModeAdjusted;
      extern ImVec2 SKIF_vecAlteredSize;
      extern float  SKIF_fStatusBarHeight;
      extern float  SKIF_fStatusBarDisabled;
      extern float  SKIF_fStatusBarHeightTips;

      // Adjust the large mode size
      SKIF_vecAppModeAdjusted = SKIF_vecAppModeDefault;

      // Add the status bar if it is not disabled
      if ( ! _registry.bDisableStatusBar )
      {
        SKIF_vecAppModeAdjusted.y += SKIF_fStatusBarHeight;

        if (_registry.bDisableTooltips)
          SKIF_vecAppModeAdjusted.y += SKIF_fStatusBarHeightTips;
      }

      else
        SKIF_vecAppModeAdjusted.y += SKIF_fStatusBarDisabled;

      // Take the current display into account
      HMONITOR monitor =
        ::MonitorFromWindow (SKIF_ImGui_hWnd, MONITOR_DEFAULTTONEAREST);

      MONITORINFO
        info        = {                  };
        info.cbSize = sizeof (MONITORINFO);

      if (::GetMonitorInfo (monitor, &info))
      {
        ImVec2 WorkSize =
          ImVec2 ( (float)( info.rcWork.right  - info.rcWork.left ),
                    (float)( info.rcWork.bottom - info.rcWork.top  ) );

        if (SKIF_vecAppModeAdjusted.y * SKIF_ImGui_GlobalDPIScale > (WorkSize.y))
          SKIF_vecAlteredSize.y = (SKIF_vecAppModeAdjusted.y * SKIF_ImGui_GlobalDPIScale - (WorkSize.y));
      }
    }

    SKIF_ImGui_SetHoverTip (
      u8"���˹�������õ�UI������ʾ���ϣ����������л��������ĵ���Ϣ����ʾ."
    );

    ImGui::SameLine ( );
    ImGui::Spacing  ( );
    ImGui::SameLine ( );

    if (ImGui::Checkbox ("HiDPI scaling", &_registry.bDisableDPIScaling))
    {
      extern bool
        changedHiDPIScaling;
        changedHiDPIScaling = true;
    }

    SKIF_ImGui_SetHoverTip (
      "This application will appear smaller on HiDPI monitors."
    );

    if (_registry.bDisableTooltips &&
        _registry.bDisableStatusBar)
    {
      ImGui::BeginGroup     ( );
      ImGui::TextColored    (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
      ImGui::SameLine       ( );
      ImGui::TextColored    (ImColor(0.68F, 0.68F, 0.68F, 1.0f), u8"���������ĵ���Ϣ����ʾ���������!");
      ImGui::EndGroup       ( );
    }

    ImGui::TreePop       ( );

    ImGui::NextColumn    ( );

    ImGui::TreePush      ( );

    extern bool RecreateSwapChains;

    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip ("Move the mouse over each option to get more information");
    ImGui::SameLine        ( );
    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                            "UI mode:"
    );

    extern bool SKIF_bCanFlip;
    ImGui::TreePush        ("SKIF_iSyncMode");
    // Flip VRR Compatibility Mode (only relevant on Windows 10+)
    if (SKIF_bCanFlip && SKIF_Util_IsWindows10OrGreater ( ))
    {
      if (ImGui::RadioButton ("VRR Compatibility", &_registry.iUIMode, 2))
      {
        _registry.regKVUIMode.putData (             _registry.iUIMode);
        RecreateSwapChains = true;
      }
      SKIF_ImGui_SetHoverTip (
        u8"����VRR��ʾ���ϵ��źŶ�ʧ����˸"
      );
      ImGui::SameLine        ( );
    }
    if (ImGui::RadioButton ("Normal",              &_registry.iUIMode, 1))
    {
      _registry.regKVUIMode.putData (               _registry.iUIMode);
      RecreateSwapChains = true;
    }
    SKIF_ImGui_SetHoverTip (
      "Improves UI response on low fixed-refresh rate displays"
    );
    ImGui::SameLine        ( );
    if (ImGui::RadioButton ("Safe Mode",           &_registry.iUIMode, 0))
    {
      _registry.regKVUIMode.putData (               _registry.iUIMode);
      RecreateSwapChains = true;
    }
    SKIF_ImGui_SetHoverTip (
      "Compatibility mode for users experiencing issues with the other two modes"
    );
    ImGui::TreePop         ( );

    ImGui::Spacing         ( );

    ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
    SKIF_ImGui_SetHoverTip ("Increases the color depth of the app.");
    ImGui::SameLine        ( );
    ImGui::TextColored (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
        u8"��ɫ����: (��Ҫ��������)"
    );
    
    static int placeholder = 0;
    static int* ptrSDR = nullptr;

    if ((_registry.iHDRMode > 0 && SKIF_Util_IsHDRActive ( )))
    {
      SKIF_ImGui_PushDisableState ( );

      ptrSDR = &_registry.iHDRMode;
    }
    else
      ptrSDR = &_registry.iSDRMode;
    
    ImGui::TreePush        ("iSDRMode");
    if (ImGui::RadioButton   ("8 bpc",        ptrSDR, 0))
    {
      _registry.regKVSDRMode.putData (_registry.iSDRMode);
      RecreateSwapChains = true;
    }
    // It seems that Windows 10 1709+ (Build 16299) is required to
    // support 10 bpc (DXGI_FORMAT_R10G10B10A2_UNORM) for flip model
    if (SKIF_Util_IsWindows10v1709OrGreater ( ))
    {
      ImGui::SameLine        ( );
      if (ImGui::RadioButton ("10 bpc",       ptrSDR, 1))
      {
        _registry.regKVSDRMode.putData (_registry.iSDRMode);
        RecreateSwapChains = true;
      }
    }
    ImGui::SameLine        ( );
    if (ImGui::RadioButton   ("16 bpc",       ptrSDR, 2))
    {
      _registry.regKVSDRMode.putData (_registry.iSDRMode);
      RecreateSwapChains = true;
    }
    ImGui::TreePop         ( );
    
    if ((_registry.iHDRMode > 0 && SKIF_Util_IsHDRActive ( )))
    {
      SKIF_ImGui_PopDisableState  ( );
    }

    ImGui::Spacing         ( );
    
    if (SKIF_Util_IsHDRSupported ( )  )
    {
      ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
      SKIF_ImGui_SetHoverTip ("Makes the app pop more on HDR displays.");
      ImGui::SameLine        ( );
      ImGui::TextColored (
        ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
          "High dynamic range (HDR):"
      );

      ImGui::TreePush        ("iHDRMode");

      if (_registry.iUIMode == 0)
      {
        ImGui::TextDisabled   ("HDR support is disabled while the UI is in Safe Mode.");
      }

      else if (SKIF_Util_IsHDRActive ( ))
      {
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

        // HDR Brightness

        if (_registry.iHDRMode == 0)
          SKIF_ImGui_PushDisableState ( );

        if (ImGui::SliderInt("HDR brightness", &_registry.iHDRBrightness, 80, 400, "%d nits"))
        {
          // Reset to 203 nits (default; HDR reference white for BT.2408) if negative or zero
          if (_registry.iHDRBrightness <= 0)
              _registry.iHDRBrightness  = 203;

          // Keep the nits value between 80 and 400
          _registry.iHDRBrightness = std::min (std::max (80, _registry.iHDRBrightness), 400);
          _registry.regKVHDRBrightness.putData (_registry.iHDRBrightness);
        }
    
        if (ImGui::IsItemActive    ( ))
          allowShortcutCtrlA = false;

        if (_registry.iHDRMode == 0)
          SKIF_ImGui_PopDisableState  ( );
      }

      else {
        ImGui::TextDisabled   ("Your display(s) supports HDR, but does not use it.");
      }

      if (SKIF_Util_GetHotKeyStateHDRToggle ( ) && _registry.iUIMode != 0)
      {
        ImGui::Spacing         ( );
        /*
        ImGui::PushStyleColor (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped    ("FYI: Use " ICON_FA_WINDOWS " + Ctrl + Shift + H while this app is running to toggle "
                               "HDR for the display the mouse cursor is currently located on.");
        ImGui::PopStyleColor  ( );
        */
        
        ImGui::BeginGroup       ( );
        ImGui::TextDisabled     ("Use");
        ImGui::SameLine         ( );
        ImGui::TextColored      (
          ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),
          ICON_FA_WINDOWS " + Ctrl + Shift + H");
        ImGui::SameLine         ( );
        ImGui::TextDisabled     ("to toggle HDR where the");
        ImGui::SameLine         ( );
        ImGui::TextColored      (
          ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase),
            ICON_FA_ARROW_POINTER "");
        ImGui::SameLine         ( );
        ImGui::TextDisabled     ("is.");
        ImGui::EndGroup         ( );
      }

      ImGui::TreePop         ( );
    }

    ImGui::TreePop       ( );

    ImGui::Columns       (1);

    ImGui::PopStyleColor ( );
  }

  ImGui::Spacing ();
  ImGui::Spacing ();
#pragma endregion

#pragma region Section: Advanced
  if (ImGui::CollapsingHeader (u8"�߼�###SKIF_SettingsHeader-3"))
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
      _registry.regKVOpenAtCursorPosition.putData (                                  _registry.bOpenAtCursorPosition );

    if ( ImGui::Checkbox ( "Always open this app in the smaller service mode view", &_registry.bOpenInServiceMode) )
      _registry.regKVOpenInServiceMode.putData (                                     _registry.bOpenInServiceMode);

    if (_registry.bCloseToTray)
    {
      ImGui::BeginGroup   ( );
      SKIF_ImGui_PushDisableState ( );
    }

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
              if (SKIF_Notify_hWnd != hWnd) // Don't send WM_QUIT to ourselves
                PostMessage (  hWnd, WM_QUIT,
                                0x0, 0x0  );
            }
          }
          return TRUE;
        }, (LPARAM)SKIF_NotifyIcoClass);
      }

      _registry.regKVAllowMultipleInstances.putData (
        _registry.bAllowMultipleInstances
        );
    }

    if (_registry.bCloseToTray)
    {
      SKIF_ImGui_PopDisableState  ( );
      ImGui::SameLine        ( );
      ImGui::TextColored     (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
      ImGui::EndGroup        ( );
      SKIF_ImGui_SetHoverTip ("Requires 'Close to the notification area' to be disabled.");
    }

    ImGui::NextColumn       ( );

    ImGui::TreePush         ( );

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

    if (ImGui::Checkbox  ("Do not stop the injection service when this app closes",
                                                      &_registry.bAllowBackgroundService))
      _registry.regKVAllowBackgroundService.putData (  _registry.bAllowBackgroundService);

    if (SKIF_Util_IsWindows11orGreater ( ))
    {
      if ( ImGui::Checkbox ( "Enable Win11 rounded corners (may be buggy; restart required)", &_registry.bWin11Corners) )
        _registry.regKVWin11Corners.putData (                                                  _registry.bWin11Corners);
    }

    ImGui::TreePop          ( );

    ImGui::Columns          (1);

    ImGui::PopStyleColor    ( );
  }

  ImGui::Spacing ();
  ImGui::Spacing ();
#pragma endregion

#pragma region Section: Whitelist / Blacklist
  if (ImGui::CollapsingHeader (u8"������ / ������###SKIF_SettingsHeader-4")) //, ImGuiTreeNodeFlags_DefaultOpen)) // Disabled auto-open for this section
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
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow),     ICON_FA_TRIANGLE_EXCLAMATION);
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
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),      ICON_FA_LIGHTBULB);
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
        ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),      ICON_FA_LIGHTBULB);
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
    ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),   ICON_FA_LIGHTBULB);
    ImGui::SameLine   (); ImGui::Text        (u8"��򵥵ķ�����ʹ����Ϸ�Ŀ�ִ���ļ����ļ��е�����.");
    ImGui::EndGroup   ();

    SKIF_ImGui_SetHoverTip (
      "e.g. a pattern like \"Assassin's Creed Valhalla\" will match an application at"
        "\nC:\\Games\\Uplay\\games\\Assassin's Creed Valhalla\\ACValhalla.exe"
    );

    ImGui::BeginGroup ();
    ImGui::Spacing    ();
    ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),   ICON_FA_LIGHTBULB);
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
    ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), ICON_FA_LIGHTBULB);
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
    ImGui::SameLine   (); ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_UP_RIGHT_FROM_SQUARE);
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

    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Success), ICON_FA_SQUARE_PLUS);
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
    
    if (ImGui::IsItemActive    ( ))
      allowShortcutCtrlA = false;

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

      _inject.WhitelistPattern ("Games");
    }

    SKIF_ImGui_SetHoverTip (
      "Whitelists games on most platforms, such as Uplay, as"
      "\nmost of them have 'games' in the full path somewhere."
    );

    /*
    if (ImGui::Selectable (u8"WindowsApps"))
    {
      white_edited = true;

      _inject.AddUserListPattern("WindowsApps", true);
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

    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Failure), ICON_FA_SQUARE_MINUS);
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

    if (ImGui::IsItemActive    ( ))
      allowShortcutCtrlA = false;

    _CheckWarnings (_inject.blacklist);

    ImGui::Separator ();

    bool bDisabled =
      (white_edited || black_edited) ?
                                false : true;

    if (bDisabled)
      SKIF_ImGui_PushDisableState ( );

    // Hotkey: Ctrl+S
    if (ImGui::Button (ICON_FA_FLOPPY_DISK " Save Changes") || ((! bDisabled) && ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeysDown['S']))
    {
      // Clear the active ID to prevent ImGui from holding outdated copies of the variable
      //   if saving succeeds, to allow SaveUserList to update the variable successfully
      ImGui::ClearActiveID();

      if (white_edited)
      {
        white_stored = _inject.SaveWhitelist ( );

        if (white_stored)
          white_edited = false;
      }

      if (black_edited)
      {
        black_stored = _inject.SaveBlacklist ( );

        if (black_stored)
          black_edited = false;
      }
    }

    ImGui::SameLine ();

    if (ImGui::Button (ICON_FA_ROTATE_LEFT " Reset"))
    {
      if (white_edited)
      {
        _inject.LoadWhitelist ( );

        white_edited = false;
        white_stored = true;
      }

      if (black_edited)
      {
        _inject.LoadBlacklist ( );

        black_edited = false;
        black_stored = true;
      }
    }

    if (bDisabled)
      SKIF_ImGui_PopDisableState  ( );

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
      btnDriverLabel    = ICON_FA_USER_SHIELD " Uninstall Driver";
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
      btnDriverLabel    = ICON_FA_USER_SHIELD " Migrate Driver";
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), "Obsolete driver installed");
      wszDriverTaskCmd = L"Migrate Install";
    }

    // Driver is not installed
    else {
      btnDriverLabel    = ICON_FA_USER_SHIELD " Install Driver";
      ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), "Not Installed");
      wszDriverTaskCmd = L"Install";
    }

    ImGui::EndGroup ();

    ImGui::Spacing  ();
    ImGui::Spacing  ();

    // Disable button if the required files are missing, status is pending, or if another driver is installed
    if (  driverStatusPending  != driverStatus ||
          OtherDriverInstalled == driverStatus )
      SKIF_ImGui_PushDisableState ( );

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
      SKIF_ImGui_PopDisableState  ( );

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
  if (ImGui::CollapsingHeader (u8"SwapChain ��ʾ������###SKIF_SettingsHeader-6", ImGuiTreeNodeFlags_DefaultOpen))
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
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_UP_RIGHT_FROM_SQUARE);
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
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_UP_RIGHT_FROM_SQUARE);
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
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), ICON_FA_LIGHTBULB);
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
      SKIF_ImGui_PushDisableState ( );

    std::string btnPfuLabel = (pfuState == Granted) ?                         ICON_FA_CHECK              " Permissions granted!" // Granted
                                                    : (pfuState == Missing) ? ICON_FA_USER_SHIELD        " Grant permissions"    // Missing
                                                                            : ICON_FA_RIGHT_FROM_BRACKET " Sign out to apply";   // Pending

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

      // For Windows 10+ we rely on modern PowerShell cmdlets as this 
      //   is the easiest way of handling non-English localizations
      if (SKIF_Util_IsWindows10OrGreater ( ))
      {
        // S-1-5-32-559 == Group : Performance Log Users
        // S-1-5-4      == User  : NT AUTHORITY\INTERACTIVE
        if (ShellExecuteW (nullptr, L"runas", L"powershell.exe", LR"(-NoProfile -NonInteractive -WindowStyle Hidden -Command "Add-LocalGroupMember -SID 'S-1-5-32-559' -Member 'S-1-5-4'")", nullptr, SW_SHOW) > (HINSTANCE)32)
          pfuState = Pending;
      }

      // On older versions we need to rely on 'net', but we also need to retrieve the local names first
      //   as non-English languages has localized user and group names...
      else if (LookupAccountSid (NULL, pfuSID, pfuName, &pfuNameLength, pfuDomainName, &pfuDomainNameLength, &pfuSnu) &&
          LookupAccountSid (NULL, intSID, intName, &intNameLength, intDomainName, &intDomainNameLength, &intSnu))
      {
        exeArgs = LR"(localgroup ")" + std::wstring(pfuName) + LR"(" ")" + std::wstring(intName) + LR"(" /add)";

        // Note that this can still fail apparently, as 'net' might be unable to handle some non-Latin characters properly (e.g. Russian localized names)
        if (ShellExecuteW (nullptr, L"runas", L"net", exeArgs.c_str(), nullptr, SW_SHOW) > (HINSTANCE)32)
          pfuState = Pending;
      }
    }

    // Disable button for granted + pending states
    else if (pfuState != Missing)
      SKIF_ImGui_PopDisableState  ( );

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
    ImGui::BeginGroup  ();
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info), (const char *)u8"\u2022 ");
    ImGui::SameLine    ();
    ImGui::TextColored (ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "Hardware: Legacy Copy to front buffer");
    ImGui::EndGroup    ();

    SKIF_ImGui_SetHoverTip ("Quite uncommon to see compared to the other models listed here.");
    
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
      ImGui::BeginGroup  ();

      ImGui::Text        ("Display");
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

      // MPO support is a bloody mystery...
      for (auto& monitor : Monitors)
      {
        std::string stretchFormat = (monitor.MaxStretchFactor < 10.0f) ? "  %.1fx - %.1fx" // two added spaces for sub-10.0x to align them vertically with other displays
                                                                       :   "%.1fx - %.1fx";
        ImVec4 colName            = (monitor.Supported) ? ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Success)
                                                        : ImVec4 (ImColor::HSV (0.11F, 1.F, 1.F));
        ImVec4 colCaps            = (monitor.Supported) ? ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_Info)
                                                        : ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextBase);

        ImGui::BeginGroup    ( );
        //ImGui::Text        (u8"%u", monitor.Index);
        //ImGui::SameLine    ( );
        ImGui::TextColored   (colName, monitor.Name.c_str());
        SKIF_ImGui_SetHoverTip (monitor.DeviceNameGdi.c_str());
        ImGui::SameLine      ( );
        ImGui::ItemSize      (ImVec2 (170.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
        ImGui::SameLine      ( );
        ImGui::Text          ("%u", (monitor.Supported) ? monitor.MaxPlanes : 1);
        if (! monitor.Supported && monitor.MaxPlanes > 1)
          SKIF_ImGui_SetHoverTip (std::to_string(monitor.MaxPlanes));
        ImGui::SameLine      ( );
        ImGui::ItemSize      (ImVec2 (235.0f * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
        ImGui::SameLine      ( );
        ImGui::Text          ((monitor.Supported) ? stretchFormat.c_str() : "Not Supported",
                               monitor.MaxStretchFactor, monitor.MaxShrinkFactor);
        if (! monitor.Supported && monitor.MaxStretchFactor != monitor.MaxShrinkFactor)
          SKIF_ImGui_SetHoverTip (SK_FormatString (stretchFormat.c_str(), monitor.MaxStretchFactor, monitor.MaxShrinkFactor));
        ImGui::SameLine      ( );
        ImGui::ItemSize      (ImVec2 (((monitor.Supported) ? 390.0f : 360.0f) * SKIF_ImGui_GlobalDPIScale - ImGui::GetCursorPos().x, ImGui::GetTextLineHeight()));
        ImGui::SameLine      ( );
        ImGui::TextColored   (colCaps, (monitor.Supported) ? ICON_FA_LIGHTBULB : "Not Supported");
        if (monitor.Supported)
          SKIF_ImGui_SetHoverTip (monitor.OverlayCapsAsString.c_str(), true);
        ImGui::EndGroup      ( );
      }

      // This adds a couple of empty lines to expand the height of the clickable area
      // where the DisplayDriverMenu is accessible
      static const int maxMon = 4;
      if (Monitors.size() < maxMon)
      {
        size_t rem = maxMon - Monitors.size();

        for (size_t i = 0; i < rem; i++)
          ImGui::NewLine ( );
      }

      ImGui::EndGroup  ();

      if (ImGui::IsItemClicked (ImGuiMouseButton_Right))
        ImGui::OpenPopup ("DisplayDriverMenu");

      if (ImGui::BeginPopup ("DisplayDriverMenu", ImGuiWindowFlags_NoMove))
      {
        if (ImGui::Selectable (ICON_FA_ROTATE " Refresh"))
          RefreshSettingsTab = true;

        ImGui::Separator ( );

        if (ImGui::Selectable (ICON_FA_ROTATE_RIGHT " Restart display driver"))
          ShellExecuteW (nullptr, L"runas", _path_cache.skif_executable, L"RestartDisplDrv", nullptr, SW_SHOW);

        ImGui::EndPopup ( );
      }

      if (SKIF_Util_IsMPOsDisabledInRegistry ( ))
      {
        // Move up the line 2 pixels as otherwise a scroll would appear...
        //ImGui::SetCursorPosY    (ImGui::GetCursorPosY ( ) - (2.0f * SKIF_ImGui_GlobalDPIScale));
        ImGui::PushStyleColor     (ImGuiCol_Text, ImColor::HSV (0.11F,   1.F, 1.F).Value);
        if (ImGui::Selectable (ICON_FA_TRIANGLE_EXCLAMATION "  Disabled through the registry! Click to reset (restart required)"))
        {
          if (ShellExecuteW (nullptr, L"runas", L"REG", LR"(DELETE HKLM\SOFTWARE\Microsoft\Windows\Dwm /v OverlayTestMode /f)", nullptr, SW_SHOW) > (HINSTANCE)32)
          {
            dwTriggerNewRefresh = SKIF_Util_timeGetTime ( ) + 500; // Trigger a refresh in 500ms
          }
        }
        ImGui::PopStyleColor      ( );
        SKIF_ImGui_SetHoverTip        (R"(A restart of the computer is required for the changes to be applied.)");
        SKIF_ImGui_SetMouseCursorHand ( );
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

    ImGui::TextWrapped ("Support depends on the GPU and display configuration. Unusual display "
                        "configurations, such as 10 bpc in SDR mode, might lack MPO support.");

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
