#include "BlurApp.h"

#include <random>

const int gNumFrameResources = 3;

BlurApp::BlurApp(HINSTANCE hInstance)
	: MainWindow(hInstance)
{
}

BlurApp::~BlurApp()
{
	if (device->GetD3DDevice() != nullptr)
		device->FlushCommandQueue();
}

bool BlurApp::Initialize()
{
	if (!MainWindow::Initialize())
		return false;

	//device->Enable4xMsaa();

	auto commandList = device->GetCommandList();
	auto commandListAllocator = device->GetCommandListAllocator();
	auto commandQueue = device->GetCommandQueue();

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(commandList->Reset(commandListAllocator.Get(), nullptr));

	// Get the increment size of a descriptor in this heap type.  This is hardware specific, so we have
	// to query this information.
	cbvSrvUavDescriptorSize = device->GetD3DDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	waves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);

	blurFilter = std::make_unique<BlurFilter>(
		device->GetD3DDevice().Get(),
		device->GetClientWidth(),
		device->GetClientHeight(),
		DXGI_FORMAT_R8G8B8A8_UNORM);

	sobelFilter = std::make_unique<SobelFilter>(
		device->GetD3DDevice().Get(),
		device->GetClientWidth(),
		device->GetClientHeight(),
		DXGI_FORMAT_R8G8B8A8_UNORM);

	LoadTextures();
	BuildRootSignature();
	BuildPostProcessRootSignature();
	BuildDescriptorHeaps();
	BuildShadersAndInputLayout();
	BuildLandGeometry();
	BuildWavesGeometryBuffers();
	BuildCrate();
	BuildTree();
	BuildMaterials();


	BuildRenderItems();
	BuildFrameResources();
	BuildPSOs();

	// Execute the initialization commands.
	ThrowIfFailed(commandList->Close());
	ID3D12CommandList* cmdsLists[] = { commandList.Get() };
	commandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until initialization is complete.
	device->FlushCommandQueue();

	return true;
}

void BlurApp::OnResize()
{
	MainWindow::OnResize();

	// The window resized, so update the aspect ratio and recompute the projection matrix.
	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&proj, P);

	if(blurFilter)
	{
		blurFilter->OnResize(device->GetClientWidth(), device->GetClientHeight());
	}

	if (blurFilter)
	{
		sobelFilter->OnResize(device->GetClientWidth(), device->GetClientHeight());
	}
}

void BlurApp::AnimateMaterials(const GameTimer& gt)
{
	auto waterMat = materials["water"].get();

	float& tu = waterMat->MatTransform(3, 0);
	float& tv = waterMat->MatTransform(3, 1);

	tu += 0.1f * gt.DeltaTime();
	tv += 0.02f * gt.DeltaTime();

	if (tu >= 1.0f)
	{
		tu -= 1.0f;
	}

	if (tv >= 1.0f)
	{
		tv -= 1.0f;
	}

	waterMat->MatTransform(3, 0) = tu;
	waterMat->MatTransform(3, 1) = tv;

	waterMat->NumFramesDirty = gNumFrameResources;
	
}

void BlurApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);
	UpdateCamera(gt);

	// Cycle through the circular frame resource array.
	currFrameResourceIndex = (currFrameResourceIndex + 1) % gNumFrameResources;
	currFrameResource = frameResources[currFrameResourceIndex].get();

	auto fence = device->GetFence();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (currFrameResource->Fence != 0 && fence->GetCompletedValue() < currFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(fence->SetEventOnCompletion(currFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
	AnimateMaterials(gt);
	UpdateWaves(gt);
}

void BlurApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = currFrameResource->CmdListAlloc;

	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(cmdListAlloc->Reset());

	auto commandList = device->GetCommandList();
	auto commandListAllocator = device->GetCommandListAllocator();
	auto commandQueue = device->GetCommandQueue();

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	ThrowIfFailed(commandList->Reset(cmdListAlloc.Get(), PSOs["opaque"].Get()));

	commandList->RSSetViewports(1, &device->GetScreenViewport());
	commandList->RSSetScissorRects(1, &device->GetScissorRect());

	auto currentBackBuffer = device->CurrentBackBuffer();
	auto currentBackBufferView = device->CurrentBackBufferView();
	auto depthStencilView = device->DepthStencilView();

	auto barrierReset = CD3DX12_RESOURCE_BARRIER::Transition(
		currentBackBuffer,
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET);

	// Indicate a state transition on the resource usage.
	commandList->ResourceBarrier(1, &barrierReset);

	// Clear the back buffer and depth buffer.
	commandList->ClearRenderTargetView(currentBackBufferView, DirectX::Colors::LightSteelBlue, 0, nullptr);
	commandList->ClearDepthStencilView(depthStencilView, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	commandList->OMSetRenderTargets(1, &currentBackBufferView, true, &depthStencilView);;

	ID3D12DescriptorHeap* descriptorHeaps[] = { cbvSrvUavDescriptorHeap.Get() };
	commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	commandList->SetGraphicsRootSignature(rootSignature.Get());;

	auto passCB = currFrameResource->PassCB->Resource();
	commandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

	commandList->SetPipelineState(PSOs["opaque"].Get());
	DrawRenderItems(commandList.Get(), RitemLayer[static_cast<int>(RenderLayer::Opaque)]);

	commandList->SetPipelineState(PSOs["tree"].Get());
	DrawRenderItems(commandList.Get(), RitemLayer[static_cast<int>(RenderLayer::Tree)]);

	commandList->SetPipelineState(PSOs["transparent"].Get());
	DrawRenderItems(commandList.Get(), RitemLayer[static_cast<int>(RenderLayer::Transparent)]);

	sobelFilter->Execute(
		commandList.Get(),
		device->CurrentBackBuffer());
	
	auto barrierPresent = CD3DX12_RESOURCE_BARRIER::Transition(
		currentBackBuffer,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT
	);

	// Indicate a state transition on the resource usage.
	commandList->ResourceBarrier(1, &barrierPresent);

	// Done recording commands.
	ThrowIfFailed(commandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { commandList.Get() };
	commandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	device->SwapBuffers();

	// Advance the fence value to mark commands up to this fence point.
	currFrameResource->Fence = device->IncreaseFence();

	// Add an instruction to the command queue to set a new fence point. 
	// Because we are on the GPU timeline, the new fence point won't be 
	// set until the GPU finishes processing all the commands prior to this Signal().
	commandQueue->Signal(device->GetFence().Get(), device->GetCurrentFence());
}

void BlurApp::OnMouseLeftDown(int x, int y, short keyState)
{
	lastMousePos.x = x;
	lastMousePos.y = y;

	SetCapture(m_hwnd);
}

void BlurApp::OnMouseLeftUp(int x, int y, short keyState)
{
	ReleaseCapture();
}

void BlurApp::OnMouseMove(int x, int y, short keyState)
{
	if ((keyState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = DirectX::XMConvertToRadians(0.25f * static_cast<float>(x - lastMousePos.x));
		float dy = DirectX::XMConvertToRadians(0.25f * static_cast<float>(y - lastMousePos.y));

		// Update angles based on input to orbit camera around box.
		theta += dx;
		phi += dy;

		// Restrict the angle phi.
		phi = MathHelper::Clamp(phi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((keyState & MK_RBUTTON) != 0)
	{
		// Make each pixel correspond to 0.2 unit in the scene.
		float dx = 0.05f * static_cast<float>(x - lastMousePos.x);
		float dy = 0.05f * static_cast<float>(y - lastMousePos.y);

		// Update the camera radius based on input.
		radius += dx - dy;

		// Restrict the radius.
		radius = MathHelper::Clamp(radius, 5.0f, 150.0f);
	}

	lastMousePos.x = x;
	lastMousePos.y = y;
}

void BlurApp::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();

	if (GetAsyncKeyState(VK_LEFT) & 0x8000)
		sunTheta -= 1.0f * dt;

	if (GetAsyncKeyState(VK_RIGHT) & 0x8000)
		sunTheta += 1.0f * dt;

	if (GetAsyncKeyState(VK_UP) & 0x8000)
		sunPhi -= 1.0f * dt;

	if (GetAsyncKeyState(VK_DOWN) & 0x8000)
		sunPhi += 1.0f * dt;

	sunPhi = MathHelper::Clamp(sunPhi, 0.1f, XM_PIDIV2);
}

void BlurApp::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	eyePos.x = radius * sinf(phi) * cosf(theta);
	eyePos.z = radius * sinf(phi) * sinf(theta);
	eyePos.y = radius * cosf(phi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(eyePos.x, eyePos.y, eyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&this->view, view);
}

void BlurApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = currFrameResource->ObjectCB.get();
	for (auto& e : allRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.Textransform, XMMatrixTranspose(texTransform));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void BlurApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = currFrameResource->MaterialCB.get();
	for (auto& e : materials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if (mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void BlurApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&this->view);
	XMMATRIX proj = XMLoadFloat4x4(&this->proj);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	auto viewDeterminant = XMMatrixDeterminant(view);
	XMMATRIX invView = XMMatrixInverse(&viewDeterminant, view);
	auto projDeterminant = XMMatrixDeterminant(proj);
	XMMATRIX invProj = XMMatrixInverse(&projDeterminant, proj);
	auto viewProjDeterminant = XMMatrixDeterminant(viewProj);
	XMMATRIX invViewProj = XMMatrixInverse(&viewProjDeterminant, viewProj);

	XMStoreFloat4x4(&mainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mainPassCB.EyePosW = eyePos;
	auto clientWidth = device->GetClientWidth();
	auto clientHeight = device->GetClientHeight();

	mainPassCB.RenderTargetSize = XMFLOAT2((float)clientWidth, (float)clientHeight);
	mainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / clientWidth, 1.0f / clientHeight);
	mainPassCB.NearZ = 1.0f;
	mainPassCB.FarZ = 1000.0f;
	mainPassCB.TotalTime = gt.TotalTime();
	mainPassCB.DeltaTime = gt.DeltaTime();
	mainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };

	XMVECTOR lightDir = -MathHelper::SphericalToCartesian(1.0f, sunTheta, sunPhi);

	XMStoreFloat3(&mainPassCB.Lights[0].Direction, lightDir);
	mainPassCB.Lights[0].Strength = { 1.0f,1.0f, 0.9f };

	mainPassCB.FogColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	mainPassCB.FogStart = { 5.0f };
	mainPassCB.FogRange = { 400.0f };

	auto currPassCB = currFrameResource->PassCB.get();
	currPassCB->CopyData(0, mainPassCB);
}

void BlurApp::UpdateWaves(const GameTimer& gt)
{
	// Every quarter second, generate a random wave.
	static float t_base = 0.0f;
	if ((timer.TotalTime() - t_base) >= 0.25f)
	{
		t_base += 0.25f;

		int i = MathHelper::Rand(4, waves->RowCount() - 5);
		int j = MathHelper::Rand(4, waves->ColumnCount() - 5);

		float r = MathHelper::RandF(0.2f, 0.5f);

		waves->Disturb(i, j, r);
	}

	// Update the wave simulation.
	waves->Update(gt.DeltaTime());

	// Update the wave vertex buffer with the new solution.
	auto currWavesVB = currFrameResource->WavesVB.get();
	for (int i = 0; i < waves->VertexCount(); ++i)
	{
		Vertex v;

		v.Pos = waves->Position(i);
		v.Normal = waves->Normal(i);

		// Derive tex-coords from position by 
		// mapping [-w/2,w/2] --> [0,1]
		v.TexC.x = 0.5f + v.Pos.x / waves->Width();
		v.TexC.y = 0.5f - v.Pos.z / waves->Depth();

		currWavesVB->CopyData(i, v);
	}

	// Set the dynamic VB of the wave renderitem to the current frame VB.
	wavesRitem->Geo->VertexBufferGPU = currWavesVB->Resource();
}

void BlurApp::LoadTexture(std::wstring filePath, std::string textureName)
{
	auto texture = std::make_unique<Texture>();
	texture->Name = textureName;
	texture->Filename = filePath;
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(device->GetD3DDevice().Get(),
		device->GetCommandList().Get(), texture->Filename.c_str(),
		texture->Resource, texture->UploadHeap));

	textures[texture->Name] = std::move(texture);
}

void BlurApp::LoadTextureArray(std::wstring filePath, std::string textureName)
{
	auto texture = std::make_unique<Texture>();
	texture->Name = textureName;
	texture->Filename = filePath;
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(device->GetD3DDevice().Get(),
		device->GetCommandList().Get(), texture->Filename.c_str(),
		texture->Resource, texture->UploadHeap));

	textureArrays[texture->Name] = std::move(texture);
}

void BlurApp::LoadTextures()
{
	LoadTexture(L"Textures/WoodCrate01.dds", "crateTex");
	LoadTexture(L"Textures/water1.dds", "waterTex");
	LoadTexture(L"Textures/grass.dds", "grassTex");
	LoadTexture(L"Textures/WireFence.dds", "wireFenceTex");
	LoadTextureArray(L"Textures/treearray.dds", "treeTex");
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> BlurApp::GetStaticSamplers()
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

void BlurApp::BuildRootSignature()
{
	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	CD3DX12_DESCRIPTOR_RANGE srv;
	srv.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	slotRootParameter[0].InitAsDescriptorTable(1, &srv);

	// Create root CBV.
	slotRootParameter[1].InitAsConstantBufferView(0);
	slotRootParameter[2].InitAsConstantBufferView(1);
	slotRootParameter[3].InitAsConstantBufferView(2);

	auto staticSamplers = GetStaticSamplers();

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
		_countof(slotRootParameter), 
		slotRootParameter, 
		staticSamplers.size(),
		staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(device->GetD3DDevice()->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(rootSignature.GetAddressOf())));
}

void BlurApp::BuildDescriptorHeaps()
{
	const int blurDescriptorCount = blurFilter->DescriptorCount();
	const int sobelDescriptorCount = sobelFilter->DescriptorCount();

	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors =
		textures.size() + textureArrays.size() + blurDescriptorCount + sobelDescriptorCount;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	ThrowIfFailed(device->GetD3DDevice()->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&cbvSrvUavDescriptorHeap)));

	int descriptorIndex = 0;
	for (auto& each : textures)
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(cbvSrvUavDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
		hDescriptor.Offset(descriptorIndex, device->GetCbvSrvUavDescriptorSize());

		auto texture = each.second.get();
		auto textureResource = texture->Resource;
		each.second->SrvIndex = descriptorIndex;

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = textureResource->GetDesc().Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = textureResource->GetDesc().MipLevels;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		device->GetD3DDevice()->CreateShaderResourceView(
			textureResource.Get(), &srvDesc, hDescriptor);

		descriptorIndex++;
	}

	for (auto& each : textureArrays)
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(cbvSrvUavDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
		hDescriptor.Offset(descriptorIndex, device->GetCbvSrvUavDescriptorSize());

		auto texture = each.second.get();
		auto textureResource = texture->Resource;
		each.second->SrvIndex = descriptorIndex;

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = textureResource->GetDesc().Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		srvDesc.Texture2DArray.ArraySize = textureResource->GetDesc().DepthOrArraySize;
		srvDesc.Texture2DArray.MostDetailedMip = 0;
		srvDesc.Texture2DArray.MipLevels = textureResource->GetDesc().MipLevels;
		srvDesc.Texture2DArray.ResourceMinLODClamp = 0.0f;
		srvDesc.Texture2DArray.FirstArraySlice = 0;

		device->GetD3DDevice()->CreateShaderResourceView(
			textureResource.Get(), &srvDesc, hDescriptor);

		descriptorIndex++;
	}

	blurFilter->BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE(
			cbvSrvUavDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			descriptorIndex,
			cbvSrvUavDescriptorSize),
		CD3DX12_GPU_DESCRIPTOR_HANDLE(
			cbvSrvUavDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
			descriptorIndex,
			cbvSrvUavDescriptorSize),
		cbvSrvUavDescriptorSize
	);

	descriptorIndex += 4;

	sobelFilter->BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE(
			cbvSrvUavDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			descriptorIndex,
			cbvSrvUavDescriptorSize),
		CD3DX12_GPU_DESCRIPTOR_HANDLE(
			cbvSrvUavDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
			descriptorIndex,
			cbvSrvUavDescriptorSize),
		cbvSrvUavDescriptorSize
	);

}

void BlurApp::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO defines[] =
	{
		"FOG", "1",
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	shaders["standardVS"] = DxUtil::CompileShader(L"13Blur\\Shaders\\Default.hlsl", nullptr, "VS", "vs_5_0");
	shaders["opaquePS"] = DxUtil::CompileShader(L"13Blur\\Shaders\\Default.hlsl", defines, "PS", "ps_5_0");
	shaders["treeVS"] = DxUtil::CompileShader(L"13Blur\\Shaders\\TreeSprite.hlsl", defines, "VS", "vs_5_0");
	shaders["treeGS"] = DxUtil::CompileShader(L"13Blur\\Shaders\\TreeSprite.hlsl", defines, "GS", "gs_5_0");
	shaders["treePS"] = DxUtil::CompileShader(L"13Blur\\Shaders\\TreeSprite.hlsl", defines, "PS", "ps_5_0");
	shaders["horzBlurCS"] = DxUtil::CompileShader(L"13Blur\\Shaders\\Blur.hlsl", nullptr, "HorzBlurCS", "cs_5_0");
	shaders["vertBlurCS"] = DxUtil::CompileShader(L"13Blur\\Shaders\\Blur.hlsl", nullptr, "VertBlurCS", "cs_5_0");

	defaultInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	treeInputLayout =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{"SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void BlurApp::BuildLandGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(160.0f, 160.0f, 50, 50);

	//
	// Extract the vertex elements we are interested and apply the height function to
	// each vertex.  In addition, color the vertices based on their height so we have
	// sandy looking beaches, grassy low hills, and snow mountain peaks.
	//

	std::vector<Vertex> vertices(grid.Vertices.size());
	for (size_t i = 0; i < grid.Vertices.size(); ++i)
	{
		auto& p = grid.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Pos.y = GetHillsHeight(p.x, p.z);
		vertices[i].Normal = GetHillsNormal(p.x, p.z);
		vertices[i].TexC = grid.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = grid.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "landGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	auto commandList = device->GetCommandList();

	geo->VertexBufferGPU = DxUtil::CreateDefaultBuffer(device->GetD3DDevice().Get(),
		commandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = DxUtil::CreateDefaultBuffer(device->GetD3DDevice().Get(),
		commandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	geometries["landGeo"] = std::move(geo);
}

void BlurApp::BuildCrate()
{
	GeometryGenerator geoGen;

	constexpr float BOX_SIDE_LENGTH{ 1.0f };
	constexpr int NUM_DEVISIONS{ 4 };

	auto box = geoGen.CreateBox(BOX_SIDE_LENGTH, 
		BOX_SIDE_LENGTH, 
		BOX_SIDE_LENGTH, 
		NUM_DEVISIONS);

	std::vector<Vertex> vertices(box.Vertices.size());
	auto indices = box.GetIndices16();

	for(int i = 0; i < vertices.size(); ++i)
	{
		vertices[i].Pos = box.Vertices[i].Position;
		vertices[i].Normal = box.Vertices[i].Normal;
		vertices[i].TexC = box.Vertices[i].TexC;
	}

	auto crateGeometry = std::make_unique<MeshGeometry>();
	auto vbByteSize = sizeof(Vertex) * vertices.size();
	auto ibByteSize = sizeof(std::uint16_t) * indices.size();

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &crateGeometry->VertexBufferCPU));
	ThrowIfFailed(D3DCreateBlob(ibByteSize, &crateGeometry->IndexBufferCPU));

	CopyMemory(crateGeometry->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
	CopyMemory(crateGeometry->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	auto commandList = device->GetCommandList();

	crateGeometry->VertexBufferGPU = DxUtil::CreateDefaultBuffer(device->GetD3DDevice().Get(),
		commandList.Get(), vertices.data(), vbByteSize, crateGeometry->VertexBufferUploader);

	crateGeometry->IndexBufferGPU = DxUtil::CreateDefaultBuffer(device->GetD3DDevice().Get(),
		commandList.Get(), indices.data(), ibByteSize, crateGeometry->IndexBufferUploader);
	
	crateGeometry->VertexByteStride = sizeof(Vertex);
	crateGeometry->VertexBufferByteSize = vbByteSize;
	crateGeometry->IndexFormat = DXGI_FORMAT_R16_UINT;
	crateGeometry->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	crateGeometry->DrawArgs["crate"] = submesh;

	geometries["crateGeo"] = std::move(crateGeometry);
}

void BlurApp::BuildTree()
{
	GeometryGenerator geoGen;
	auto points = 
		geoGen.CreateUniformRandomPoints(
			-45.0f, 45.0f, 0.f, 0.f, -45.0f, 45.0f, 10
		);

	std::vector<TreeVertex> vertices(points.Vertices.size());
	auto indices = points.GetIndices16();

	for (int i = 0; i < vertices.size(); ++i)
	{
		vertices[i].Pos = points.Vertices[i].Position;
		vertices[i].Size = DirectX::XMFLOAT2 { 10.0f, 30.0f };
	}

	auto treeGeometry = std::make_unique<MeshGeometry>();
	auto vbByteSize = sizeof(TreeVertex) * vertices.size();
	auto ibByteSize = sizeof(std::uint16_t) * indices.size();

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &treeGeometry->VertexBufferCPU));
	ThrowIfFailed(D3DCreateBlob(ibByteSize, &treeGeometry->IndexBufferCPU));

	CopyMemory(treeGeometry->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
	CopyMemory(treeGeometry->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	auto commandList = device->GetCommandList();

	treeGeometry->VertexBufferGPU = DxUtil::CreateDefaultBuffer(device->GetD3DDevice().Get(),
		commandList.Get(), vertices.data(), vbByteSize, treeGeometry->VertexBufferUploader);

	treeGeometry->IndexBufferGPU = DxUtil::CreateDefaultBuffer(device->GetD3DDevice().Get(),
		commandList.Get(), indices.data(), ibByteSize, treeGeometry->IndexBufferUploader);

	treeGeometry->VertexByteStride = sizeof(TreeVertex);
	treeGeometry->VertexBufferByteSize = vbByteSize;
	treeGeometry->IndexFormat = DXGI_FORMAT_R16_UINT;
	treeGeometry->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	treeGeometry->DrawArgs["tree"] = submesh;

	geometries["treeGeo"] = std::move(treeGeometry);
}

void BlurApp::BuildWavesGeometryBuffers()
{
	std::vector<std::uint16_t> indices(3 * waves->TriangleCount()); // 3 indices per face
	assert(waves->VertexCount() < 0x0000ffff);

	// Iterate over each quad.
	int m = waves->RowCount();
	int n = waves->ColumnCount();
	int k = 0;
	for (int i = 0; i < m - 1; ++i)
	{
		for (int j = 0; j < n - 1; ++j)
		{
			indices[k] = i * n + j;
			indices[k + 1] = i * n + j + 1;
			indices[k + 2] = (i + 1) * n + j;

			indices[k + 3] = (i + 1) * n + j;
			indices[k + 4] = i * n + j + 1;
			indices[k + 5] = (i + 1) * n + j + 1;

			k += 6; // next quad
		}
	}

	UINT vbByteSize = waves->VertexCount() * sizeof(Vertex);
	UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "waterGeo";

	// Set dynamically.
	geo->VertexBufferCPU = nullptr;
	geo->VertexBufferGPU = nullptr;

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	auto commandList = device->GetCommandList();

	geo->IndexBufferGPU = DxUtil::CreateDefaultBuffer(device->GetD3DDevice().Get(),
		commandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	geometries["waterGeo"] = std::move(geo);
}

void BlurApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { defaultInputLayout.data(), (UINT)defaultInputLayout.size() };
	opaquePsoDesc.pRootSignature = rootSignature.Get();
	opaquePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(shaders["standardVS"]->GetBufferPointer()),
		shaders["standardVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(shaders["opaquePS"]->GetBufferPointer()),
		shaders["opaquePS"]->GetBufferSize()
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.BlendState.AlphaToCoverageEnable = true;
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = device->GetBackBufferFormat();
	opaquePsoDesc.SampleDesc.Count = device->GetMsaaState() ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = device->GetMsaaState() ? (device->GetMsaaQuality() - 1) : 0;
	opaquePsoDesc.DSVFormat = device->GetDepthStencilFormat();
	
	ThrowIfFailed(device->GetD3DDevice()->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&PSOs["opaque"])));

	auto transparentPsoDesc = opaquePsoDesc;

	D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
	transparencyBlendDesc.BlendEnable = true;
	transparencyBlendDesc.LogicOpEnable = false;
	transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	transparencyBlendDesc.DestBlend = D3D12_BLEND_DEST_ALPHA;
	transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
	transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
	transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
	ThrowIfFailed(
		device->GetD3DDevice()->CreateGraphicsPipelineState(
			&transparentPsoDesc, IID_PPV_ARGS(&PSOs["transparent"])
		)
	);

	auto treePsoDesc = opaquePsoDesc;

	treePsoDesc.InputLayout = 
	{
		treeInputLayout.data(),
		static_cast<UINT>(treeInputLayout.size())
	};
	treePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(shaders["treeVS"]->GetBufferPointer()),
		shaders["treeVS"]->GetBufferSize()
	};
	treePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(shaders["treePS"]->GetBufferPointer()),
		shaders["treePS"]->GetBufferSize()
	};
	treePsoDesc.GS =
	{
		reinterpret_cast<BYTE*>(shaders["treeGS"]->GetBufferPointer()),
		shaders["treeGS"]->GetBufferSize()
	};
	
	treePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	treePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	
	ThrowIfFailed(
		device->GetD3DDevice()->CreateGraphicsPipelineState(
			&treePsoDesc, IID_PPV_ARGS(&PSOs["tree"])
		)
	);

	D3D12_COMPUTE_PIPELINE_STATE_DESC horzBlurPSO = {};
	horzBlurPSO.pRootSignature = postProcessRootSignature.Get();
	horzBlurPSO.CS =
	{
		reinterpret_cast<BYTE*>(shaders["horzBlurCS"]->GetBufferPointer()),
		shaders["horzBlurCS"]->GetBufferSize()
	};
	horzBlurPSO.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	ThrowIfFailed(
		device->GetD3DDevice()->CreateComputePipelineState(
			&horzBlurPSO, IID_PPV_ARGS(&PSOs["horzBlur"])
		)
	);

	D3D12_COMPUTE_PIPELINE_STATE_DESC vertBlurPSO = {};
	vertBlurPSO.pRootSignature = postProcessRootSignature.Get();
	vertBlurPSO.CS =
	{
		reinterpret_cast<BYTE*>(shaders["vertBlurCS"]->GetBufferPointer()),
		shaders["vertBlurCS"]->GetBufferSize()
	};
	vertBlurPSO.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	ThrowIfFailed(
		device->GetD3DDevice()->CreateComputePipelineState(
			&vertBlurPSO, IID_PPV_ARGS(&PSOs["vertBlur"])
		)
	);
}

void BlurApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		frameResources.push_back(std::make_unique<FrameResource>(device->GetD3DDevice().Get(),
			1, static_cast<UINT>(allRitems.size()), static_cast<UINT>(materials.size()), waves->VertexCount()));
	}
}

void BlurApp::BuildMaterials()
{
	auto crate = std::make_unique<Material>();
	crate->Name = "crate";
	crate->MatCBIndex = 0;
	crate->DiffuseSrvHeapIndex = textures["crateTex"]->SrvIndex;
	crate->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	crate->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	crate->Roughness = 0.0f;

	auto grass = std::make_unique<Material>();
	grass->Name = "grass";
	grass->MatCBIndex = 1;
	grass->DiffuseSrvHeapIndex = textures["grassTex"]->SrvIndex;
	grass->DiffuseAlbedo = XMFLOAT4(0.2f, 0.6f, 0.2f, 1.0f);
	grass->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	grass->Roughness = 0.125f;
	
	auto water = std::make_unique<Material>();
	water->Name = "water";
	water->MatCBIndex = 2;
	water->DiffuseSrvHeapIndex = textures["waterTex"]->SrvIndex;
	water->DiffuseAlbedo = XMFLOAT4(0.0f, 0.2f, 0.5f, 0.8f);
	water->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	water->Roughness = 0.0f;

	auto wireFenceBox = std::make_unique<Material>();
	wireFenceBox->Name = "wireFence";
	wireFenceBox->MatCBIndex = 3;
	wireFenceBox->DiffuseSrvHeapIndex = textures["wireFenceTex"]->SrvIndex;
	wireFenceBox->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	wireFenceBox->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.12);
	wireFenceBox->Roughness = 0.0f;

	auto treeSprite = std::make_unique<Material>();
	treeSprite->Name = "tree";
	treeSprite->MatCBIndex = 3;
	treeSprite->DiffuseSrvHeapIndex = textureArrays["treeTex"]->SrvIndex;
	treeSprite->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	treeSprite->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1);
	treeSprite->Roughness = 0.1f;

	materials["crate"] = std::move(crate);
	materials["grass"] = std::move(grass);
	materials["water"] = std::move(water);
	materials["wireFence"] = std::move(wireFenceBox);
	materials["tree"] = std::move(treeSprite);
}

void BlurApp::BuildPostProcessRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE srvTable;
	srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE uavTable;
	uavTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

	CD3DX12_ROOT_PARAMETER slotRootParameter[3];

	slotRootParameter[0].InitAsConstants(12, 0);
	slotRootParameter[1].InitAsDescriptorTable(1, &srvTable);
	slotRootParameter[2].InitAsDescriptorTable(1, &uavTable);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
		_countof(slotRootParameter),
		slotRootParameter,
		0, 
		nullptr,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc,
		D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(),
		errorBlob.GetAddressOf());

	if(errorBlob != nullptr)
	{
		::OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(device->GetD3DDevice()->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(postProcessRootSignature.GetAddressOf())
	));
}

void BlurApp::BuildRenderItems()
{
	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	gridRitem->ObjCBIndex = 1;
	gridRitem->Mat = materials["grass"].get();
	gridRitem->Geo = geometries["landGeo"].get();
	gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));

	RitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());

	auto crateRitem = std::make_unique<RenderItem>();
	auto translated = XMMatrixTranslation(-1.0f, 0.0f, 1.0f);
	auto scaled = translated * XMMatrixScaling(7.0f, 7.0f, 7.0f);

	XMStoreFloat4x4(&crateRitem->World, scaled);
	crateRitem->ObjCBIndex = 2;
	crateRitem->Mat = materials["wireFence"].get();
	crateRitem->Geo = geometries["crateGeo"].get();
	crateRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	crateRitem->IndexCount = crateRitem->Geo->DrawArgs["crate"].IndexCount;
	crateRitem->StartIndexLocation = crateRitem->Geo->DrawArgs["crate"].StartIndexLocation;
	crateRitem->BaseVertexLocation = crateRitem->Geo->DrawArgs["crate"].BaseVertexLocation;
	XMStoreFloat4x4(&crateRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));

	RitemLayer[(int)RenderLayer::Opaque].push_back(crateRitem.get());

	allRitems.push_back(std::move(gridRitem));
	allRitems.push_back(std::move(crateRitem));
	
	auto treeRitem = std::make_unique<RenderItem>();
	treeRitem->World = MathHelper::Identity4x4();
	treeRitem->ObjCBIndex = 3;
	treeRitem->Mat = materials["tree"].get();
	treeRitem->Geo = geometries["treeGeo"].get();
	treeRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
	treeRitem->IndexCount = treeRitem->Geo->DrawArgs["tree"].IndexCount;
	treeRitem->StartIndexLocation = treeRitem->Geo->DrawArgs["tree"].StartIndexLocation;
	treeRitem->BaseVertexLocation = treeRitem->Geo->DrawArgs["tree"].BaseVertexLocation;
	XMStoreFloat4x4(&treeRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));

	RitemLayer[static_cast<int>(RenderLayer::Tree)].push_back(treeRitem.get());
	allRitems.push_back(std::move(treeRitem));

	auto wavesRitem = std::make_unique<RenderItem>();
	wavesRitem->World = MathHelper::Identity4x4();
	wavesRitem->ObjCBIndex = 0;
	wavesRitem->Mat = materials["water"].get();
	wavesRitem->Geo = geometries["waterGeo"].get();
	wavesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wavesRitem->IndexCount = wavesRitem->Geo->DrawArgs["grid"].IndexCount;
	wavesRitem->StartIndexLocation = wavesRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	wavesRitem->BaseVertexLocation = wavesRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	this->wavesRitem = wavesRitem.get();

	RitemLayer[(int)RenderLayer::Transparent].push_back(wavesRitem.get());

	allRitems.push_back(std::move(wavesRitem));
}

void BlurApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = DxUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = DxUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = currFrameResource->ObjectCB->Resource();
	auto matCB = currFrameResource->MaterialCB->Resource();

	// For each render item...
	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];

		auto vertexBufferView = ri->Geo->VertexBufferView();
		auto indexBufferView = ri->Geo->IndexBufferView();

		cmdList->IASetVertexBuffers(0, 1, &vertexBufferView);
		cmdList->IASetIndexBuffer(&indexBufferView);
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(cbvSrvUavDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		tex.Offset(ri->Mat->DiffuseSrvHeapIndex, device->GetCbvSrvUavDescriptorSize());

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

		cmdList->SetGraphicsRootDescriptorTable(0, tex);
		cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

float BlurApp::GetHillsHeight(float x, float z)const
{
	return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
}

XMFLOAT3 BlurApp::GetHillsNormal(float x, float z)const
{
	// n = (-df/dx, 1, -df/dz)
	XMFLOAT3 n(
		-0.03f * z * cosf(0.1f * x) - 0.3f * cosf(0.1f * z),
		1.0f,
		-0.3f * sinf(0.1f * x) + 0.03f * x * sinf(0.1f * z));

	XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
	XMStoreFloat3(&n, unitNormal);

	return n;
}
