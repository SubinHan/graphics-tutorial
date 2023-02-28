#pragma once

#include <Windows.h>
#include "../../inlcude/directx/d3dx12.h"
#include <dxgi1_6.h>
#include <d3d12.h>
#include <windows.foundation.h>
#include <wrl\wrappers\corewrappers.h>
#include <wrl\client.h>
#include <DirectXColors.h>

using Microsoft::WRL::ComPtr;

class DxDevice
{
public:
	DxDevice(HWND mainWindow);

	void CreateDepthStencilView();
	void InitScreenViewport();
	void InitScissorRect();

	void ResetCommandList();
	ID3D12Resource* CurrentBackBuffer();
	D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView();
	D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView();
	void FlushCommandQueue();
	void ResetAllSwapChainBuffers();
	void ResetDepthStencilBuffer();
	void ResizeBuffers();

	ComPtr<ID3D12Device>& GetD3DDevice();
	ComPtr<ID3D12CommandQueue>& GetCommandQueue();
	ComPtr<ID3D12CommandAllocator>& GetCommandListAllocator();
	ComPtr<ID3D12GraphicsCommandList>& GetCommandList();
	ComPtr<IDXGISwapChain>& GetSwapChain();
	void RSSetViewports(UINT numViewports);
	void RSSetScissorRects(UINT numRects);
	void SwapBuffers();

	DXGI_FORMAT GetBackBufferFormat();
	DXGI_FORMAT GetDepthStencilFormat();
	bool GetMsaaState();
	UINT GetMsaaQuality();

	UINT GetClientWidth();
	UINT GetClientHeight();
	void SetClientWidth(UINT width);
	void SetClientHeight(UINT height);
	UINT GetSwapChainBufferCount();

	ComPtr<ID3D12Fence> GetFence();
	D3D12_VIEWPORT& GetScreenViewport();
	tagRECT& GetScissorRect();
	UINT GetCbvSrvUavDescriptorSize();

	UINT GetCurrentFence();
	UINT IncreaseFence();

public:
	static constexpr UINT SWAP_CHAIN_BUFFER_COUNT = 2;

private:
	void Init();
	void CreateDevice();
	void CreateFence();
	void CheckMsaa();
	void CreateCommandQueueAndList();
	void CreateSwapChain();
	void CreateRtvAndDsvDescriptorHeaps();
	void CreateRenderTargetView();

private:
	ComPtr<IDXGIFactory> pDxgiFactory;
	ComPtr<ID3D12Device> pD3dDevice;
	ComPtr<ID3D12Fence> fence;

	UINT rtvDescriptorSize = 0;
	UINT dsvDescriptorSize = 0;
	UINT cbvDescriptorSize = 0;

	ComPtr<ID3D12CommandQueue> commandQueue;
	ComPtr<ID3D12CommandAllocator> commandListAllocator;
	ComPtr<ID3D12GraphicsCommandList> commandList;
	ComPtr<IDXGISwapChain> swapChain;
	ComPtr<ID3D12DescriptorHeap> rtvHeap;
	ComPtr<ID3D12DescriptorHeap> dsvHeap;
	ComPtr<ID3D12Resource> swapChainBuffer[SWAP_CHAIN_BUFFER_COUNT];
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
	UINT currentFence = 0;

	D3D12_VIEWPORT screenViewport;
	tagRECT scissorRect;
};