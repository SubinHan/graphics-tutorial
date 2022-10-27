#pragma once

#include <Windows.h>
#include <dxgi1_6.h>
#include <vector>
#include <string>

class DxgiLogger 
{
public:
	static void LogAdapters();

private:
	static void LogAdapterOutputs(IDXGIAdapter* adapter);
	static void LogOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format);
};
