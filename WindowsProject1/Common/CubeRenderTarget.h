#pragma once

#include "DxUtil.h"

class CubeRenderTarget
{
public:
	CubeRenderTarget(
		ID3D12Device* device,
		LONG width,
		LONG height,
		DXGI_FORMAT format
	);

	CubeRenderTarget(const CubeRenderTarget& rhs) = delete;
	CubeRenderTarget& operator=(const CubeRenderTarget& rhs) = delete;
	~CubeRenderTarget() = default;

	ID3D12Resource* Resource();
	CD3DX12_GPU_DESCRIPTOR_HANDLE Srv();
	CD3DX12_CPU_DESCRIPTOR_HANDLE Rtv(int faceIndex);

	D3D12_VIEWPORT Viewport() const;
	D3D12_RECT ScissorRect() const;

	void BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
		CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuRtv[6]
	);

	void OnResize(LONG newWidth, LONG newHeight);

private:
	void BuildDescriptors();
	void BuildResource();

private:
	ID3D12Device* d3dDevice = nullptr;

	D3D12_VIEWPORT viewport;
	D3D12_RECT scissorRect;

	LONG width = 0;
	LONG height = 0;
	DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;

	CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuRtv[6];

	Microsoft::WRL::ComPtr<ID3D12Resource> cubeMap = nullptr;

};