#include "MouseMessageHandler.h"

LRESULT MouseMessageHandler::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_LBUTTONDOWN:
		int xPos = GET_X_LPARAM(lParam);
		int yPos = GET_Y_LPARAM(lParam);
		break;
	case WM_LBUTTONUP:
		break;
	case WM_MBUTTONDOWN:
		break;
	case WM_MBUTTONUP:
		break;
	case WM_RBUTTONDOWN:
		break;
	case WM_RBUTTONUP:
		break;
	case WM_XBUTTONDOWN:
		break;
	case WM_XBUTTONUP:
		break;
	default:
		return PassNext(uMsg, wParam, lParam);
	}
}
