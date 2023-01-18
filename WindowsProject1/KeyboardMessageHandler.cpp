#include "KeyboardMessageHandler.h"

LRESULT KeyboardMessageHandler::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_SYSKEYDOWN:
		SysKeyDown(wParam);
		break;
	case WM_KEYDOWN:
		KeyDown(wParam);
		break;
	case WM_SYSKEYUP:
		SysKeyUp(wParam);
		break;
	case WM_KEYUP:
		KeyUp(wParam);
		break;
	default:
		return PassNext(uMsg, wParam, lParam);
	}
}
