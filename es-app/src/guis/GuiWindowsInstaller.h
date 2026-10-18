// SPDX-License-Identifier: MIT
#pragma once
#if ESNE

class Window;

// ES-NE: WINDOWS GAMES menu. Installs a Windows game from a disc, a disc image, an installer or a folder
// through "esctl windows" (see docs/ESCTL_CONTRACT.md in ES-NE): choose the source, the name and the method,
// prepare (progress shown), run the installer with ES stepped aside, pick the game program if needed, reload the game lists.
class GuiWindowsInstaller
{
public:
	static void open(Window* window);
};

#endif
