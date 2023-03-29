#include "RenderTarget.h"

RenderTarget::RenderTarget(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format)
	: d3dDevice(device), width(width), height(height), format(format)
{
	BuildResources();
}

ID3D12Resource* RenderTarget::Resource()
{
	return offScreenTex.Get();
}

CD3DX12_GPU_DESCRIPTOR_HANDLE RenderTarget::Srv()
{
	return gpuSrv;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE RenderTarget::Rtv()
{
	return cpuRtv;
}

void RenderTarget::BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE cpuSrv, CD3DX12_GPU_DESCRIPTOR_HANDLE gpuSrv,
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuRtv)
{
	this->cpuSrv = cpuSrv;
	this->gpuSrv = gpuSrv;
	this->cpuRtv = cpuRtv;

	BuildDescriptors();
}

void RenderTarget::OnResize(UINT newWidth, UINT newHeight)
{
	if (width == newWidth && height == newHeight)
		return;

	width = newWidth;
	height = newHeight;
	BuildResources();
	BuildDescriptors();
}

void RenderTarget::BuildDescriptors()
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;

	d3dDevice->CreateShaderResourceView(
		offScreenTex.Get(),
		&srvDesc,
		cpuSrv);

	d3dDevice->CreateRenderTargetView(
		offScreenTex.Get(),
		nullptr,
		cpuRtv
	);
}

void RenderTarget::BuildResources()
{
	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));

	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.MipLevels = 1;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	texDesc.Format = format;
	texDesc.Alignment = 0;
	texDesc.Height = height;
	texDesc.DepthOrArraySize = 1;
	texDesc.Width = width;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 1;

	const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	ThrowIfFailed(
		d3dDevice->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(offScreenTex.GetAddressOf())
		)
	);
}
