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
	mDisplacementMap0{ nullptr }
{
	BuildResource();
}

ID3D12Resource* OceanMap::Output() const
{
	return mDisplacementMap0.Get();
}

void OceanMap::BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv, CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv)
{
	const auto descriptorSize = mD3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	mhCpuSrvHTilde0 = hCpuSrv;
	mhCpuUavHTilde0 = hCpuSrv.Offset(1, descriptorSize);
	mhCpuSrvHTilde0Conj = hCpuSrv.Offset(1, descriptorSize);
	mhCpuUavHTilde0Conj = hCpuSrv.Offset(1, descriptorSize);
	mhCpuSrvHTilde = hCpuSrv.Offset(1, descriptorSize);
	mhCpuUavHTilde = hCpuSrv.Offset(1, descriptorSize);
	mhCpuSrvDisplacementMap0 = hCpuSrv.Offset(1, descriptorSize);
	mhCpuUavDisplacementMap0 = hCpuSrv.Offset(1, descriptorSize);
	mhCpuSrvDisplacementMap1 = hCpuSrv.Offset(1, descriptorSize);
	mhCpuUavDisplacementMap1 = hCpuSrv.Offset(1, descriptorSize);

	mhGpuSrvHTilde0 = hGpuSrv;
	mhGpuUavHTilde0 = hGpuSrv.Offset(1, descriptorSize);
	mhGpuSrvHTilde0Conj = hGpuSrv.Offset(1, descriptorSize);
	mhGpuUavHTilde0Conj = hGpuSrv.Offset(1, descriptorSize);
	mhGpuSrvHTilde = hGpuSrv.Offset(1, descriptorSize);
	mhGpuUavHTilde = hGpuSrv.Offset(1, descriptorSize);
	mhGpuSrvDisplacementMap0 = hGpuSrv.Offset(1, descriptorSize);
	mhGpuUavDisplacementMap0 = hGpuSrv.Offset(1, descriptorSize);
	mhGpuSrvDisplacementMap1 = hGpuSrv.Offset(1, descriptorSize);
	mhGpuUavDisplacementMap1 = hGpuSrv.Offset(1, descriptorSize);

	BuildDescriptors();
}

void OceanMap::BuildOceanBasis(ID3D12GraphicsCommandList* cmdList,
	ID3D12RootSignature* rootSig,
	ID3D12PipelineState* oceanBasisPSO
	)
{
	OceanBasisConstants c = { 100.0f, DirectX::XMFLOAT2{1.0f, 0.5f}, 256, 2.0f };

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
	constexpr UINT numGroupsZ = NUM_OCEAN_BASIS; // x, y, z
	cmdList->Dispatch(numGroupsX, numGroupsY, numGroupsZ);

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

void OceanMap::ComputeOceanFrequency(ID3D12GraphicsCommandList* cmdList,
	ID3D12RootSignature* rootSig,
	ID3D12PipelineState* oceanFrequencyPSO,
	float waveTime
)
{
	FrequencyConstants c = { 256u, 1.0f, waveTime };

	cmdList->SetComputeRootSignature(rootSig);

	const auto barrierHTildeToUav = CD3DX12_RESOURCE_BARRIER::Transition(
		mHTilde.Get(),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	
	cmdList->ResourceBarrier(1, &barrierHTildeToUav);

	cmdList->SetPipelineState(oceanFrequencyPSO);

	cmdList->SetComputeRoot32BitConstants(0, 3, &c, 0);
	cmdList->SetComputeRootDescriptorTable(1, mhGpuSrvHTilde0);
	cmdList->SetComputeRootDescriptorTable(2, mhGpuSrvHTilde0Conj);
	cmdList->SetComputeRootDescriptorTable(3, mhGpuUavHTilde);

	const auto numGroupsX = static_cast<UINT>(ceilf(mWidth / 256.0f));
	const auto numGroupsY = mHeight;
	const UINT numGroupsZ = NUM_OCEAN_FREQUENCY;
	cmdList->Dispatch(numGroupsX, numGroupsY, numGroupsZ);

	const auto barrierHTildeToSrv = CD3DX12_RESOURCE_BARRIER::Transition(
		mHTilde.Get(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_GENERIC_READ);
	cmdList->ResourceBarrier(1, &barrierHTildeToSrv);
}

void OceanMap::ComputeOceanDisplacement(ID3D12GraphicsCommandList* cmdList,
                                        ID3D12RootSignature* rootSig,
                                        ID3D12PipelineState* shiftCsPso,
                                        ID3D12PipelineState* bitReversalCsPso,
                                        ID3D12PipelineState* fft1dCsPso,
                                        ID3D12PipelineState* transposeCsPso
)
{
	FftConstants c = { mWidth, 1 };

	cmdList->SetComputeRootSignature(rootSig);

	{
		const auto barrierHTilde0ToCopySrc = CD3DX12_RESOURCE_BARRIER::Transition(
			mHTilde.Get(),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			D3D12_RESOURCE_STATE_COPY_SOURCE);

		const auto barrierDisplacementMap0ToCopyDest = CD3DX12_RESOURCE_BARRIER::Transition(
			mDisplacementMap0.Get(),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			D3D12_RESOURCE_STATE_COPY_DEST);

		cmdList->ResourceBarrier(1, &barrierHTilde0ToCopySrc);
		cmdList->ResourceBarrier(1, &barrierDisplacementMap0ToCopyDest);
		cmdList->CopyResource(mDisplacementMap0.Get(), mHTilde.Get());
	}

	{
		const auto barrierHTilde0ToSrv = CD3DX12_RESOURCE_BARRIER::Transition(
			mHTilde.Get(),
			D3D12_RESOURCE_STATE_COPY_SOURCE,
			D3D12_RESOURCE_STATE_GENERIC_READ);

		const auto barrierDisplacementMap0ToUav = CD3DX12_RESOURCE_BARRIER::Transition(
			mDisplacementMap0.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		const auto barrierDisplacementMap1ToUav = CD3DX12_RESOURCE_BARRIER::Transition(
			mDisplacementMap1.Get(),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		cmdList->ResourceBarrier(1, &barrierHTilde0ToSrv);
		cmdList->ResourceBarrier(1, &barrierDisplacementMap0ToUav);
		cmdList->ResourceBarrier(1, &barrierDisplacementMap1ToUav);
	}

	cmdList->SetComputeRoot32BitConstants(0, 2, &c, 0);
	cmdList->SetComputeRootDescriptorTable(1, mhGpuUavDisplacementMap0);
	cmdList->SetComputeRootDescriptorTable(2, mhGpuUavDisplacementMap1);

	Shift(cmdList, shiftCsPso, c);
	BitReversal(cmdList, bitReversalCsPso, c);
	Fft1d(cmdList, fft1dCsPso, c);
	Transpose(cmdList, transposeCsPso, c);

	// copy gInput to gOutput because BitReversal uses gOutput
	// but transposed matrix are gInput.
	{
		const auto barrierDisplacementMap0ToSrc = CD3DX12_RESOURCE_BARRIER::Transition(
			mDisplacementMap0.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_COPY_SOURCE);

		const auto barrierDisplacementMap1ToDest = CD3DX12_RESOURCE_BARRIER::Transition(
			mDisplacementMap1.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_COPY_DEST);
		
		cmdList->ResourceBarrier(1, &barrierDisplacementMap0ToSrc);
		cmdList->ResourceBarrier(1, &barrierDisplacementMap1ToDest);

		cmdList->CopyResource(mDisplacementMap1.Get(), mDisplacementMap0.Get());

		const auto barrierDisplacementMap0ToUav = CD3DX12_RESOURCE_BARRIER::Transition(
			mDisplacementMap0.Get(),
			D3D12_RESOURCE_STATE_COPY_SOURCE,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		const auto barrierDisplacementMap1ToUav = CD3DX12_RESOURCE_BARRIER::Transition(
			mDisplacementMap1.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		cmdList->ResourceBarrier(1, &barrierDisplacementMap0ToUav);
		cmdList->ResourceBarrier(1, &barrierDisplacementMap1ToUav);
	}

	BitReversal(cmdList, bitReversalCsPso, c);
	Fft1d(cmdList, fft1dCsPso, c);
	Transpose(cmdList, transposeCsPso, c);
	
	{
		const auto barrierDisplacementMap0ToSrv = CD3DX12_RESOURCE_BARRIER::Transition(
			mDisplacementMap0.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_GENERIC_READ);

		const auto barrierDisplacementMap1ToSrv = CD3DX12_RESOURCE_BARRIER::Transition(
			mDisplacementMap1.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_GENERIC_READ);
		
		cmdList->ResourceBarrier(1, &barrierDisplacementMap0ToSrv);
		cmdList->ResourceBarrier(1, &barrierDisplacementMap1ToSrv);
	}
	
}

void OceanMap::Dispatch(ID3D12GraphicsCommandList* cmdList)
{
	const auto numGroupsX = static_cast<UINT>(ceilf(static_cast<float>(mWidth) / 256.0f));
	const auto numGroupsY = mHeight;
	const auto numGroupsZ = NUM_OCEAN_FREQUENCY;
	cmdList->Dispatch(numGroupsX, numGroupsY, numGroupsZ);
}

void OceanMap::Shift(ID3D12GraphicsCommandList* cmdList, ID3D12PipelineState* shiftCsPso, FftConstants& c)
{
	cmdList->SetPipelineState(shiftCsPso);
	Dispatch(cmdList);
}

void OceanMap::BitReversal(ID3D12GraphicsCommandList* cmdList, ID3D12PipelineState* bitReversalCsPso, FftConstants& c)
{
	cmdList->SetPipelineState(bitReversalCsPso);
	Dispatch(cmdList);
}

void OceanMap::Fft1d(ID3D12GraphicsCommandList* cmdList, ID3D12PipelineState* fft1dCsPso, FftConstants& c)
{
	cmdList->SetPipelineState(fft1dCsPso);
	Dispatch(cmdList);
}

void OceanMap::Transpose(ID3D12GraphicsCommandList* cmdList, ID3D12PipelineState* transposeCsPso, FftConstants& c)
{
	cmdList->SetPipelineState(transposeCsPso);
	Dispatch(cmdList);
}

CD3DX12_CPU_DESCRIPTOR_HANDLE OceanMap::GetCpuHTilde0Srv()
{
	return mhCpuSrvHTilde0;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE OceanMap::GetCpuHTilde0Uav()
{
	return mhCpuUavHTilde0;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE OceanMap::GetGpuHTilde0Srv()
{
	return mhGpuSrvHTilde0;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE OceanMap::GetGpuHTilde0Uav()
{
	return mhGpuUavHTilde0;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE OceanMap::GetCpuHTilde0ConjSrv()
{
	return mhCpuSrvHTilde0Conj;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE OceanMap::GetCpuHTilde0ConjUav()
{
	return mhCpuUavHTilde0Conj;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE OceanMap::GetGpuHTilde0ConjSrv()
{
	return mhGpuSrvHTilde0Conj;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE OceanMap::GetGpuHTilde0ConjUav()
{
	return mhGpuUavHTilde0Conj;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE OceanMap::GetCpuHTildeSrv()
{
	return mhCpuSrvHTilde;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE OceanMap::GetCpuHTildeUav()
{
	return mhCpuUavHTilde;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE OceanMap::GetGpuHTildeSrv()
{
	return mhGpuSrvHTilde;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE OceanMap::GetGpuHTildeUav()
{
	return mhGpuUavHTilde;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE OceanMap::GetCpuDisplacementMapSrv()
{
	return mhCpuSrvDisplacementMap1;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE OceanMap::GetCpuDisplacementMapUav()
{
	return mhCpuUavDisplacementMap1;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE OceanMap::GetGpuDisplacementMapSrv()
{
	return mhGpuSrvDisplacementMap1;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE OceanMap::GetGpuDisplacementMapUav()
{
	return mhGpuUavDisplacementMap1;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE OceanMap::GetCpuDescriptorEnd()
{
	auto end = mhCpuUavDisplacementMap1;
	end.Offset(1, mD3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

	return end;
}

UINT OceanMap::GetNumBasisConstants()
{
	return 5;
}

UINT OceanMap::GetNumFrequencyConstants()
{
	return 3;
}

UINT OceanMap::GetNumFftConstants()
{
	return 2;
}

UINT OceanMap::GetNumDescriptors()
{
	return 10;
}

void OceanMap::Execute(ID3D12GraphicsCommandList* cmdList, ID3D12RootSignature* rootSig, ID3D12PipelineState* pso)
{
}

void OceanMap::BuildDescriptors()
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = mBasisFormat;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
	srvDesc.Texture3D.MipLevels = 1;
	srvDesc.Texture3D.MostDetailedMip = 0;
	srvDesc.Texture3D.ResourceMinLODClamp = 0.0f;

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

	uavDesc.Format = mBasisFormat;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
	uavDesc.Texture3D.FirstWSlice = 0;
	uavDesc.Texture3D.MipSlice = 0;
	uavDesc.Texture3D.WSize = NUM_OCEAN_BASIS; // x, y, z

	mD3dDevice->CreateShaderResourceView(mHTilde0.Get(), &srvDesc, mhCpuSrvHTilde0);
	mD3dDevice->CreateUnorderedAccessView(mHTilde0.Get(), nullptr, &uavDesc, mhCpuUavHTilde0);
	mD3dDevice->CreateShaderResourceView(mHTilde0Conj.Get(), &srvDesc, mhCpuSrvHTilde0Conj);
	mD3dDevice->CreateUnorderedAccessView(mHTilde0Conj.Get(), nullptr, &uavDesc, mhCpuUavHTilde0Conj);

	uavDesc.Texture3D.WSize = NUM_OCEAN_FREQUENCY;
	mD3dDevice->CreateShaderResourceView(mHTilde.Get(), &srvDesc, mhCpuSrvHTilde);
	mD3dDevice->CreateUnorderedAccessView(mHTilde.Get(), nullptr, &uavDesc, mhCpuUavHTilde);

	srvDesc.Format = mDisplacementMapFormat;
	uavDesc.Format = mDisplacementMapFormat;

	mD3dDevice->CreateShaderResourceView(mDisplacementMap0.Get(), &srvDesc, mhCpuSrvDisplacementMap0);
	mD3dDevice->CreateUnorderedAccessView(mDisplacementMap0.Get(), nullptr, &uavDesc, mhCpuUavDisplacementMap0);
	mD3dDevice->CreateShaderResourceView(mDisplacementMap1.Get(), &srvDesc, mhCpuSrvDisplacementMap1);
	mD3dDevice->CreateUnorderedAccessView(mDisplacementMap1.Get(), nullptr, &uavDesc, mhCpuUavDisplacementMap1);
}

void OceanMap::BuildResource()
{
	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
	texDesc.Alignment = 0;
	texDesc.Width = mWidth;
	texDesc.Height = mHeight;
	texDesc.DepthOrArraySize = NUM_OCEAN_FREQUENCY;
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
			IID_PPV_ARGS(&mDisplacementMap0)
		)
	);

	ThrowIfFailed(
		mD3dDevice->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&mDisplacementMap1)
		)
	);

	ThrowIfFailed(
		mD3dDevice->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&mHTilde)
		)
	);

	texDesc.DepthOrArraySize = NUM_OCEAN_BASIS;

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
