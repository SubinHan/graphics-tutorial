#pragma once

#include <WindowsX.h>
#include "AbstractMessageHandler.h"
#include "DXGILogger.h"

class MouseMessageHandler : public AbstractMessageHandler
{
public:
	MouseMessageHandler(HWND hwnd) : AbstractMessageHandler(hwnd) {}

	virtual LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) override;
};