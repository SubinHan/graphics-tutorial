#pragma once

#include "../Common/DxUtil.h"

class SobelFilter
{
public:
	SobelFilter(
		ID3D12Device* device,
		UINT width,
		UINT height,
		DXGI_FORMAT format);

	SobelFilter(const SobelFilter& rhs) = delete;
	SobelFilter& operator=(const SobelFilter& rhs) = delete;
	~SobelFilter() = default;
	
	ID3D12Resource* Output();

	void BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDescriptor,
		CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuDescriptor,
		UINT descriptorSize);

	void OnResize(UINT newWidth, UINT newHeight);

	void Execute(
		ID3D12GraphicsCommandList* cmdList,
		ID3D12Resource* input);
	const int DescriptorCount();

private:
	void BuildRootSignatures();
	void BuildComputeRootSignature();
	void BuildCompositeRootSignature();
	void BuildDescriptors();
	void BuildResources();
	void BuildShaders();
	void BuildPSOs();
	void BuildComputePSO();
	void BuildCompositePSO();

	void DrawFullScreenQuad(ID3D12GraphicsCommandList* cmdList);

private:
	ID3D12Device* d3dDevice = nullptr;

	UINT width = 0;
	UINT height = 0;
	DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;

	CD3DX12_CPU_DESCRIPTOR_HANDLE sobelInputCpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE sobelInputGpuSrv;

	CD3DX12_CPU_DESCRIPTOR_HANDLE sobelResultCpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE sobelResultCpuUav;

	CD3DX12_GPU_DESCRIPTOR_HANDLE sobelResultGpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE sobelResultGpuUav;

	Microsoft::WRL::ComPtr<ID3DBlob> shaderVs;
	Microsoft::WRL::ComPtr<ID3DBlob> shaderPs;
	Microsoft::WRL::ComPtr<ID3DBlob> shaderCs;
	
	Microsoft::WRL::ComPtr<ID3D12Resource> sobelInput = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> sobelResult = nullptr;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> computeRootSig = nullptr;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> compositeRootSig = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> comuptePso = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> compositePso = nullptr;

	std::vector<D3D12_INPUT_ELEMENT_DESC> compositeInputLayout;
};