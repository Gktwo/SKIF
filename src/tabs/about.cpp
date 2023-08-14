#include <SKIF.h>
#include <fonts/fa_621.h>
#include <fonts/fa_621b.h>
#include <utility/sk_utility.h>
#include <utility/utility.h>
#include <utility/skif_imgui.h>
#include <utility/fsutil.h>

// Registry Settings
#include <utility/registry.h>
#include <utility/updater.h>
#include <tabs/common_ui.h>

void
SKIF_UI_Tab_DrawAbout (void)
{
  static SKIF_RegistrySettings& _registry   = SKIF_RegistrySettings::GetInstance ( );

  SKIF_ImGui_Spacing      ( );

  SKIF_ImGui_Columns      (2, nullptr, true);

  SK_RunOnce (
    ImGui::SetColumnWidth (0, 600.0f * SKIF_ImGui_GlobalDPIScale)
  );

  ImGui::PushStyleColor   (
    ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextBase)
                            );

  // ImColor::HSV (0.11F, 1.F, 1.F)   // Orange
  // ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info) // Blue Bullets
  // ImColor(100, 255, 218); // Teal
  // ImGui::GetStyleColorVec4(ImGuiCol_TabHovered);

  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                            u8"Special K (SK)入门指南:"
                            );

  SKIF_ImGui_Spacing      ( );

  ImGui::TextWrapped      ( u8"被人们亲切地称为PC游戏中的瑞士军刀,Special K什么都会做."
    "它以修复和增强图形、许多详细的性能分析和校正模块而闻名,"
    "以及不断增长的工具选项板,可解决影响PC游戏的各种问题.");
  
  SKIF_ImGui_Spacing      ( );

  ImGui::TextWrapped      (u8"其主要功能包括无延迟无边界窗口模式、 "
    u8"SDR游戏，在不受支持的游戏中添加Nvidia Reflex，以及纹理修改 "
    u8"无论是玩家还是改装者。虽然并非所有游戏都支持所有功能，但大多数 "
    u8"DirectX 11和12游戏可以使用其中的一个（如果不是更多的话）功能."
  );
  ImGui::NewLine          ( );
  ImGui::Text             (u8"要开始，只需跳到");
  ImGui::SameLine         ( );
  //ImGui::TextColored    (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption), ICON_FA_GAMEPAD " Library");
  ImGui::PushStyleColor   (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption));
  SKIF_ImGui_Selectable   (ICON_FA_GAMEPAD " Library###About-Lib1");
  ImGui::PopStyleColor    ( );
  SKIF_ImGui_SetMouseCursorHand ( );
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) SKIF_Tab_ChangeTo = UITab_Library;
  ImGui::SameLine         ( );
  ImGui::Text             (u8"并启动游戏!");
  ImGui::SameLine         ( );
  ImGui::TextColored      (ImColor::HSV (0.11F, 1.F, 1.F), ICON_FA_FACE_GRIN_BEAM);

  ImGui::NewLine          ( );
  ImGui::NewLine          ( );

  float fY1 = ImGui::GetCursorPosY();

  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
    u8"开始使用Epic、GOG、Steam或Xbox游戏:");

  SKIF_ImGui_Spacing      ( );

  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                        "1 ");
  ImGui::SameLine         ( );
  ImGui::Text             (u8"转到 ");
  ImGui::SameLine         ( );
  ImGui::PushStyleColor   (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption));
  SKIF_ImGui_Selectable   (ICON_FA_GAMEPAD " Library###About-Lib2");
  ImGui::PopStyleColor    ( );
  SKIF_ImGui_SetMouseCursorHand ( );
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) SKIF_Tab_ChangeTo = UITab_Library;
  ImGui::SameLine         ( );
  ImGui::Text             (u8"tab.");

  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                        "2 ");
  ImGui::SameLine         ( );
  ImGui::TextWrapped      (u8"选择并启动游戏.");

  ImGui::NewLine          ( );
  ImGui::NewLine          ( );

  float fY2 = ImGui::GetCursorPosY();

  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
    u8"开始玩其他游戏:"
  );

  SKIF_ImGui_Spacing      ( );

  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                        "1 ");
  ImGui::SameLine         ( );
  ImGui::Text             (u8"转到 ");
  ImGui::SameLine         ( );
  ImGui::PushStyleColor   (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption));
  SKIF_ImGui_Selectable   (ICON_FA_GAMEPAD " Library###About-Lib3");
  ImGui::PopStyleColor    ( );
  SKIF_ImGui_SetMouseCursorHand ( );
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) SKIF_Tab_ChangeTo = UITab_Library;
  ImGui::SameLine         ( );
  ImGui::Text             (u8"tab.");

  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                        "2 ");
  ImGui::SameLine         ( );
  ImGui::Text             (u8"点击 ");
  ImGui::SameLine         ( );
  ImGui::PushStyleColor   (ImGuiCol_Text, ImGui::GetStyleColorVec4 (ImGuiCol_SKIF_TextCaption));
  SKIF_ImGui_Selectable   (ICON_FA_SQUARE_PLUS " Add Game");
  ImGui::PopStyleColor    ( );
  SKIF_ImGui_SetMouseCursorHand ( );
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
    AddGamePopup = PopupState_Open;
    SKIF_Tab_ChangeTo = UITab_Library;
  }
  ImGui::SameLine         ( );
  ImGui::Text             (u8"to add the game to the list.");

  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                        "3 ");
  ImGui::SameLine         ( );
  ImGui::TextWrapped      (u8"启动游戏.");

  ImGui::NewLine          ( );
  ImGui::NewLine          ( );

  float fY3 = ImGui::GetCursorPosY();
          
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Failure), ICON_FA_ROCKET);
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
    u8"通过Steam为选定游戏快速推出Special K:"
  );

  SKIF_ImGui_Spacing      ( );

  extern int
      SKIF_Util_RegisterApp (bool force = false);
  if (SKIF_Util_RegisterApp ( ) > 0)
  {
    ImGui::TextWrapped      (u8"您的系统设置为通过Steam快速启动注射.");

    SKIF_ImGui_Spacing      ( );

    ImGui::BeginGroup       ( );
    ImGui::Spacing          ( );
    ImGui::SameLine         ( );
    ImGui::TextColored      (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                          "1 ");
    ImGui::SameLine         ( );
    ImGui::TextWrapped      (u8"在Steam中右键单击所需游戏，然后选择 \"Properties...\".");
    ImGui::EndGroup         ( );

    ImGui::BeginGroup       ( );
    ImGui::Spacing          ( );
    ImGui::SameLine         ( );
    ImGui::TextColored      (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                          "2 ");
    ImGui::SameLine         ( );
    ImGui::TextWrapped      (u8"将以下内容复制并粘贴到 \"启动选项\" 域.");
    ImGui::EndGroup         ( );

    ImGui::TreePush         (u8"");
    ImGui::Spacing          ( );
    ImGui::SameLine         ( );
    ImGui::PushStyleColor   (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption));
    char szSteamCommand[MAX_PATH] = "SKIF %COMMAND%";
    ImGui::InputTextEx      (u8"###Launcher", NULL, szSteamCommand, MAX_PATH, ImVec2(0, 0), ImGuiInputTextFlags_ReadOnly);
    if (ImGui::IsItemActive    ( ))
    {
      extern bool allowShortcutCtrlA;
      allowShortcutCtrlA = false;
    }
    ImGui::PopStyleColor    ( );
    ImGui::TreePop          ( );

    ImGui::BeginGroup       ( );
    ImGui::Spacing          ( );
    ImGui::SameLine         ( );
    ImGui::TextColored      (
      ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
                          "3 ");
    ImGui::SameLine         ( );
    ImGui::TextWrapped      (u8"通过Steam照常启动游戏.");
    ImGui::EndGroup         ( );
  }

  else {
    ImGui::Spacing          ( );
    ImGui::SameLine         ( );
    ImGui::TextColored      (
      ImColor::HSV (0.11F,   1.F, 1.F),
        ICON_FA_TRIANGLE_EXCLAMATION " ");
    ImGui::SameLine         ( );
    ImGui::TextWrapped      (u8"您的系统未设置为使用此安装的Special K通过Steam快速启动注入.");

    SKIF_ImGui_Spacing      ( );
    
    SKIF_ImGui_Spacing      (1.0f);
    ImGui::SameLine         ( );
    
    ImGui::PushStyleColor   (ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption));
    if (ImGui::Button (u8"  将此安装设置为默认装载  "))
      SKIF_Util_RegisterApp(true);
    ImGui::PopStyleColor    ( );
    
    // We need som additional spacing at the bottom here to push down the Components section in the right column
    SKIF_ImGui_Spacing      (2.00f);
  }

  ImGui::NewLine          ( );
  ImGui::NewLine          ( );

  float fY4 = ImGui::GetCursorPosY();
          
  ImGui::TextColored      (ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow), ICON_FA_AWARD);//ICON_FA_WRENCH);
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
    "Tips and Tricks:");

  SKIF_ImGui_Spacing      ( );

  // Show a randomized select of tips and tricks
  SKIF_UI_TipsAndTricks   ( );

  float pushColumnSeparator =
    (900.0f * SKIF_ImGui_GlobalDPIScale) - ImGui::GetCursorPosY                () -
                                          (ImGui::GetTextLineHeightWithSpacing () );

  ImGui::ItemSize (
    ImVec2 (0.0f, pushColumnSeparator)
  );


  ImGui::NextColumn       ( ); // Next Column
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      "关于注射前端 (SKIF):"    );

  SKIF_ImGui_Spacing      ( );

  ImGui::TextWrapped      (u8"您看到的是Special K 注入前端，通常被称为 \"SKIF\".\n\n"
                           u8"注入前端用于管理全局注入服务，该服务在游戏开始时，甚至在已经运行的游戏中注入Special K!\n\n"
                           u8"前端还提供了到特殊位置的方便快捷方式，包括配置和日志文件、云存储以及PCGamingWiki和SteamDB等外部资源.");

  ImGui::SetCursorPosY    (fY1);

  ImGui::TextColored (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
                  u8"多人游戏:");

  SKIF_ImGui_Spacing      ( );

  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImColor::HSV (0.11F,   1.F, 1.F),
      ICON_FA_TRIANGLE_EXCLAMATION " ");
  ImGui::SameLine         (0.0f, 6.0f);
  ImGui::Text             (u8"不要在多人游戏中使用特殊K!");
  ImGui::EndGroup         ( );

  SKIF_ImGui_SetHoverTip (
    u8"在可能存在反作弊保护的特定游戏中."
  );

  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
      ICON_FA_UP_RIGHT_FROM_SQUARE " "      );
  ImGui::SameLine         ( );
  if (ImGui::Selectable   (u8"有关wiki的更多信息"))
    SKIF_Util_OpenURI     (L"https://wiki.special-k.info/en/SpecialK/Global#multiplayer-games");
  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText ( "https://wiki.special-k.info/en/SpecialK/Global#multiplayer-games");
  ImGui::EndGroup         ( );

  ImGui::SetCursorPosY    (fY2);

  ImGui::TextColored (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      u8"关于如何注入Special K的更多信息:"
  );

  SKIF_ImGui_Spacing      ( );

  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );

  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
      ICON_FA_UP_RIGHT_FROM_SQUARE " "      );
  ImGui::SameLine         ( );
  if (ImGui::Selectable   (u8"全局 (system-wide)"))
    SKIF_Util_OpenURI     (L"https://wiki.special-k.info/SpecialK/Global");
  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText ( "https://wiki.special-k.info/SpecialK/Global");
  ImGui::EndGroup         ( );

  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Info),
    ICON_FA_UP_RIGHT_FROM_SQUARE " "      );
  ImGui::SameLine         ( );
  if (ImGui::Selectable   (u8"本地 (game-specific)"))
    SKIF_Util_OpenURI     (L"https://wiki.special-k.info/SpecialK/Local");
  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText ( "https://wiki.special-k.info/SpecialK/Local");
  ImGui::EndGroup         ( );

  ImGui::SetCursorPosY    (fY3);

  ImGui::TextColored      (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      u8"联机资源:"   );
  SKIF_ImGui_Spacing      ( );

  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImColor (25, 118, 210),
      ICON_FA_BOOK " "   );
  ImGui::SameLine         ( );

  if (ImGui::Selectable   (u8"Wiki"))
    SKIF_Util_OpenURI     (L"https://wiki.special-k.info/");

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText ( "https://wiki.special-k.info/");
  ImGui::EndGroup         ( );


  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImColor (114, 137, 218),
      ICON_FA_DISCORD " "   );
  ImGui::SameLine         (35.0f * SKIF_ImGui_GlobalDPIScale);

  if (ImGui::Selectable   (u8"Discord"))
    SKIF_Util_OpenURI     (L"https://discord.gg/specialk");

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText ( "https://discord.gg/specialk");
  ImGui::EndGroup         ( );


  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    (_registry.iStyle == 2) ? ImGui::GetStyleColorVec4(ImGuiCol_SKIF_Yellow) : ImVec4 (ImColor (247, 241, 169)),
      ICON_FA_DISCOURSE " " );
  ImGui::SameLine         ( );

  if (ImGui::Selectable   (u8"Forum"))
    SKIF_Util_OpenURI     (L"https://discourse.differentk.fyi/");

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText ( "https://discourse.differentk.fyi/");
  ImGui::EndGroup         ( );

  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    ImColor (249, 104, 84),
      ICON_FA_PATREON " "   );
  ImGui::SameLine         ( );
  if (ImGui::Selectable   (u8"Patreon"))
    SKIF_Util_OpenURI     (L"https://www.patreon.com/Kaldaien");

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText ( "https://www.patreon.com/Kaldaien");
  ImGui::EndGroup         ( );

  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         ( );
  ImGui::TextColored      (
    (_registry.iStyle == 2) ? ImColor (0, 0, 0) : ImColor (255, 255, 255), // ImColor (226, 67, 40)
      ICON_FA_GITHUB " "   );
  ImGui::SameLine         ( );
  if (ImGui::Selectable   (u8"GitHub"))
    SKIF_Util_OpenURI     (L"https://github.com/SpecialKO");

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText ( "https://github.com/SpecialKO");
  ImGui::EndGroup         ( );

  ImGui::BeginGroup       ( );
  ImGui::Spacing          ( );
  ImGui::SameLine         (0.0f, 10.0f * SKIF_ImGui_GlobalDPIScale);
  //ImGui::SetCursorPosX    (ImGui::GetCursorPosX ( ) + 1.0f);
  ImGui::TextColored      (
    (_registry.iStyle == 2) ? ImColor (0, 0, 0) : ImColor (255, 255, 255), // ImColor (226, 67, 40)
      ICON_FA_FILE_CONTRACT " ");
  ImGui::SameLine         (0.0f, 10.0f);
  if (ImGui::Selectable   (u8"Privacy Policy"))
    SKIF_Util_OpenURI     (L"https://wiki.special-k.info/Privacy");

  SKIF_ImGui_SetMouseCursorHand ();
  SKIF_ImGui_SetHoverText ( "https://wiki.special-k.info/Privacy");
  ImGui::EndGroup         ( );

  // Move up a line to allow the "view release notes..." link to appear without pushing down the Update button
  static SKIF_Updater& _updater = SKIF_Updater::GetInstance ( );
  if ((_updater.GetState ( ) & UpdateFlags_Available) == UpdateFlags_Available)
    ImGui::SetCursorPosY (fY4 - ImGui::GetFontSize());
  else
    ImGui::SetCursorPosY (fY4);

    
  ImGui::PushStyleColor   (
    ImGuiCol_SKIF_TextCaption, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption) * ImVec4(0.5f, 0.5f, 0.5f, 1.0f)
                            );
    
  ImGui::PushStyleColor   (
    ImGuiCol_CheckMark, ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption)
                            );

  ImGui::TextColored (
    ImGui::GetStyleColorVec4(ImGuiCol_SKIF_TextCaption),
      "组成部分:"
  );
    
  ImGui::PushStyleColor   (
    ImGuiCol_SKIF_TextBase, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
                            );
    
  ImGui::PushStyleColor   (
    ImGuiCol_TextDisabled, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) * ImVec4(0.5f, 0.5f, 0.5f, 1.0f)
                            );

  SKIF_ImGui_Spacing      ( );
  
  SKIF_UI_DrawComponentVersion ( );

  ImGui::PopStyleColor    (4);

  ImGui::Columns          (1);

  ImGui::PopStyleColor    ( );
}