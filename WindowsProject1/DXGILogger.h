#pragma once

#include <Windows.h>
#include <dxgi.h>
#include <vector>
#include <string>

class DXGILogger 
{
public:
	static void LogAdapters();

private:
	static void LogAdapterOutputs(IDXGIAdapter* adapter);
	static void LogOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format);
};
