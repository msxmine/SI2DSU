# SI2DSU
SteamInput to DSU wrapper

This utility connects to SteamWorks and serves a controller recognized by SteamInput as a DSU/CemuHook server, while also launching the DSU client executable.
It enables motion controls integration for Steam controller/Dualshock 4 in programs like Cemu/Citra/Dolphin, requiring only the steam client as the driver.
Because it uses SteamInput, it also works over Steam Link/RemotePlay/In-Home-Streaming.

Currently it is Windows-only as it relies on Winsock and the CreateProcess Win32 API, which would have to be replaced with normal sockets and fork()/execve()
