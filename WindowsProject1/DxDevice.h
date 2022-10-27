#pragma once

#include <Windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <windows.foundation.h>
#include <wrl\wrappers\corewrappers.h>
#include <wrl\client.h>

class DxDevice
{
	IDXGIFactory* pDxgiFactory;
	ID3D12Device* pD3dDevice;
	ID3D12Fence* fence;

	UINT rtvDescriptorSize;
	UINT dsvDescriptorSize;
	UINT cbvDescriptorSize;

	UINT msaaQuality;
	DXGI_FORMAT backBufferFormat;

public:
	DxDevice();

private:
	void Init();
	void CreateDevice();
	void CreateFence();
	void CheckMsaa();
};