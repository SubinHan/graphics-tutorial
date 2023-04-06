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
	
	ID2D1Factory* pFactory;

protected:
	GameTimer timer;
	DxDevice* device;
	std::wstring mainWndCaption;

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

	virtual void OnMouseLeftDown(int x, int y, short keyState) {};
	virtual void OnMouseLeftUp(int x, int y, short keyState) {};
	virtual void OnMouseMiddleDown(int x, int y, short keyState) {};
	virtual void OnMouseMiddleUp(int x, int y, short keyState) {};
	virtual void OnMouseRightDown(int x, int y, short keyState) {};
	virtual void OnMouseRightUp(int x, int y, short keyState) {};
	virtual void OnMouseXDown(int x, int y, short keyState) {};
	virtual void OnMouseXUp(int x, int y, short keyState) {};
	virtual void OnMouseWheel(short delta, short keyState) {}
	virtual void OnMouseHover(int x, int y) {}
	virtual void OnMouseLeave() {}
	virtual void OnMouseMove(int x, int y, short keyState) {}

	virtual void OnKeyDown(WPARAM windowVirtualKeyCode) {}
	virtual void OnKeyUp(WPARAM windowVirtualKeyCode) {}


	void CalculateFrameStats();
};