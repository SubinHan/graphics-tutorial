#pragma once

#include <WindowsX.h>
#include "AbstractMessageHandler.h"

class KeyboardMessageHandler : public AbstractMessageHandler
{
public:
	KeyboardMessageHandler(HWND hwnd) : AbstractMessageHandler(hwnd) {}

	virtual void KeyDown(int KeyCode) {};
	virtual void KeyUp(int KeyCode) {};
	virtual void SysKeyDown(int KeyCode) {};
	virtual void SysKeyUp(int KeyCode) {};

private:
	virtual LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) override;
};