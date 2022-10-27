#pragma once

#include <Windows.h>
#include <d2d1.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <windows.foundation.h>
#include <wrl\wrappers\corewrappers.h>
#include <wrl\client.h>

#include <memory>
#include "BaseWindow.h"
#include "AbstractMessageHandler.h"
#include "MouseMessageHandler.h"
#include "KeyboardMessageHandler.h"
#include "DXGILogger.h"
#include "DxDebug.h"

class MainWindow : public BaseWindow<MainWindow>
{
	AbstractMessageHandler* pMessageHandler;
	ID2D1Factory* pFactory;
	ID2D1HwndRenderTarget* pRenderTarget;
	ID2D1SolidColorBrush* pBrush;
	D2D1_ELLIPSE ellipse;

public:
	PCWSTR ClassName() const;
	LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);

	void InitMessageHandlers();
	void DestroyMessageHandlers();

	void CreateDevice();

	HRESULT CreateGraphicsResources();
	void DiscardGraphicsResources();
	void CalculateLayout();
	void OnPaint();

};