#include "BlurFilter.h"

BlurFilter::BlurFilter(ID3D12Device* device,
	UINT width, UINT height,
	DXGI_FORMAT format)
{
	d3dDevice = device;

	this->width = width;
	this->height = height;
	this->format = format;

	BuildResources();
}

ID3D12Resource* BlurFilter::Output()
{
	return blurMap0.Get();
}

void BlurFilter::BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDescriptor,
	CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuDescriptor,
	UINT descriptorSize)
{
	// Save references to the descriptors. 
	blur0CpuSrv = hCpuDescriptor;
	blur0CpuUav = hCpuDescriptor.Offset(1, descriptorSize);
	blur1CpuSrv = hCpuDescriptor.Offset(1, descriptorSize);
	blur1CpuUav = hCpuDescriptor.Offset(1, descriptorSize);

	blur0GpuSrv = hGpuDescriptor;
	blur0GpuUav = hGpuDescriptor.Offset(1, descriptorSize);
	blur1GpuSrv = hGpuDescriptor.Offset(1, descriptorSize);
	blur1GpuUav = hGpuDescriptor.Offset(1, descriptorSize);

	BuildDescriptors();
}

void BlurFilter::OnResize(UINT newWidth, UINT newHeight)
{
	if ((width != newWidth) || (height != newHeight))
	{
		width = newWidth;
		height = newHeight;

		BuildResources();

		// New resource, so we need new descriptors to that resource.
		BuildDescriptors();
	}
}

void BlurFilter::Execute(ID3D12GraphicsCommandList* cmdList,
	ID3D12RootSignature* rootSig,
	ID3D12PipelineState* horzBlurPSO,
	ID3D12PipelineState* vertBlurPSO,
	ID3D12Resource* input,
	int blurCount)
{
	auto weights = CalcGaussWeights(2.5f);
	int blurRadius = (int)weights.size() / 2;

	cmdList->SetComputeRootSignature(rootSig);

	cmdList->SetComputeRoot32BitConstants(0, 1, &blurRadius, 0);
	cmdList->SetComputeRoot32BitConstants(0, static_cast<UINT>(weights.size()), weights.data(), 1);

	const auto barrierMakeInputCopySrc = CD3DX12_RESOURCE_BARRIER::Transition(
		input,
		D3D12_RESOURCE_STATE_RENDER_TARGET, 
		D3D12_RESOURCE_STATE_COPY_SOURCE);

	cmdList->ResourceBarrier(1, &barrierMakeInputCopySrc);

	const auto barrierMakeBlur0CopyDest = CD3DX12_RESOURCE_BARRIER::Transition(
		blurMap0.Get(),
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_COPY_DEST);

	cmdList->ResourceBarrier(1, &barrierMakeBlur0CopyDest);

	// Copy the input (back-buffer in this example) to BlurMap0.
	cmdList->CopyResource(blurMap0.Get(), input);

	const auto barrierMakeBlur0Read = CD3DX12_RESOURCE_BARRIER::Transition(
		blurMap0.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_GENERIC_READ);

	cmdList->ResourceBarrier(1, &barrierMakeBlur0Read);

	const auto barrierMakeBlur1Uav = CD3DX12_RESOURCE_BARRIER::Transition(
		blurMap1.Get(),
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	cmdList->ResourceBarrier(1, &barrierMakeBlur1Uav);

	for (int i = 0; i < blurCount; ++i)
	{
		//
		// Horizontal Blur pass.
		//

		cmdList->SetPipelineState(horzBlurPSO);

		cmdList->SetComputeRootDescriptorTable(1, blur0GpuSrv);
		cmdList->SetComputeRootDescriptorTable(2, blur1GpuUav);

		// How many groups do we need to dispatch to cover a row of pixels, where each
		// group covers 256 pixels (the 256 is defined in the ComputeShader).
		UINT numGroupsX = (UINT)ceilf(width / 256.0f);
		cmdList->Dispatch(numGroupsX, height, 1);

		const auto barrierMakeBlur0Uav = CD3DX12_RESOURCE_BARRIER::Transition(
			blurMap0.Get(),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		cmdList->ResourceBarrier(1, &barrierMakeBlur0Uav);

		const auto barrierMakeBlur1Read = CD3DX12_RESOURCE_BARRIER::Transition(
			blurMap1.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_GENERIC_READ);

		cmdList->ResourceBarrier(1, &barrierMakeBlur1Read);

		//
		// Vertical Blur pass.
		//

		cmdList->SetPipelineState(vertBlurPSO);

		cmdList->SetComputeRootDescriptorTable(1, blur1GpuSrv);
		cmdList->SetComputeRootDescriptorTable(2, blur0GpuUav);

		// How many groups do we need to dispatch to cover a column of pixels, where each
		// group covers 256 pixels  (the 256 is defined in the ComputeShader).
		UINT numGroupsY = (UINT)ceilf(height / 256.0f);
		cmdList->Dispatch(width, numGroupsY, 1);

		const auto barrierMakeBlur0Read0 = CD3DX12_RESOURCE_BARRIER::Transition(
			blurMap0.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 
			D3D12_RESOURCE_STATE_GENERIC_READ);
		cmdList->ResourceBarrier(1, &barrierMakeBlur0Read0);

		const auto barrierMakeBlur1Uav0 = CD3DX12_RESOURCE_BARRIER::Transition(
			blurMap1.Get(),
			D3D12_RESOURCE_STATE_GENERIC_READ, 
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		cmdList->ResourceBarrier(1, &barrierMakeBlur1Uav0);
	}
}

std::vector<float> BlurFilter::CalcGaussWeights(float sigma)
{
	float twoSigma2 = 2.0f * sigma * sigma;

	// Estimate the blur radius based on sigma since sigma controls the "width" of the bell curve.
	// For example, for sigma = 3, the width of the bell curve is 
	int blurRadius = (int)ceil(2.0f * sigma);

	assert(blurRadius <= MaxBlurRadius);

	std::vector<float> weights;
	weights.resize(2 * blurRadius + 1);

	float weightSum = 0.0f;

	for (int i = -blurRadius; i <= blurRadius; ++i)
	{
		float x = (float)i;

		weights[i + blurRadius] = expf(-x * x / twoSigma2);

		weightSum += weights[i + blurRadius];
	}

	// Divide by the sum so all the weights add up to 1.0.
	for (int i = 0; i < weights.size(); ++i)
	{
		weights[i] /= weightSum;
	}

	return weights;
}

void BlurFilter::BuildDescriptors()
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

	uavDesc.Format = format;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Texture2D.MipSlice = 0;

	d3dDevice->CreateShaderResourceView(blurMap0.Get(), &srvDesc, blur0CpuSrv);
	d3dDevice->CreateUnorderedAccessView(blurMap0.Get(), nullptr, &uavDesc, blur0CpuUav);

	d3dDevice->CreateShaderResourceView(blurMap1.Get(), &srvDesc, blur1CpuSrv);
	d3dDevice->CreateUnorderedAccessView(blurMap1.Get(), nullptr, &uavDesc, blur1CpuUav);
}

void BlurFilter::BuildResources()
{
	// Note, compressed formats cannot be used for UAV.  We get error like:
	// ERROR: ID3D11Device::CreateTexture2D: The format (0x4d, BC3_UNORM) 
	// cannot be bound as an UnorderedAccessView, or cast to a format that
	// could be bound as an UnorderedAccessView.  Therefore this format 
	// does not support D3D11_BIND_UNORDERED_ACCESS.

	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = width;
	texDesc.Height = height;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = format;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	ThrowIfFailed(d3dDevice->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&blurMap0)));

	ThrowIfFailed(d3dDevice->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&blurMap1)));
}

const int BlurFilter::DescriptorCount()
{
	return 4;
}
