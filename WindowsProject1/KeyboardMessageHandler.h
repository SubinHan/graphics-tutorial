#pragma once

#include <WindowsX.h>
#include "AbstractMessageHandler.h"

class KeyboardMessageHandler : public AbstractMessageHandler
{
public:
	KeyboardMessageHandler(HWND hwnd) : AbstractMessageHandler(hwnd) {}

	virtual LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) override;
};