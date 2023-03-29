#pragma once

#include "../Common/DxUtil.h"

class RenderTarget
{
public:
	RenderTarget(ID3D12Device* device,
		UINT width,
		UINT height,
		DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM);

	RenderTarget(const RenderTarget& rhs) = delete;
	RenderTarget& operator=(const RenderTarget& rhs) = delete;
	~RenderTarget() = default;

	ID3D12Resource* Resource();
	CD3DX12_GPU_DESCRIPTOR_HANDLE Srv();
	CD3DX12_CPU_DESCRIPTOR_HANDLE Rtv();

	void BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE cpuSrv,
		CD3DX12_GPU_DESCRIPTOR_HANDLE gpuSrv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE cpuRtv);

	void OnResize(UINT newWidth, UINT newHeight);

private:
	void BuildDescriptors();
	void BuildResources();

private:
	ID3D12Device* d3dDevice = nullptr;

	UINT width = 0;
	UINT height = 0;
	DXGI_FORMAT format;

	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE gpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuRtv;

	Microsoft::WRL::ComPtr<ID3D12Resource> offScreenTex = nullptr;
};