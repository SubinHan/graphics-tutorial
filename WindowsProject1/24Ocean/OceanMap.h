#pragma once

#include "../Common/DxUtil.h"

struct OceanBasisConstants
{
	float Amplitude;
	DirectX::XMFLOAT2 Wind;
	UINT ResolutionSize;
	float WaveLength;
};

struct FrequencyConstants
{
	UINT ResolutionSize;
	float WaveLength;
	float WavveTime;
};

struct FftConstants
{
	UINT ResolutionSize;
	BOOL WaveLength;
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
	void ComputeOceanFrequency(ID3D12GraphicsCommandList* cmdList, ID3D12RootSignature* rootSig,
	                           ID3D12PipelineState* oceanFrequencyPSO, float waveTime);
	
	void ComputeOceanDisplacement(ID3D12GraphicsCommandList* cmdList,
	                              ID3D12RootSignature* rootSig,
	                              ID3D12PipelineState* shiftCsPso,
	                              ID3D12PipelineState* bitReversalCsPso,
	                              ID3D12PipelineState* fft1dCsPso,
	                              ID3D12PipelineState* transposeCsPso);
	void Dispatch(ID3D12GraphicsCommandList* cmdList);

	void Shift(ID3D12GraphicsCommandList* cmdList, ID3D12PipelineState* shiftCsPso, FftConstants& c);
	void BitReversal(ID3D12GraphicsCommandList* cmdList, ID3D12PipelineState* bitReversalCsPso, FftConstants& c);
	void Fft1d(ID3D12GraphicsCommandList* cmdList, ID3D12PipelineState* fft1dCsPso, FftConstants& c);
	void Transpose(ID3D12GraphicsCommandList* cmdList, ID3D12PipelineState* transposeCsPso, FftConstants& c);

	CD3DX12_CPU_DESCRIPTOR_HANDLE GetCpuHTilde0Srv();
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetCpuHTilde0Uav();
	CD3DX12_GPU_DESCRIPTOR_HANDLE GetGpuHTilde0Srv();
	CD3DX12_GPU_DESCRIPTOR_HANDLE GetGpuHTilde0Uav();
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetCpuHTilde0ConjSrv();
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetCpuHTilde0ConjUav();
	CD3DX12_GPU_DESCRIPTOR_HANDLE GetGpuHTilde0ConjSrv();
	CD3DX12_GPU_DESCRIPTOR_HANDLE GetGpuHTilde0ConjUav();
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetCpuHTildeSrv();
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetCpuHTildeUav();
	CD3DX12_GPU_DESCRIPTOR_HANDLE GetGpuHTildeSrv();
	CD3DX12_GPU_DESCRIPTOR_HANDLE GetGpuHTildeUav();
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetCpuDisplacementMapSrv();
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetCpuDisplacementMapUav();
	CD3DX12_GPU_DESCRIPTOR_HANDLE GetGpuDisplacementMapSrv();
	CD3DX12_GPU_DESCRIPTOR_HANDLE GetGpuDisplacementMapUav();
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetCpuDescriptorEnd();

	UINT GetNumBasisConstants();
	UINT GetNumFrequencyConstants();
	UINT GetNumFftConstants();

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

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuSrvHTilde;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuUavHTilde;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuSrvHTilde;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuUavHTilde;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuSrvDisplacementMap0;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuUavDisplacementMap0;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuSrvDisplacementMap0;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuUavDisplacementMap0;

	// for ping-pong
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuSrvDisplacementMap1;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuUavDisplacementMap1;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuSrvDisplacementMap1;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuUavDisplacementMap1;

	Microsoft::WRL::ComPtr<ID3D12Resource> mHTilde0;
	Microsoft::WRL::ComPtr<ID3D12Resource> mHTilde0Conj;
	Microsoft::WRL::ComPtr<ID3D12Resource> mHTilde;
	Microsoft::WRL::ComPtr<ID3D12Resource> mDisplacementMap0;
	Microsoft::WRL::ComPtr<ID3D12Resource> mDisplacementMap1;
};