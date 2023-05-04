#pragma once

#include "../Common/DxUtil.h"

struct OceanBasisConstants
{
	float Amplitude;
	DirectX::XMFLOAT2 Wind;
	UINT ResolutionSize;
	float WaveLength;
};

class OceanMap
{
public:
	OceanMap(ID3D12Device* device, 
		UINT width, 
		UINT height);
	OceanMap(const OceanMap& other) = delete;
	OceanMap(OceanMap&& other) noexcept = default;
	OceanMap& operator=(const OceanMap& other) = delete;
	OceanMap& operator=(OceanMap&& other) noexcept = default;
	~OceanMap() = default;
	
	ID3D12Resource* Output() const;

	void BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
		CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv
	);
	void BuildOceanBasis(
		ID3D12GraphicsCommandList* cmdList, ID3D12RootSignature* rootSig, ID3D12PipelineState* oceanBasisPSO
	);

	UINT GetHTilde0SrvIndexRelative();
	UINT GetHTilde0UavIndexRelative();
	UINT GetHTilde0ConjSrvIndexRelative();
	UINT GetHTilde0ConjUavIndexRelative();
	UINT GetDisplacementMapSrvIndexRelative();
	UINT GetNumBasisConstants();

	static UINT GetNumDescriptors();
	
	void Execute(
		ID3D12GraphicsCommandList* cmdList,
		ID3D12RootSignature* rootSig,
		ID3D12PipelineState* pso
	);

private:
	void BuildDescriptors();
	void BuildResource();

private:
	ID3D12Device* mD3dDevice;
	D3D12_VIEWPORT mViewport;
	D3D12_RECT mScissorRect;

	UINT mWidth;
	UINT mHeight;
	DXGI_FORMAT mDisplacementMapFormat;
	DXGI_FORMAT mBasisFormat;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuSrvHTilde0;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuUavHTilde0;

	CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuSrvHTilde0;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuUavHTilde0;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuSrvHTilde0Conj;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuUavHTilde0Conj;

	CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuSrvHTilde0Conj;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuUavHTilde0Conj;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuSrvDisplacementMap;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuUavDisplacementMap;

	CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuSrvDisplacementMap;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuUavDisplacementMap;

	Microsoft::WRL::ComPtr<ID3D12Resource> mHTilde0;
	Microsoft::WRL::ComPtr<ID3D12Resource> mHTilde0Conj;
	Microsoft::WRL::ComPtr<ID3D12Resource> mDisplacementMap;
};