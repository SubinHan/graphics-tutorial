#include "OceanMap.h"

OceanMap::OceanMap(ID3D12Device* device, UINT width, UINT height)
	: mD3dDevice{ device },
	mViewport{},
	mScissorRect{},
	mWidth{ width },
	mHeight{ height },
	mDisplacementMapFormat{ DXGI_FORMAT_R32G32B32A32_FLOAT },
	mBasisFormat{ DXGI_FORMAT_R32G32B32A32_FLOAT },
	mHTilde0{ nullptr},
	mHTilde0Conj{ nullptr },
	mDisplacementMap{ nullptr }
{
	BuildResource();
}

ID3D12Resource* OceanMap::Output() const
{
	return mDisplacementMap.Get();
}

void OceanMap::BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv, CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv)
{
	const auto descriptorSize = mD3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	mhCpuSrvHTilde0 = hCpuSrv;
	mhCpuUavHTilde0 = hCpuSrv.Offset(1, descriptorSize);
	mhCpuSrvHTilde0Conj = hCpuSrv.Offset(1, descriptorSize);
	mhCpuUavHTilde0Conj = hCpuSrv.Offset(1, descriptorSize);
	mhCpuSrvDisplacementMap = hCpuSrv.Offset(1, descriptorSize);
	mhCpuUavDisplacementMap = hCpuSrv.Offset(1, descriptorSize);

	mhGpuSrvHTilde0 = hGpuSrv;
	mhGpuUavHTilde0 = hGpuSrv.Offset(1, descriptorSize);
	mhGpuSrvHTilde0Conj = hGpuSrv.Offset(1, descriptorSize);
	mhGpuUavHTilde0Conj = hGpuSrv.Offset(1, descriptorSize);
	mhGpuSrvDisplacementMap = hGpuSrv.Offset(1, descriptorSize);
	mhGpuUavDisplacementMap = hGpuSrv.Offset(1, descriptorSize);

	BuildDescriptors();
}

void OceanMap::BuildOceanBasis(ID3D12GraphicsCommandList* cmdList,
	ID3D12RootSignature* rootSig,
	ID3D12PipelineState* oceanBasisPSO
	)
{
	OceanBasisConstants c = { 1.0f, DirectX::XMFLOAT2{0.5f, 0.5f}, 256, 1.0f };

	cmdList->SetComputeRootSignature(rootSig);

	const auto barrierHTilde0ToUav = CD3DX12_RESOURCE_BARRIER::Transition(
		mHTilde0.Get(),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	const auto barrierHTilde0ConjToUav = CD3DX12_RESOURCE_BARRIER::Transition(
		mHTilde0Conj.Get(),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	cmdList->ResourceBarrier(1, &barrierHTilde0ToUav);
	cmdList->ResourceBarrier(1, &barrierHTilde0ConjToUav);

	cmdList->SetPipelineState(oceanBasisPSO);

	cmdList->SetComputeRoot32BitConstants(0, 5, &c, 0);
	cmdList->SetComputeRootDescriptorTable(1, mhGpuUavHTilde0);
	cmdList->SetComputeRootDescriptorTable(2, mhGpuUavHTilde0Conj);

	const auto numGroupsX = static_cast<UINT>(ceilf(mWidth / 16.0f));
	const auto numGroupsY = static_cast<UINT>(ceilf(mHeight / 16.0f));
	cmdList->Dispatch(numGroupsX, numGroupsY, 1);

	const auto barrierHTilde0ToSrv = CD3DX12_RESOURCE_BARRIER::Transition(
		mHTilde0.Get(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_GENERIC_READ);

	const auto barrierHTilde0ConjToSrv = CD3DX12_RESOURCE_BARRIER::Transition(
		mHTilde0Conj.Get(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_GENERIC_READ);

	cmdList->ResourceBarrier(1, &barrierHTilde0ToSrv);
	cmdList->ResourceBarrier(1, &barrierHTilde0ConjToSrv);
}

UINT OceanMap::GetHTilde0SrvIndexRelative()
{
	return 0;
}

UINT OceanMap::GetHTilde0UavIndexRelative()
{
	return 1;
}

UINT OceanMap::GetHTilde0ConjSrvIndexRelative()
{
	return 2;
}

UINT OceanMap::GetHTilde0ConjUavIndexRelative()
{
	return 3;
}

UINT OceanMap::GetDisplacementMapSrvIndexRelative()
{
	return 4;
}

UINT OceanMap::GetNumBasisConstants()
{
	return 5;
}

UINT OceanMap::GetNumDescriptors()
{
	return 6;
}

void OceanMap::Execute(ID3D12GraphicsCommandList* cmdList, ID3D12RootSignature* rootSig, ID3D12PipelineState* pso)
{
}

void OceanMap::BuildDescriptors()
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = mBasisFormat;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

	uavDesc.Format = mBasisFormat;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Texture2D.MipSlice = 0;

	mD3dDevice->CreateShaderResourceView(mHTilde0.Get(), &srvDesc, mhCpuSrvHTilde0);
	mD3dDevice->CreateUnorderedAccessView(mHTilde0.Get(), nullptr, &uavDesc, mhCpuUavHTilde0);
	mD3dDevice->CreateShaderResourceView(mHTilde0Conj.Get(), &srvDesc, mhCpuSrvHTilde0Conj);
	mD3dDevice->CreateUnorderedAccessView(mHTilde0Conj.Get(), nullptr, &uavDesc, mhCpuUavHTilde0Conj);

	srvDesc.Format = mDisplacementMapFormat;
	uavDesc.Format = mDisplacementMapFormat;

	mD3dDevice->CreateShaderResourceView(mDisplacementMap.Get(), &srvDesc, mhCpuSrvDisplacementMap);
	mD3dDevice->CreateUnorderedAccessView(mDisplacementMap.Get(), nullptr, &uavDesc, mhCpuUavDisplacementMap);
}

void OceanMap::BuildResource()
{
	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = mWidth;
	texDesc.Height = mHeight;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = mDisplacementMapFormat;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	ThrowIfFailed(
		mD3dDevice->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&mDisplacementMap)
		)
	);

	texDesc.Format = mBasisFormat;
	ThrowIfFailed(
		mD3dDevice->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&mHTilde0)
		)
	);

	ThrowIfFailed(
		mD3dDevice->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&mHTilde0Conj)
		)
	);
}
