#include "SobelFilter.h"


std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers()
{
	// Applications usually only need a handful of samplers.  So just define them all up front
	// and keep them available as part of the root signature.  

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp };

}


SobelFilter::SobelFilter(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format)
	: d3dDevice(device), width(width), height(height), format(format)
{
	BuildRootSignatures();
	BuildComputeRootSignature();
	BuildCompositeRootSignature();
	BuildResources();
	BuildShaders();
	BuildPSOs();
}

ID3D12Resource* SobelFilter::Output()
{
	return sobelResult.Get();
}

void SobelFilter::BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDescriptor,
	CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuDescriptor, UINT descriptorSize)
{
	sobelInputCpuSrv = hCpuDescriptor;
	sobelResultCpuSrv = hCpuDescriptor.Offset(1, descriptorSize);
	sobelResultCpuUav = hCpuDescriptor.Offset(1, descriptorSize);

	sobelInputGpuSrv = hGpuDescriptor;
	sobelResultGpuSrv = hGpuDescriptor.Offset(1, descriptorSize);
	sobelResultGpuUav = hGpuDescriptor.Offset(1, descriptorSize);

	BuildDescriptors();
}

void SobelFilter::OnResize(UINT newWidth, UINT newHeight)
{
	if (width == newWidth && height == newHeight)
		return;

	width = newWidth;
	height = newHeight;

	BuildResources();
	BuildDescriptors();
}

void SobelFilter::Execute(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* input)
{
	cmdList->SetComputeRootSignature(computeRootSig.Get());
	cmdList->SetPipelineState(comuptePso.Get());

	const auto barrierMakeRenderTargetCopySrc = CD3DX12_RESOURCE_BARRIER::Transition(
		input,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_COPY_SOURCE
	);
	cmdList->ResourceBarrier(1, &barrierMakeRenderTargetCopySrc);

	const auto barrierMakeInputCopyDest = CD3DX12_RESOURCE_BARRIER::Transition(
		sobelInput.Get(),
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_COPY_DEST
	);
	cmdList->ResourceBarrier(1, &barrierMakeInputCopyDest);

	cmdList->CopyResource(sobelInput.Get(), input);

	const auto barrierMakeInputSrv = CD3DX12_RESOURCE_BARRIER::Transition(
		sobelInput.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_GENERIC_READ
	);
	cmdList->ResourceBarrier(1, &barrierMakeInputSrv);

	const auto barrierMakeOutputUav = CD3DX12_RESOURCE_BARRIER::Transition(
		sobelResult.Get(),
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS
	);
	
	cmdList->ResourceBarrier(1, &barrierMakeOutputUav);

	cmdList->SetComputeRootDescriptorTable(0, sobelInputGpuSrv);
	cmdList->SetComputeRootDescriptorTable(1, sobelResultGpuUav);

	UINT numGroupsX = static_cast<UINT>(ceilf(width / 16.0f));
	UINT numGroupsY = static_cast<UINT>(ceilf(height / 16.0f)); 
	cmdList->Dispatch(numGroupsX, numGroupsY, 1);
	
	const auto barrierMakeResultSrv = CD3DX12_RESOURCE_BARRIER::Transition(
		sobelResult.Get(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		D3D12_RESOURCE_STATE_GENERIC_READ
	);
	cmdList->ResourceBarrier(1, &barrierMakeResultSrv);

	cmdList->SetGraphicsRootSignature(compositeRootSig.Get());
	cmdList->SetPipelineState(compositePso.Get());
	cmdList->SetGraphicsRootDescriptorTable(0, sobelInputGpuSrv);
	cmdList->SetGraphicsRootDescriptorTable(1, sobelResultGpuSrv);

	const auto barrierCopyToRenderTarget = CD3DX12_RESOURCE_BARRIER::Transition(
		input,
		D3D12_RESOURCE_STATE_COPY_SOURCE,
		D3D12_RESOURCE_STATE_RENDER_TARGET
	);
	cmdList->ResourceBarrier(1, &barrierCopyToRenderTarget);

	DrawFullScreenQuad(cmdList);
}

const int SobelFilter::DescriptorCount()
{
	return 3;
}

void SobelFilter::BuildRootSignatures()
{
	BuildComputeRootSignature();
	BuildCompositeRootSignature();
}

void SobelFilter::BuildDescriptors()
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = format;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.MostDetailedMip = 0;

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Format = format;
	uavDesc.Texture2D.MipSlice = 0;

	d3dDevice->CreateShaderResourceView(
		sobelInput.Get(),
		&srvDesc,
		sobelInputCpuSrv);
	d3dDevice->CreateShaderResourceView(
		sobelResult.Get(), 
		&srvDesc, 
		sobelResultCpuSrv);
	d3dDevice->CreateUnorderedAccessView(
		sobelResult.Get(),
		nullptr,
		&uavDesc, sobelResultCpuUav);
}

void SobelFilter::BuildResources()
{
	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));

	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.MipLevels = 1;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
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
			IID_PPV_ARGS(&sobelInput)
		)
	);

	ThrowIfFailed(
		d3dDevice->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&sobelResult)
		)
	);
}
void SobelFilter::BuildShaders()
{
	shaderVs = DxUtil::CompileShader(
	L"13Blur/Shaders/Composite.hlsl",
		nullptr, 
		"VS",
		"vs_5_0");

	shaderPs = DxUtil::CompileShader(
	L"13Blur/Shaders/Composite.hlsl",
		nullptr, 
		"PS",
		"ps_5_0");

	shaderCs = DxUtil::CompileShader(
		L"13Blur/Shaders/Sobel.hlsl",
		nullptr,
		"SobelCS",
		"cs_5_0");

	compositeInputLayout =
	{
		{"SV_VertexID", 0, DXGI_FORMAT_R32_UINT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};
}

void SobelFilter::BuildComputeRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE srvDesc = {};
	srvDesc.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE uavDesc = {};
	uavDesc.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

	CD3DX12_ROOT_PARAMETER slotRootParameter[2];
	slotRootParameter[0].InitAsDescriptorTable(1, &srvDesc);
	slotRootParameter[1].InitAsDescriptorTable(1, &uavDesc);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
		_countof(slotRootParameter),
		slotRootParameter,
		0,
		nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

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
		IID_PPV_ARGS(computeRootSig.GetAddressOf())
	));
}

void SobelFilter::BuildCompositeRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE baseSrvDesc = {};
	baseSrvDesc.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE edgeSrvDesc = {};
	edgeSrvDesc.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

	CD3DX12_ROOT_PARAMETER slotRootParameter[2];
	slotRootParameter[0].InitAsDescriptorTable(1, &baseSrvDesc);
	slotRootParameter[1].InitAsDescriptorTable(1, &edgeSrvDesc);

	auto staticSamplers = GetStaticSamplers();

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
		_countof(slotRootParameter),
		slotRootParameter,
		staticSamplers.size(),
		staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

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
		IID_PPV_ARGS(compositeRootSig.GetAddressOf())
	));
}

void SobelFilter::BuildPSOs()
{
	BuildComputePSO();
	BuildCompositePSO();
}

void SobelFilter::BuildComputePSO()
{
	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc;
	ZeroMemory(&psoDesc, sizeof(D3D12_COMPUTE_PIPELINE_STATE_DESC));

	psoDesc.CS =
	{
		reinterpret_cast<BYTE*>(shaderCs->GetBufferPointer()),
		shaderCs->GetBufferSize()
	};
	psoDesc.pRootSignature = computeRootSig.Get();
	psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

	ThrowIfFailed(
		d3dDevice->CreateComputePipelineState(
			&psoDesc,
			IID_PPV_ARGS(comuptePso.GetAddressOf())
		)
	);
}

void SobelFilter::BuildCompositePSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

	psoDesc.InputLayout = { compositeInputLayout.data(), (UINT)compositeInputLayout.size() };
	psoDesc.pRootSignature = compositeRootSig.Get();
	psoDesc.VS =
	{
		reinterpret_cast<BYTE*>(shaderVs->GetBufferPointer()),
		shaderVs->GetBufferSize()
	};

	psoDesc.PS =
	{
		reinterpret_cast<BYTE*>(shaderPs->GetBufferPointer()),
		shaderPs->GetBufferSize()
	};
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState.DepthEnable = false;
	psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;
	psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	ThrowIfFailed(
		d3dDevice->CreateGraphicsPipelineState(
			&psoDesc,
			IID_PPV_ARGS(compositePso.GetAddressOf())
		)
	);
}

void SobelFilter::DrawFullScreenQuad(ID3D12GraphicsCommandList* cmdList)
{
	cmdList->IASetVertexBuffers(0, 1, nullptr);
	cmdList->IASetIndexBuffer(nullptr);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	cmdList->DrawInstanced(6, 1, 0, 0);
}

