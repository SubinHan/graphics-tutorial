#pragma once
#include "MainWindow.h"

class InitApp : public MainWindow
{
public:
	InitApp(HINSTANCE hInstance) : MainWindow(hInstance) {}

private:
	virtual void InitMessageHandlers() override;
	virtual void Update(const GameTimer& gt) override;
	virtual void Draw(const GameTimer& gt) override;
};

class InitAppMouseMessageHandler : public MouseMessageHandler
{
public:
	InitAppMouseMessageHandler(HWND hwnd) : MouseMessageHandler(hwnd) {}

private:
	virtual void LeftDown(int x, int y, short keyState) override;
};

class InitAppKeyboardMessageHandler : public KeyboardMessageHandler
{
public:
	InitAppKeyboardMessageHandler(HWND hwnd) : KeyboardMessageHandler(hwnd) {}

private:
	virtual void KeyDown(int keyCode) override;
	virtual void KeyUp(int keyCode) override;
	virtual void SysKeyDown(int keyCode) override;
	virtual void SysKeyUp(int keyCode) override;
};