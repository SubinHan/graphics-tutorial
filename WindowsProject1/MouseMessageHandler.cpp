#include "MouseMessageHandler.h"

LRESULT MouseMessageHandler::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	int xPos = GET_X_LPARAM(lParam);
	int yPos = GET_Y_LPARAM(lParam);
	int keyState = GET_KEYSTATE_WPARAM(wParam);

	switch (uMsg)
	{
	case WM_LBUTTONDOWN:
		LeftDown(xPos, yPos, keyState);
		break;
	case WM_LBUTTONUP:
		LeftUp(xPos, yPos, keyState);
		break;
	case WM_MBUTTONDOWN:
		MiddleDown(xPos, yPos, keyState);
		break;
	case WM_MBUTTONUP:
		MiddleUp(xPos, yPos, keyState);
		break;
	case WM_RBUTTONDOWN:
		RightDown(xPos, yPos, keyState);
		break;
	case WM_RBUTTONUP:
		RightUp(xPos, yPos, keyState);
		break;
	case WM_XBUTTONDOWN:
		XDown(xPos, yPos, keyState);
		break;
	case WM_XBUTTONUP:
		XUp(xPos, yPos, keyState);
		break;
	case WM_MOUSEWHEEL:
		MouseWheel(GET_WHEEL_DELTA_WPARAM(wParam), keyState);
		break;
	case WM_MOUSEHOVER:
		MouseHover(xPos, yPos);
		break;
	case WM_MOUSELEAVE:
		MouseLeave();
		break;
	case WM_MOUSEMOVE:
		MouseMove(xPos, yPos, keyState);
		break;

	default:
		return PassNext(uMsg, wParam, lParam);
	}
}
