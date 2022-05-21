#pragma once

#include <WindowsX.h>
#include "AbstractMessageHandler.h"

class MouseMessageHandler : public AbstractMessageHandler
{
public:
	MouseMessageHandler(HWND hwnd) : AbstractMessageHandler(hwnd) {}

	virtual LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) override;
};