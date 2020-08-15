# SI2DSU
SteamInput to DSU wrapper

This utility connects to SteamWorks and serves a controller recognized by SteamInput as a DSU/CemuHook server, while also launching the DSU client executable.
It enables motion controls integration for Steam controller/Dualshock 4 in programs like Cemu/Citra/Dolphin, requiring only the steam client as the driver.
Because it uses SteamInput, it also works over Steam Link/RemotePlay/In-Home-Streaming.

Currently it is Windows-only as it relies on Winsock and the CreateProcess Win32 API, which would have to be replaced with normal sockets and fork()/execve()

# Installation

1. Choose a sacrificial steam game for SI2DSU to steal controller input from. (For example Super Hexagon)
2. Find out the game's Steam AppID using SteamDB (SuperHexagon = 221640)
3. Go to your Steam client installation directory, and if it doesn't already exist, create a folder named "controller_config" inside it.
4. Copy the file "game_actions_480.vdf" from SI2DSU release to the "controller_config" folder.
5. Change the filename to correspond to your sacrificial SteamID ("game_actions_221640.vdf")
6. Go to your sacrificial game's install directory, where the main exe which steam launches is located (SteamInstallDirectory/steamapps/common/Super Hexagon/)
7. Create a folder named "dsu" in that directory, and move all the game's original files which were here in there
8. Copy "sidsu.exe", "dsusettings.txt" and "steam_api64.dll" from the SI2DSU release into the game's folder (beside "dsu")
9. Rename "sidsu.exe" to whatever the game's executable was called ("superhexagon.exe")
10. Open dsusettings.txt and change the first line to point to your client ("Cemu.exe"), and the second line to be the sacrificial AppID
11. At this point, the sacrificial game should work fine, other than the controller input
12. Add the renamed "sidsu.exe" as a non-steam game shortcut into steam
13. Inside steam, set the shortcut's launch options to "-dsumode" and whatever launch options you want your client to have (-dsumode -f -g "Path\To\Game.rpx" to launch a fullscreen game in cemu)
14. Launch the shortcut from Steam's big picture mode (You need to launch some game in the client/emulator, because Steam Overlay only binds to OpenGL/Vulkan/DirectX windows)
15. Press the home/PS/Steam/Xbox button on your controller to open the steam overlay
16. In the overlay bind all the controller buttons to corresponding DSU actions
17. In your client open input settings, set the protocol to DSU/CemuHook and bind all DSU actions to corresponding client actions.
