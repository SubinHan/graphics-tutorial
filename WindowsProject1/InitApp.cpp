#include "InitApp.h"

void InitApp::InitMessageHandlers()
{
	AddMessageHandler(new InitAppMouseMessageHandler(m_hwnd));
	AddMessageHandler(new InitAppKeyboardMessageHandler(m_hwnd));
}

void InitApp::Update(const GameTimer& gt)
{
}

void InitApp::Draw(const GameTimer& gt)
{
	device->ResetCommandList();
}

void InitAppMouseMessageHandler::LeftDown(int x, int y, short keyState)
{
	DxgiLogger::LogAdapters();
}

void InitAppKeyboardMessageHandler::KeyDown(int keyCode)
{
}

void InitAppKeyboardMessageHandler::KeyUp(int keyCode)
{
}

void InitAppKeyboardMessageHandler::SysKeyDown(int keyCode)
{
}

void InitAppKeyboardMessageHandler::SysKeyUp(int keyCode)
{
}
