#pragma once

#include <Windows.h>
#include <dxgi1_6.h>
#include <d3d12.h>
#include <windows.foundation.h>
#include <wrl\wrappers\corewrappers.h>
#include <wrl\client.h>

using Microsoft::WRL::ComPtr;

class DxDevice
{
	static const UINT swapChainBufferCount = 2;

	IDXGIFactory* pDxgiFactory;
	ID3D12Device* pD3dDevice;
	ID3D12Fence* fence;

	UINT rtvDescriptorSize;
	UINT dsvDescriptorSize;
	UINT cbvDescriptorSize;

	ComPtr<ID3D12CommandQueue> commandQueue;
	ComPtr<ID3D12CommandAllocator> commandListAllocator;
	ComPtr<ID3D12GraphicsCommandList> commandList;
	ComPtr<IDXGISwapChain> swapChain;
	ComPtr<ID3D12DescriptorHeap> rtvHeap;
	ComPtr<ID3D12DescriptorHeap> dsvHeap;
	ComPtr<ID3D12Resource> swapChainBuffer[swapChainBufferCount];
	ComPtr<ID3D12Resource> depthStencilBuffer;

	UINT msaaQuality;
	bool msaaState;
	DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	HWND mainWindow;
	UINT clientWidth;
	UINT clientHeight;
	UINT clientRefreshRate;

	UINT currentBackBuffer = 0;

public:
	DxDevice(HWND mainWindow);

private:
	void Init();
	void CreateDevice();
	void CreateFence();
	void CheckMsaa();
	void CreateCommandQueueAndList();
	void CreateSwapChain();
	void CreateRtvAndDsvDescriptorHeaps();
	void CreateRenderTargetView();
	void CreateDepthStencilView();
};