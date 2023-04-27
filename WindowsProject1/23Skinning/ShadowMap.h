#pragma once

#include "../Common/DxUtil.h"

class ShadowMap
{
public:
	ShadowMap(ID3D12Device* device, UINT width, UINT height);
	ShadowMap(const ShadowMap& other) = delete;
	ShadowMap(ShadowMap&& other) noexcept = default;
	ShadowMap& operator=(const ShadowMap& other) = delete;
	ShadowMap& operator=(ShadowMap&& other) noexcept = default;
	~ShadowMap() = default;

	UINT Width() const;
	UINT Height() const;
	ID3D12Resource* Resource();
	CD3DX12_GPU_DESCRIPTOR_HANDLE Srv() const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE Dsv() const;

	const D3D12_VIEWPORT& Viewport() const;
	const D3D12_RECT& ScissorRect() const;

	void BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
		CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDsv
	);

	void OnResize(UINT newWidth, UINT newHeight);

private:
	void BuildDescriptors();
	void BuildResource();

private:
	ID3D12Device* md3dDevice;
	D3D12_VIEWPORT mViewport;
	D3D12_RECT mScissorRect;

	UINT mWidth;
	UINT mHeight;
	DXGI_FORMAT mFormat;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuDsv;

	Microsoft::WRL::ComPtr<ID3D12Resource> mShadowMap;
};