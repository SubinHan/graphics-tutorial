//***************************************************************************************
// Waves.cpp by Frank Luna (C) 2011 All Rights Reserved.
//***************************************************************************************

#include "Waves.h"
#include <ppl.h>
#include <algorithm>
#include <vector>
#include <cassert>

using namespace DirectX;

Waves::Waves(ID3D12Device* device, int m, int n, float dx, float dt, float speed, float damping)
{
	d3dDevice = device;

	mNumRows = m;
	mNumCols = n;

	mVertexCount = m * n;
	mTriangleCount = (m - 1) * (n - 1) * 2;

	mTimeStep = dt;
	mSpatialStep = dx;

	float d = damping * dt + 2.0f;
	float e = (speed * speed) * (dt * dt) / (dx * dx);
	mK1 = (damping * dt - 2.0f) / d;
	mK2 = (4.0f - 8.0f * e) / d;
	mK3 = (2.0f * e) / d;

	mPrevSolution.resize(m * n);
	mCurrSolution.resize(m * n);
	mNormals.resize(m * n);
	mTangentX.resize(m * n);

	// Generate grid vertices in system memory.

	float halfWidth = (n - 1) * dx * 0.5f;
	float halfDepth = (m - 1) * dx * 0.5f;
	for (int i = 0; i < m; ++i)
	{
		float z = halfDepth - i * dx;
		for (int j = 0; j < n; ++j)
		{
			float x = -halfWidth + j * dx;

			mPrevSolution[i * n + j] = XMFLOAT3(x, 0.0f, z);
			mCurrSolution[i * n + j] = XMFLOAT3(x, 0.0f, z);
			mNormals[i * n + j] = XMFLOAT3(0.0f, 1.0f, 0.0f);
			mTangentX[i * n + j] = XMFLOAT3(1.0f, 0.0f, 0.0f);
		}
	}

	BuildRootSignature();
	BuildShaders();
	BuildResources();
	BuildPSOs();
}

Waves::~Waves()
{
}

int Waves::RowCount()const
{
	return mNumRows;
}

int Waves::ColumnCount()const
{
	return mNumCols;
}

int Waves::VertexCount()const
{
	return mVertexCount;
}

int Waves::TriangleCount()const
{
	return mTriangleCount;
}

float Waves::Width()const
{
	return mNumCols * mSpatialStep;
}

float Waves::Depth()const
{
	return mNumRows * mSpatialStep;
}

float Waves::GetSpatialStep()
{
	return mSpatialStep;
}

ID3D12Resource* Waves::GetSolutionTexture()
{
	return currSolution.Get();
}

D3D12_GPU_DESCRIPTOR_HANDLE Waves::GetSolutionSrvHandleGpu()
{
	return currSolGpuSrv;
}

D3D12_CPU_DESCRIPTOR_HANDLE Waves::GetSolutionSrvHandleCpu()
{
	return currSolCpuSrv;
}

void Waves::Disturb(int i, int j, float magnitude)
{
	// Don't disturb boundaries.
	assert(i > 1 && i < mNumRows - 2);
	assert(j > 1 && j < mNumCols - 2);

	float halfMag = 0.5f * magnitude;

	// Disturb the ijth vertex height and its neighbors.
	mCurrSolution[i * mNumCols + j].y += magnitude;
	mCurrSolution[i * mNumCols + j + 1].y += halfMag;
	mCurrSolution[i * mNumCols + j - 1].y += halfMag;
	mCurrSolution[(i + 1) * mNumCols + j].y += halfMag;
	mCurrSolution[(i - 1) * mNumCols + j].y += halfMag;
}

void Waves::Execute(ID3D12GraphicsCommandList* cmdList,
	const GameTimer& gt)
{
	cmdList->SetComputeRootSignature(rootSignature.Get());

	cmdList->SetComputeRoot32BitConstants(0, 1, &mK1, 0);
	cmdList->SetComputeRoot32BitConstants(0, 1, &mK2, 1);
	cmdList->SetComputeRoot32BitConstants(0, 1, &mK3, 2);


	// Every quarter second, generate a random wave.
	static float t_base = 0.0f;
	if ((gt.TotalTime() - t_base) >= 0.05f)
	{
		t_base += 0.05f;

		int i = MathHelper::Rand(4, mNumRows - 5);
		int j = MathHelper::Rand(4, mNumCols - 5);

		float r = MathHelper::RandF(0.2f, 0.5f);

		cmdList->SetComputeRoot32BitConstants(0, 1, &r, 3);
		cmdList->SetComputeRoot32BitConstants(0, 1, &i, 4);
		cmdList->SetComputeRoot32BitConstants(0, 1, &j, 5);
	}
	
	const auto barrierMakePrevUav = CD3DX12_RESOURCE_BARRIER::Transition(
		prevSolution.Get(),
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS
	);
	cmdList->ResourceBarrier(1, &barrierMakePrevUav);

	const auto barrierMakeCurrUav = CD3DX12_RESOURCE_BARRIER::Transition(
		currSolution.Get(),
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS
	);
	cmdList->ResourceBarrier(1, &barrierMakeCurrUav);

	cmdList->SetPipelineState(pso.Get());

	cmdList->SetComputeRootDescriptorTable(1, prevSolGpuUav);
	cmdList->SetComputeRootDescriptorTable(2, currSolGpuUav);

	UINT numGroupsX = (UINT)ceilf(mNumCols / 16.0f);
	UINT numGroupsY = (UINT)ceilf(mNumRows / 16.0f);
	cmdList->Dispatch(numGroupsX, numGroupsY, 1);

	const auto barrierMakePrevSrv = CD3DX12_RESOURCE_BARRIER::Transition(
		prevSolution.Get(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_COMMON
	);
	cmdList->ResourceBarrier(1, &barrierMakePrevSrv);

	const auto barrierMakeCurrSrv = CD3DX12_RESOURCE_BARRIER::Transition(
		currSolution.Get(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_COMMON
	);
	cmdList->ResourceBarrier(1, &barrierMakeCurrSrv);
}

void Waves::BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDescriptor, CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuDescriptor,
                             UINT descriptorSize)
{
	prevSolCpuSrv = hCpuDescriptor;
	prevSolCpuUav = hCpuDescriptor.Offset(1, descriptorSize);
	currSolCpuSrv = hCpuDescriptor.Offset(1, descriptorSize);
	currSolCpuUav = hCpuDescriptor.Offset(1, descriptorSize);

	prevSolGpuSrv = hGpuDescriptor;
	prevSolGpuUav = hGpuDescriptor.Offset(1, descriptorSize);
	currSolGpuSrv = hGpuDescriptor.Offset(1, descriptorSize);
	currSolGpuUav = hGpuDescriptor.Offset(1, descriptorSize);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.MostDetailedMip = 0;

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Texture2D.MipSlice = 0;

	d3dDevice->CreateShaderResourceView(
		prevSolution.Get(),
		&srvDesc,
		prevSolCpuSrv
	);

	d3dDevice->CreateUnorderedAccessView(
		prevSolution.Get(),
		nullptr,
		&uavDesc,
		prevSolCpuUav
	);

	d3dDevice->CreateShaderResourceView(
		currSolution.Get(),
		&srvDesc,
		currSolCpuSrv
	);

	d3dDevice->CreateUnorderedAccessView(
		currSolution.Get(),
		nullptr,
		&uavDesc,
		currSolCpuUav
	);
}

void Waves::BuildResources()
{
	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = mNumCols;
	texDesc.Height = mNumRows;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = format;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	ThrowIfFailed(
		d3dDevice->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&prevSolution)
		)
	)

	ThrowIfFailed(
		d3dDevice->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&currSolution)
		)
	)
}

void Waves::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE prevUavTable;
	prevUavTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE currUavTable;
	currUavTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);

	CD3DX12_ROOT_PARAMETER slotRootParameter[3];
	slotRootParameter[0].InitAsConstants(6, 0);
	slotRootParameter[1].InitAsDescriptorTable(1, &prevUavTable);
	slotRootParameter[2].InitAsDescriptorTable(1, &currUavTable);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
		_countof(slotRootParameter),
		slotRootParameter,
		0,
		nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	);

	Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc,
		D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(),
		errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(d3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(rootSignature.GetAddressOf())
	));
}

void Waves::BuildShaders()
{
	const D3D_SHADER_MACRO wavesDefines[] =
	{
		"DISPLACEMENT_MAP", "1",
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	shaderVS = DxUtil::CompileShader(L"13Blur\\Shaders\\Default.hlsl", wavesDefines, "VS", "vs_5_0");
	shaderCS = DxUtil::CompileShader(L"13Blur\\Shaders\\Waves.hlsl", nullptr, "CS", "cs_5_0");
}

void Waves::BuildPSOs()
{
	D3D12_COMPUTE_PIPELINE_STATE_DESC wavesPSO = {};
	wavesPSO.pRootSignature = rootSignature.Get();
	wavesPSO.CS =
	{
		reinterpret_cast<BYTE*>(shaderCS->GetBufferPointer()),
		shaderCS->GetBufferSize()
	};
	wavesPSO.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	ThrowIfFailed(
		d3dDevice->CreateComputePipelineState(
			&wavesPSO, IID_PPV_ARGS(&pso)
		)
	);
}

