#pragma once
#include <Windows.h>

class AbstractMessageHandler
{
private:
	HWND m_hwnd;
	AbstractMessageHandler* m_next;

public:
	AbstractMessageHandler(HWND hwnd) : m_hwnd(hwnd), m_next(nullptr)
	{
	}

	~AbstractMessageHandler()
	{
		if(m_next)
			delete m_next;
	}

	AbstractMessageHandler* SetNext(AbstractMessageHandler* next)
	{
		m_next = next;
		return next;
	}

	LRESULT PassNext(UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		if (m_next)
		{
			m_next->HandleMessage(uMsg, wParam, lParam);
		}
		else
		{
			return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
		}
	}

	virtual LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);
};