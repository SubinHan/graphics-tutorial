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
#include "DXGILogger.h"
#include "DxDebug.h"
#include "DxDevice.h"
#include "GameTimer.h"

class MainWindow : public BaseWindow<MainWindow>
{
	bool isPaused = false;
	bool isMinimized = false;
	bool isMaximized = false;
	bool isResizing = false;
	bool isFullscreenActivated = false;

	AbstractMessageHandler* pMessageHandler;
	ID2D1Factory* pFactory;

	GameTimer timer;

protected:
	DxDevice* device;

public:
	MainWindow(HINSTANCE hInstance);

	float AspectRatio() const;

	int Run();

	virtual bool Initialize();

	PCWSTR ClassName() const;
	LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);

	void CreateDevice();
	void ReleaseDevice();

	virtual void OnResize();
	virtual void Update(const GameTimer& gt) = 0;
	virtual void Draw(const GameTimer& gt) = 0;

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

	void CalculateFrameStats();
};