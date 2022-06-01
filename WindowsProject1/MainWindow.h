#pragma once

#include <Windows.h>
#include <d2d1.h>
#pragma comment(lib, "d2d1")

#include <memory>
#include "BaseWindow.h"
#include "AbstractMessageHandler.h"
#include "MouseMessageHandler.h"
#include "KeyboardMessageHandler.h"

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
	HRESULT CreateGraphicsResources();
	void DiscardGraphicsResources();
	void CalculateLayout();
	void OnPaint();

};