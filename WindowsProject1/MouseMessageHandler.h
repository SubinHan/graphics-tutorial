#pragma once

#include <WindowsX.h>
#include "AbstractMessageHandler.h"
#include "DXGILogger.h"

class MouseMessageHandler : public AbstractMessageHandler
{
public:
	MouseMessageHandler(HWND hwnd) : AbstractMessageHandler(hwnd) {}

	virtual void LeftDown(int x, int y, short keyState) {};
	virtual void LeftUp(int x, int y, short keyState) {};
	virtual void MiddleDown(int x, int y, short keyState) {};
	virtual void MiddleUp(int x, int y, short keyState) {};
	virtual void RightDown(int x, int y, short keyState) {};
	virtual void RightUp(int x, int y, short keyState) {};
	virtual void XDown(int x, int y, short keyState) {};
	virtual void XUp(int x, int y, short keyState) {};
	virtual void MouseWheel(short delta, short keyState) {}
	virtual void MouseHover(int x, int y) {}
	virtual void MouseLeave() {}
	virtual void MouseMove(int x, int y, short keyState) {}

private:
	virtual LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) override;
};