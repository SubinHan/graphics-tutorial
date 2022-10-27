#include "KeyboardMessageHandler.h"

LRESULT KeyboardMessageHandler::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_SYSKEYDOWN:
		if (wParam == VK_ESCAPE)
		{
			;
		}
		break;
	case WM_KEYDOWN:
		break;
	case WM_SYSKEYUP:
		break;
	case WM_KEYUP:
		break;
	default:
		return PassNext(uMsg, wParam, lParam);
	}
}
