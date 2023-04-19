#include "StencilApp.h"

const int gNumFrameResources = 3;

StencilApp::StencilApp(HINSTANCE hInstance)
	: MainWindow(hInstance)
{
}

StencilApp::~StencilApp()
{
	if (device->GetD3DDevice() != nullptr)
		device->FlushCommandQueue();
}

bool StencilApp::Initialize()
{
	if (!MainWindow::Initialize())
		return false;

	auto commandList = device->GetCommandList();
	auto commandListAllocator = device->GetCommandListAllocator();
	auto commandQueue = device->GetCommandQueue();

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(commandList->Reset(commandListAllocator.Get(), nullptr));

	// Get the increment size of a descriptor in this heap type.  This is hardware specific, so we have
	// to query this information.
	cbvSrvDescriptorSize = device->GetD3DDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	waves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);
	LoadTextures();
	BuildRootSignature();
	BuildDescriptorHeaps();
	BuildShaderResourceViews();
	BuildShadersAndInputLayout();
	BuildLandGeometry();
	BuildWavesGeometryBuffers();
	BuildCrateGeometry();
	BuildMirrorGeometry();
	BuildEndOfWorldGeometry();
	BuildReflectedItems();
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

void StencilApp::OnResize()
{
	MainWindow::OnResize();

	// The window resized, so update the aspect ratio and recompute the projection matrix.
	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&proj, P);
}

void StencilApp::AnimateMaterials(const GameTimer& gt)
{
	auto waterMat = materials["water"].get();

	float& tu = waterMat->MatTransform1(3, 0);
	float& tv = waterMat->MatTransform1(3, 1);

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

	waterMat->MatTransform1(3, 0) = tu;
	waterMat->MatTransform1(3, 1) = tv;

	waterMat->NumFramesDirty = gNumFrameResources;
	
}

void StencilApp::Update(const GameTimer& gt)
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

	UpdateReflectedItem(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
	UpdateReflectedPassCB(gt);
	AnimateMaterials(gt);
	UpdateWaves(gt);
}

void StencilApp::Draw(const GameTimer& gt)
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

	ID3D12DescriptorHeap* descriptorHeaps[] = { srvHeap.Get() };
	commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	commandList->SetGraphicsRootSignature(rootSignature.Get());;

	auto passCB = currFrameResource->PassCB->Resource();
	commandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());
	
	//commandList->SetPipelineState(PSOs["markDepthMirrors"].Get());
	//DrawRenderItems(commandList.Get(), RitemLayer[static_cast<int>(RenderLayer::Mirrors)]);

	commandList->OMSetStencilRef(0);
	commandList->SetPipelineState(PSOs["opaque"].Get());
	DrawRenderItems(commandList.Get(), RitemLayer[static_cast<int>(RenderLayer::OpaqueFrustumCull)]);
	
	//commandList->OMSetStencilRef(0);
	//commandList->SetPipelineState(PSOs["shadow"].Get());
	//DrawRenderItems(commandList.Get(), RitemLayer[static_cast<int>(RenderLayer::Shadow)]);

	commandList->OMSetStencilRef(1);
	commandList->SetPipelineState(PSOs["markStencilMirrors"].Get());
	DrawRenderItems(commandList.Get(), RitemLayer[static_cast<int>(RenderLayer::Mirrors)]);

	commandList->SetPipelineState(PSOs["alwaysRenderInStencil"].Get());
	DrawRenderItems(commandList.Get(), RitemLayer[static_cast<int>(RenderLayer::Clear)]);

	commandList->OMSetStencilRef(1);
	const auto passCBByteSize = DxUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	commandList->SetGraphicsRootConstantBufferView(
		2, passCB->GetGPUVirtualAddress() + 1 * passCBByteSize);
	commandList->SetPipelineState(PSOs["drawStencilReflections"].Get());
	DrawRenderItems(commandList.Get(), RitemLayer[static_cast<int>(RenderLayer::Reflected)]);

	commandList->SetGraphicsRootConstantBufferView(
		2, passCB->GetGPUVirtualAddress()
	);
	commandList->OMSetStencilRef(0);

	commandList->SetPipelineState(PSOs["transparent"].Get());
	DrawRenderItems(commandList.Get(), RitemLayer[static_cast<int>(RenderLayer::OpaqueNonFrustumCull)]);
	

	auto barrierDraw = CD3DX12_RESOURCE_BARRIER::Transition(
		currentBackBuffer,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT
	);

	// Indicate a state transition on the resource usage.
	commandList->ResourceBarrier(1, &barrierDraw);

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

void StencilApp::OnMouseLeftDown(int x, int y, short keyState)
{
	lastMousePos.x = x;
	lastMousePos.y = y;

	SetCapture(m_hwnd);
}

void StencilApp::OnMouseLeftUp(int x, int y, short keyState)
{
	ReleaseCapture();
}

void StencilApp::OnMouseMove(int x, int y, short keyState)
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

void StencilApp::UpdateReflectedItem(const GameTimer& gt)
{
	// Update reflection world matrix.
	XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // xy plane
	XMMATRIX R = XMMatrixReflect(mirrorPlane);
	//XMStoreFloat4x4(&mReflectedSkullRitem->World, skullWorld * R);

	for (auto& each : RitemLayer[static_cast<int>(RenderLayer::Reflected)])
	{
		auto reflectedRenderItem = static_cast<ReflectedRenderItem*>(each);

		XMStoreFloat4x4(&reflectedRenderItem->World,
		                XMLoadFloat4x4(&reflectedRenderItem->OriginalRenderItem->World) * R);

		each->NumFramesDirty = gNumFrameResources;
	}
}

void StencilApp::OnKeyboardInput(const GameTimer& gt)
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


	//// Update shadow world matrix.
	//XMVECTOR shadowPlane = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f); // xz plane
	//XMVECTOR toMainLight = -XMLoadFloat3(&mainPassCB.Lights[0].Direction);
	//XMMATRIX S = XMMatrixShadow(shadowPlane, toMainLight);
	//XMMATRIX shadowOffsetY = XMMatrixTranslation(0.0f, 0.001f, 0.0f);
	//XMStoreFloat4x4(&mShadowedSkullRitem->World, skullWorld * S * shadowOffsetY);

	//mSkullRitem->NumFramesDirty = gNumFrameResources;
	//mReflectedSkullRitem->NumFramesDirty = gNumFrameResources;
	//mShadowedSkullRitem->NumFramesDirty = gNumFrameResources;
}

void StencilApp::UpdateCamera(const GameTimer& gt)
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

void StencilApp::UpdateObjectCBs(const GameTimer& gt)
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

void StencilApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = currFrameResource->MaterialCB.get();
	for (auto& e : materials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if (mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform1);

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

void StencilApp::UpdateMainPassCB(const GameTimer& gt)
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

	// Three-point lighting
	XMVECTOR backLightDir = -MathHelper::SphericalToCartesian(1.0f, sunTheta + XM_PIDIV2, sunPhi);
	XMVECTOR fillLightDir = -MathHelper::SphericalToCartesian(1.0f, sunTheta - XM_PIDIV2, sunPhi);

	XMStoreFloat3(&mainPassCB.Lights[1].Direction, backLightDir);
	XMStoreFloat3(&mainPassCB.Lights[2].Direction, fillLightDir);
	mainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
	mainPassCB.Lights[2].Strength = { 0.5f, 0.5f, 0.5f };

	mainPassCB.FogColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	mainPassCB.FogStart = { 5.0f };
	mainPassCB.FogRange = { 400.0f };

	auto currPassCB = currFrameResource->PassCB.get();
	currPassCB->CopyData(0, mainPassCB);
}

void StencilApp::UpdateReflectedPassCB(const GameTimer& gt)
{
	reflectedPassCB = mainPassCB;

	XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);

	XMMATRIX R = XMMatrixReflect(mirrorPlane);

	for(int i = 0; i < 3; ++i)
	{
		XMVECTOR lightDir = XMLoadFloat3(&mainPassCB.Lights[i].Direction);
		XMVECTOR reflectedLightDir = XMVector3TransformNormal(lightDir, R);
		XMStoreFloat3(&reflectedPassCB.Lights[i].Direction, reflectedLightDir);
	}

	auto currPassCB = currFrameResource->PassCB.get();
	currPassCB->CopyData(1, reflectedPassCB);
}


void StencilApp::UpdateWaves(const GameTimer& gt)
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

void StencilApp::LoadTexture(std::wstring filePath, std::string textureName)
{
	auto texture = std::make_unique<Texture>();
	texture->Name = textureName;
	texture->Filename = filePath;
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(device->GetD3DDevice().Get(),
		device->GetCommandList().Get(), texture->Filename.c_str(),
		texture->Resource, texture->UploadHeap));

	textures[texture->Name] = std::move(texture);
}

void StencilApp::LoadTextures()
{
	LoadTexture(L"Textures/WoodCrate01.dds", "crateTex");
	LoadTexture(L"Textures/water1.dds", "waterTex");
	LoadTexture(L"Textures/grass.dds", "grassTex");
	LoadTexture(L"Textures/WireFence.dds", "wireFenceTex");
	LoadTexture(L"Textures/ice.dds", "mirrorTex");
	LoadTexture(L"Textures/white1x1.dds", "whiteTex");
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> StencilApp::GetStaticSamplers()
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

void StencilApp::BuildRootSignature()
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

void StencilApp::BuildDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = textures.size();
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	ThrowIfFailed(device->GetD3DDevice()->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvHeap)));
}

void StencilApp::BuildShaderResourceViews()
{
	int textureIndex = 0;
	for (auto& each : textures)
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(srvHeap->GetCPUDescriptorHandleForHeapStart());
		hDescriptor.Offset(textureIndex, device->GetCbvSrvUavDescriptorSize());

		auto texture = each.second.get();
		auto textureResource = texture->Resource;
		each.second->SrvIndex = textureIndex;

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = textureResource->GetDesc().Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = textureResource->GetDesc().MipLevels;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		device->GetD3DDevice()->CreateShaderResourceView(
			textureResource.Get(), &srvDesc, hDescriptor);

		textureIndex++;
	}
}

void StencilApp::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO defines[] =
	{
		"FOG", "1",
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	shaders["standardVS"] = DxUtil::CompileShader(L"11Stencil\\Shaders\\Default.hlsl", nullptr, "VS", "vs_5_0");
	shaders["opaquePS"] = DxUtil::CompileShader(L"11Stencil\\Shaders\\Default.hlsl", defines, "PS", "ps_5_0");
	shaders["clearVS"] = DxUtil::CompileShader(L"11Stencil\\Shaders\\Clear.hlsl", nullptr, "VSClear", "vs_5_0");
	shaders["clearPS"] = DxUtil::CompileShader(L"11Stencil\\Shaders\\Clear.hlsl", nullptr, "PSClear", "ps_5_0");

	inputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void StencilApp::BuildLandGeometry()
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

void StencilApp::BuildCrateGeometry()
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

void StencilApp::BuildWavesGeometryBuffers()
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

void StencilApp::BuildMirrorGeometry()
{
	GeometryGenerator geoGen;
	auto quad = geoGen.CreateQuad(0.0f, 0.0f, -50.0f, -50.0f, 0.0f);

	auto indices = quad.GetIndices16();
	std::vector<Vertex> vertices(quad.Vertices.size());
	
	for(int i = 0; i < vertices.size(); ++i)
	{
		vertices[i].Pos = quad.Vertices[i].Position;
		vertices[i].Normal = quad.Vertices[i].Normal;
		vertices[i].TexC = quad.Vertices[i].TexC;
	}

	auto mirrorGeometry = std::make_unique<MeshGeometry>();
	auto vbByteSize = sizeof(Vertex) * vertices.size();
	auto ibByteSize = sizeof(std::uint16_t) * indices.size();

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &mirrorGeometry->IndexBufferCPU));
	ThrowIfFailed(D3DCreateBlob(vbByteSize, &mirrorGeometry->VertexBufferCPU));
	
	CopyMemory(mirrorGeometry->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
	CopyMemory(mirrorGeometry->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	auto commandList = device->GetCommandList();

	mirrorGeometry->VertexBufferGPU = DxUtil::CreateDefaultBuffer(device->GetD3DDevice().Get(),
		commandList.Get(), vertices.data(), vbByteSize, mirrorGeometry->VertexBufferUploader);

	mirrorGeometry->IndexBufferGPU = DxUtil::CreateDefaultBuffer(device->GetD3DDevice().Get(),
		commandList.Get(), indices.data(), ibByteSize, mirrorGeometry->IndexBufferUploader);

	mirrorGeometry->VertexByteStride = sizeof(Vertex);
	mirrorGeometry->VertexBufferByteSize = vbByteSize;
	mirrorGeometry->IndexFormat = DXGI_FORMAT_R16_UINT;
	mirrorGeometry->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	mirrorGeometry->DrawArgs["mirror"] = submesh;

	geometries["mirrorGeo"] = std::move(mirrorGeometry);
}

void StencilApp::BuildEndOfWorldGeometry()
{
	GeometryGenerator geoGen;
	auto quad = geoGen.CreateQuad(0.0f, 0.0f, 1000.0f, 1000.0f, 0.0f);

	auto indices = quad.GetIndices16();
	std::vector<Vertex> vertices(quad.Vertices.size());

	for (int i = 0; i < vertices.size(); ++i)
	{
		vertices[i].Pos = quad.Vertices[i].Position;
		vertices[i].Normal = quad.Vertices[i].Normal;
		vertices[i].TexC = quad.Vertices[i].TexC;
	}

	auto wallGeometry = std::make_unique<MeshGeometry>();
	auto vbByteSize = sizeof(Vertex) * vertices.size();
	auto ibByteSize = sizeof(std::uint16_t) * indices.size();

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &wallGeometry->IndexBufferCPU));
	ThrowIfFailed(D3DCreateBlob(vbByteSize, &wallGeometry->VertexBufferCPU));

	CopyMemory(wallGeometry->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
	CopyMemory(wallGeometry->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	auto commandList = device->GetCommandList();

	wallGeometry->VertexBufferGPU = DxUtil::CreateDefaultBuffer(device->GetD3DDevice().Get(),
		commandList.Get(), vertices.data(), vbByteSize, wallGeometry->VertexBufferUploader);

	wallGeometry->IndexBufferGPU = DxUtil::CreateDefaultBuffer(device->GetD3DDevice().Get(),
		commandList.Get(), indices.data(), ibByteSize, wallGeometry->IndexBufferUploader);

	wallGeometry->VertexByteStride = sizeof(Vertex);
	wallGeometry->VertexBufferByteSize = vbByteSize;
	wallGeometry->IndexFormat = DXGI_FORMAT_R16_UINT;
	wallGeometry->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	wallGeometry->DrawArgs["wall"] = submesh;

	geometries["wallGeo"] = std::move(wallGeometry);
}

void StencilApp::BuildReflectedItems()
{
}

void StencilApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { inputLayout.data(), (UINT)inputLayout.size() };
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
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState.StencilEnable = true;
	opaquePsoDesc.DepthStencilState.StencilWriteMask = 0x00;
	opaquePsoDesc.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
	opaquePsoDesc.DepthStencilState.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
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

	D3D12_GRAPHICS_PIPELINE_STATE_DESC markMirrorsDepthPsoDesc = opaquePsoDesc;
	markMirrorsDepthPsoDesc.PS =
	{
		nullptr,
		0
	};
	ThrowIfFailed(device->GetD3DDevice()->CreateGraphicsPipelineState(
		&markMirrorsDepthPsoDesc, IID_PPV_ARGS(&PSOs["markDepthMirrors"])
	));
	
	D3D12_GRAPHICS_PIPELINE_STATE_DESC markMirrorsStencilPsoDesc = opaquePsoDesc;

	CD3DX12_BLEND_DESC mirrorBlendState(D3D12_DEFAULT);

	D3D12_DEPTH_STENCIL_DESC mirrorDSS;

	mirrorDSS.DepthEnable = true;
	mirrorDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	mirrorDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	mirrorDSS.StencilEnable = true;
	mirrorDSS.StencilReadMask = 0xff;
	mirrorDSS.StencilWriteMask = 0xff;

	mirrorDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
	mirrorDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	mirrorDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
	mirrorDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	markMirrorsStencilPsoDesc.BlendState = mirrorBlendState;
	markMirrorsStencilPsoDesc.DepthStencilState = mirrorDSS;
	markMirrorsStencilPsoDesc.PS =
	{
	reinterpret_cast<BYTE*>(shaders["clearPS"]->GetBufferPointer()),
	shaders["clearPS"]->GetBufferSize()
	};
	ThrowIfFailed(device->GetD3DDevice()->CreateGraphicsPipelineState(
		&markMirrorsStencilPsoDesc, IID_PPV_ARGS(&PSOs["markStencilMirrors"])
	));

	auto alwaysRenderInStencil = opaquePsoDesc;

	mirrorDSS.DepthEnable = true;
	mirrorDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	mirrorDSS.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	mirrorDSS.StencilEnable = true;
	mirrorDSS.StencilReadMask = 0xff;
	mirrorDSS.StencilWriteMask = 0xff;

	mirrorDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

	mirrorDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

	alwaysRenderInStencil.BlendState = mirrorBlendState;
	alwaysRenderInStencil.DepthStencilState = mirrorDSS;
	alwaysRenderInStencil.PS =
	{
	reinterpret_cast<BYTE*>(shaders["clearPS"]->GetBufferPointer()),
	shaders["clearPS"]->GetBufferSize()
	};
	ThrowIfFailed(device->GetD3DDevice()->CreateGraphicsPipelineState(
		&alwaysRenderInStencil, IID_PPV_ARGS(&PSOs["alwaysRenderInStencil"])
	));

	auto drawReflectionsPsoDesc = opaquePsoDesc;

	D3D12_DEPTH_STENCIL_DESC reflectionDSS;
	reflectionDSS.DepthEnable = true;
	reflectionDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	reflectionDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	reflectionDSS.StencilEnable = true;
	reflectionDSS.StencilReadMask = 0xff;
	reflectionDSS.StencilWriteMask = 0xff;

	reflectionDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	reflectionDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

	reflectionDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	reflectionDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

	drawReflectionsPsoDesc.DepthStencilState = reflectionDSS;
	drawReflectionsPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	drawReflectionsPsoDesc.RasterizerState.FrontCounterClockwise = true;


	ThrowIfFailed(device->GetD3DDevice()->CreateGraphicsPipelineState(
		&drawReflectionsPsoDesc, IID_PPV_ARGS(&PSOs["drawStencilReflections"])
	));

	auto shadowPsoDesc = transparentPsoDesc;

	D3D12_DEPTH_STENCIL_DESC shadowDSS;
	shadowDSS.DepthEnable = true;
	shadowDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	shadowDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	shadowDSS.StencilEnable = true;
	shadowDSS.StencilReadMask = 0xff;
	shadowDSS.StencilWriteMask = 0xff;

	shadowDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
	shadowDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

	shadowDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
	shadowDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

	shadowPsoDesc.DepthStencilState = shadowDSS;
	ThrowIfFailed(
		device->GetD3DDevice()->CreateGraphicsPipelineState(
			&shadowPsoDesc, IID_PPV_ARGS(&PSOs["shadow"])
		)
	);
}

void StencilApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		frameResources.push_back(std::make_unique<FrameResource>(device->GetD3DDevice().Get(),
			1, static_cast<UINT>(allRitems.size()), static_cast<UINT>(materials.size()), waves->VertexCount()));
	}
}

void StencilApp::BuildMaterials()
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

	auto mirror = std::make_unique<Material>();
	mirror->Name = "mirror";
	mirror->MatCBIndex = 4;
	mirror->DiffuseSrvHeapIndex = textures["mirrorTex"]->SrvIndex;
	mirror->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.3f);
	mirror->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	mirror->Roughness = 0.5f;

	auto shadow = std::make_unique<Material>();
	shadow->Name = "shadow";
	shadow->MatCBIndex = 5;
	shadow->DiffuseSrvHeapIndex = textures["whiteTex"]->SrvIndex;
	shadow->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.5f);
	shadow->FresnelR0 = XMFLOAT3(0.001f, 0.001f, 0.001f);
	shadow->Roughness = 0.0f;


	materials["crate"] = std::move(crate);
	materials["grass"] = std::move(grass);
	materials["water"] = std::move(water);
	materials["wireFence"] = std::move(wireFenceBox);
	materials["mirror"] = std::move(mirror);
}

void StencilApp::BuildRenderItems()
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

	RitemLayer[(int)RenderLayer::OpaqueFrustumCull].push_back(gridRitem.get());

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

	RitemLayer[(int)RenderLayer::OpaqueFrustumCull].push_back(crateRitem.get());

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

	RitemLayer[(int)RenderLayer::OpaqueNonFrustumCull].push_back(wavesRitem.get());


	allRitems.push_back(std::move(gridRitem));
	allRitems.push_back(std::move(crateRitem));
	allRitems.push_back(std::move(wavesRitem));


	// Update reflection world matrix.
	XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // xy plane
	XMMATRIX R = XMMatrixReflect(mirrorPlane);
	//XMStoreFloat4x4(&mReflectedSkullRitem->World, skullWorld * R);

	int objectCBIndex = 3;
	
	for (auto& each : RitemLayer[static_cast<int>(RenderLayer::OpaqueFrustumCull)])
	{
		auto reflectedRitem = std::make_unique<ReflectedRenderItem>();
		XMStoreFloat4x4(&reflectedRitem->World,
			XMLoadFloat4x4(&each->World) * R);
		reflectedRitem->World = MathHelper::Identity4x4();
		reflectedRitem->ObjCBIndex = objectCBIndex++;
		reflectedRitem->Mat = each->Mat;
		reflectedRitem->Geo = each->Geo;
		reflectedRitem->PrimitiveType = each->PrimitiveType;
		reflectedRitem->IndexCount = each->IndexCount;
		reflectedRitem->StartIndexLocation = each->StartIndexLocation;
		reflectedRitem->BaseVertexLocation = each->BaseVertexLocation;
		reflectedRitem->OriginalRenderItem = each;

		RitemLayer[static_cast<int>(RenderLayer::Reflected)].push_back(reflectedRitem.get());
		allRitems.push_back(std::move(reflectedRitem));
	}

	auto mirrorRitem = std::make_unique<RenderItem>();
	mirrorRitem->World = MathHelper::Identity4x4();
	mirrorRitem->ObjCBIndex = objectCBIndex++;
	mirrorRitem->Mat = materials["mirror"].get();
	mirrorRitem->Geo = geometries["mirrorGeo"].get();
	mirrorRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	mirrorRitem->IndexCount = mirrorRitem->Geo->DrawArgs["mirror"].IndexCount;
	mirrorRitem->StartIndexLocation = mirrorRitem->Geo->DrawArgs["mirror"].StartIndexLocation;
	mirrorRitem->BaseVertexLocation = mirrorRitem->Geo->DrawArgs["mirror"].BaseVertexLocation;

	RitemLayer[static_cast<int>(RenderLayer::Mirrors)].push_back(mirrorRitem.get());
	RitemLayer[static_cast<int>(RenderLayer::OpaqueNonFrustumCull)].push_back(mirrorRitem.get());
	allRitems.push_back(std::move(mirrorRitem));

	auto wallRitem = std::make_unique<RenderItem>();
	wallRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&wallRitem->World, XMMatrixTranslation(-500.0f, 500.0f, 100.0f));
	wallRitem->ObjCBIndex = objectCBIndex++;
	wallRitem->Mat = materials["grass"].get();
	wallRitem->Geo = geometries["wallGeo"].get();
	wallRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wallRitem->IndexCount = wallRitem->Geo->DrawArgs["wall"].IndexCount;
	wallRitem->StartIndexLocation = wallRitem->Geo->DrawArgs["wall"].StartIndexLocation;
	wallRitem->BaseVertexLocation = wallRitem->Geo->DrawArgs["wall"].BaseVertexLocation;

	RitemLayer[static_cast<int>(RenderLayer::Clear)].push_back(wallRitem.get());
	allRitems.push_back(std::move(wallRitem));
}

void StencilApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
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

		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(srvHeap->GetGPUDescriptorHandleForHeapStart());
		tex.Offset(ri->Mat->DiffuseSrvHeapIndex, device->GetCbvSrvUavDescriptorSize());

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

		cmdList->SetGraphicsRootDescriptorTable(0, tex);
		cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

float StencilApp::GetHillsHeight(float x, float z)const
{
	return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
}

XMFLOAT3 StencilApp::GetHillsNormal(float x, float z)const
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
