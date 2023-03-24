#pragma once

#include "../Common/DxUtil.h"

class BlurFilter
{
public:
	BlurFilter(
		ID3D12Device* device,
		UINT width,
		UINT height,
		DXGI_FORMAT format);

	BlurFilter(const BlurFilter& rhs) = delete;
	BlurFilter& operator=(const BlurFilter& rhs) = delete;
	~BlurFilter() = default;

	ID3D12Resource* Output();

	void BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDescriptor,
		CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuDescriptor,
		UINT descriptorSize);

	void OnResize(UINT newWidth, UINT newHeight);

	void Execute(
		ID3D12GraphicsCommandList* cmdList,
		ID3D12RootSignature* rootSig,
		ID3D12PipelineState* horzBlurPSO,
		ID3D12PipelineState* vertBlurPSO,
		ID3D12Resource* input,
		int blurCount);

private:
	std::vector<float> CalcGaussWeights(float sigma);

	void BuildDescriptors();
	void BuildResources();

private:

	const int MaxBlurRadius = 5;

	ID3D12Device* d3dDevice = nullptr;

	UINT width = 0;
	UINT height = 0;
	DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;

	CD3DX12_CPU_DESCRIPTOR_HANDLE blur0CpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE blur0CpuUav;

	CD3DX12_CPU_DESCRIPTOR_HANDLE blur1CpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE blur1CpuUav;

	CD3DX12_GPU_DESCRIPTOR_HANDLE blur0GpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE blur0GpuUav;

	CD3DX12_GPU_DESCRIPTOR_HANDLE blur1GpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE blur1GpuUav;

	// Two for ping-ponging the textures.
	Microsoft::WRL::ComPtr<ID3D12Resource> blurMap0 = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> blurMap1 = nullptr;

};