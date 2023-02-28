#pragma once
#include "Common/MainWindow.h"

class InitApp : public MainWindow
{
public:
	InitApp(HINSTANCE hInstance) : MainWindow(hInstance) {}

private:
	virtual void Update(const GameTimer& gt) override;
	virtual void Draw(const GameTimer& gt) override;
};
