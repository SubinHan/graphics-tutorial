#include "BoxApp.h"

using namespace DirectX;

#include <iostream>

BoxApp::BoxApp(HINSTANCE hInstance) : MainWindow(hInstance)
{
}

BoxApp::~BoxApp()
{
}

bool BoxApp::Initialize()
{
	if(!MainWindow::Initialize())
		return false;

	auto commandList = device->GetCommandList();
	auto commandListAlloc = device->GetCommandListAllocator();
	commandList->Reset(commandListAlloc.Get(), nullptr);

	BuildDescriptorHeaps();
	BuildConstantBuffers();
	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildBoxGeometry();
	BuildPSO();

	ThrowIfFailed(commandList->Close());

	ID3D12CommandList* cmdsLists[] = { commandList.Get() };
	auto commandQueue = device->GetCommandQueue();
	commandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	device->FlushCommandQueue();

	return true;
}

void BoxApp::OnResize()
{
	MainWindow::OnResize();

	XMMATRIX p = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&matrixProjection, p);
}

void BoxApp::Update(const GameTimer& gt)
{
	float x = radius * sinf(phi) * cosf(theta);
	float z = radius * sinf(phi) * sinf(theta);
	float y = radius * cosf(phi);

	XMVECTOR pos = XMVectorSet(x, y, z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&matrixView, view);

	XMMATRIX world = XMLoadFloat4x4(&matrixWorld);
	XMMATRIX proj = XMLoadFloat4x4(&matrixProjection);
	XMMATRIX worldViewProj = world * view * proj;

	ObjectConstants objConstants;
	XMStoreFloat4x4(&objConstants.WorldViewProj, XMMatrixTranspose(worldViewProj));
	objConstants.gTime = gt.TotalTime();

	objectConstantBuffer->CopyData(0, objConstants);
}

void BoxApp::Draw(const GameTimer& gt)
{
	auto commandList = device->GetCommandList();
	auto commandListAlloc = device->GetCommandListAllocator();
	auto currentBackBuffer = device->CurrentBackBuffer();
	auto currentBackBufferView = device->CurrentBackBufferView();
	auto depthStencilView = device->DepthStencilView();

	ThrowIfFailed(commandListAlloc->Reset());
	
	ThrowIfFailed(commandList->Reset(commandListAlloc.Get(), pso.Get()));

	device->RSSetViewports(1);
	device->RSSetScissorRects(1);

	auto barrierReset = CD3DX12_RESOURCE_BARRIER::Transition(
		currentBackBuffer,
		D3D12_RESOURCE_STATE_PRESENT, 
		D3D12_RESOURCE_STATE_RENDER_TARGET);

	commandList->ResourceBarrier(1, &barrierReset);


	commandList->ClearRenderTargetView(currentBackBufferView,
		Colors::LightSteelBlue, 0, nullptr);
	commandList->ClearDepthStencilView(depthStencilView,
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		1.0f, 0, 0, nullptr);

	commandList->OMSetRenderTargets(1, &currentBackBufferView,
		true, &depthStencilView);

	ID3D12DescriptorHeap* descriptorHeaps[] = { cbvHeap.Get() };
	commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);
	
	commandList->SetGraphicsRootSignature(rootSignature.Get());

	auto vertexBuffers = boxGeometry->VertexBufferView();
	auto indexBuffer = boxGeometry->IndexBufferView();

	commandList->IASetVertexBuffers(0, 1, &vertexBuffers);
	commandList->IASetIndexBuffer(&indexBuffer);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	commandList->SetGraphicsRootDescriptorTable(
		0, cbvHeap->GetGPUDescriptorHandleForHeapStart());

	commandList->DrawIndexedInstanced(
		boxGeometry->DrawArgs["box"].IndexCount,
		1, 0, 0, 0);

	auto barrierDraw = CD3DX12_RESOURCE_BARRIER::Transition(
		currentBackBuffer,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT
	);

	commandList->ResourceBarrier(1, &barrierDraw);

	ThrowIfFailed(commandList->Close());

	ID3D12CommandList* cmdsLists[] = { commandList.Get() };
	device->GetCommandQueue()->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	device->SwapBuffers();

	device->FlushCommandQueue();
}

void BoxApp::BuildDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.NumDescriptors = 1;
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;
	ThrowIfFailed(device->GetD3DDevice()->CreateDescriptorHeap(&cbvHeapDesc,
		IID_PPV_ARGS(&cbvHeap)));
}

void BoxApp::BuildConstantBuffers()
{
	objectConstantBuffer = std::make_unique<UploadBuffer<ObjectConstants>>(
		device->GetD3DDevice().Get(), 1, true);

	UINT objectConstantBufferByteSize = 
		DxUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	D3D12_GPU_VIRTUAL_ADDRESS constantBufferAddress =
		objectConstantBuffer->Resource()->GetGPUVirtualAddress();

	int boxConstantBufferIndex = 0;
	constantBufferAddress += boxConstantBufferIndex * objectConstantBufferByteSize;

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
	cbvDesc.BufferLocation = constantBufferAddress;
	cbvDesc.SizeInBytes = objectConstantBufferByteSize;

	device->GetD3DDevice()->CreateConstantBufferView(
		&cbvDesc,
		cbvHeap->GetCPUDescriptorHandleForHeapStart());
}

void BoxApp::BuildRootSignature()
{
	CD3DX12_ROOT_PARAMETER slotRootParameter[1];

	CD3DX12_DESCRIPTOR_RANGE cbvTable;
	cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
	slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
		1,
		slotRootParameter,
		0,
		nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(
		&rootSigDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(),
		errorBlob.GetAddressOf()
	);

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(device->GetD3DDevice()->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&rootSignature))
	);
}

void BoxApp::BuildShadersAndInputLayout()
{
	HRESULT hr = S_OK;

	mvsByteCode = DxUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "VS", "vs_5_0");
	mpsByteCode = DxUtil::CompileShader(L"Shaders\\color.hlsl", nullptr, "PS", "ps_5_0");

	inputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
		D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
		D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};
}

void BoxApp::BuildBoxGeometry()
{
	std::array<Vertex, 8> vertices =
	{
		Vertex({XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::White)}),
		Vertex({XMFLOAT3(-1.0f, +1.0f, -1.0f), XMFLOAT4(Colors::Black)}),
		Vertex({XMFLOAT3(+1.0f, +1.0f, -1.0f), XMFLOAT4(Colors::Red)}),
		Vertex({XMFLOAT3(+1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::Green)}),
		Vertex({XMFLOAT3(-1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Blue)}),
		Vertex({XMFLOAT3(-1.0f, +1.0f, +1.0f), XMFLOAT4(Colors::Yellow)}),
		Vertex({XMFLOAT3(+1.0f, +1.0f, +1.0f), XMFLOAT4(Colors::Cyan)}),
		Vertex({XMFLOAT3(+1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Magenta)})
	};

	std::array<std::uint16_t, 36> indices =
	{
		0, 1, 2,
		0, 2, 3,

		4, 6, 5,
		4, 7, 6,

		4, 5, 1,
		4, 1, 0,

		3, 2, 6,
		3, 6, 7,
		
		1, 5, 6,
		1, 6, 2,

		4, 0, 3,
		4, 3, 7
	};

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	boxGeometry = std::make_unique<MeshGeometry>();
	boxGeometry->Name = "boxGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &boxGeometry->VertexBufferCPU));
	CopyMemory(boxGeometry->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &boxGeometry->IndexBufferCPU));
	CopyMemory(boxGeometry->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	boxGeometry->VertexBufferGPU = DxUtil::CreateDefaultBuffer(
		device->GetD3DDevice().Get(),
		device->GetCommandList().Get(),
		vertices.data(),
		vbByteSize,
		boxGeometry->VertexBufferUploader
	);

	boxGeometry->IndexBufferGPU = DxUtil::CreateDefaultBuffer(
		device->GetD3DDevice().Get(),
		device->GetCommandList().Get(),
		indices.data(),
		ibByteSize,
		boxGeometry->IndexBufferUploader
	);

	boxGeometry->VertexByteStride = sizeof(Vertex);
	boxGeometry->VertexBufferByteSize = vbByteSize;
	boxGeometry->IndexFormat = DXGI_FORMAT_R16_UINT;
	boxGeometry->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	boxGeometry->DrawArgs["box"] = submesh;
}

void BoxApp::BuildPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	psoDesc.InputLayout = { inputLayout.data(), (UINT)inputLayout.size() };
	psoDesc.pRootSignature = rootSignature.Get();
	psoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mvsByteCode->GetBufferPointer()),
		mvsByteCode->GetBufferSize()
	};
	psoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mpsByteCode->GetBufferPointer()),
		mpsByteCode->GetBufferSize()
	};
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = device->GetBackBufferFormat();
	bool msaaState = device->GetMsaaState();
	psoDesc.SampleDesc.Count = msaaState ? 4 : 1;
	psoDesc.SampleDesc.Quality = msaaState ? device->GetMsaaQuality() - 1 : 0;
	psoDesc.DSVFormat = device->GetDepthStencilFormat();
	ThrowIfFailed(device->GetD3DDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)));
}

void BoxApp::LeftDown(int x, int y, short keyState)
{
	lastMousePos.x = x;
	lastMousePos.y = y;

	SetCapture(m_hwnd);
}

void BoxApp::LeftUp(int x, int y, short keyState)
{
	ReleaseCapture();
}

void BoxApp::MouseMove(int x, int y, short keyState)
{
	if ((keyState & MK_LBUTTON) != 0)
	{
		float dx = XMConvertToRadians(0.25 * static_cast<float>(x - lastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - lastMousePos.y));

		theta += dx;
		phi += dy;

		phi = MathHelper::Clamp(phi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if((keyState & MK_RBUTTON) != 0)
	{
		float dx = 0.005f * static_cast<float>(x - lastMousePos.x);
		float dy = 0.005f * static_cast<float>(y - lastMousePos.y);

		radius += dx - dy;

		radius = MathHelper::Clamp(radius, 3.0f, 15.0f);
	}

	lastMousePos.x = x;
	lastMousePos.y = y;
}
