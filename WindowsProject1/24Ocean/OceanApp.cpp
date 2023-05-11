#include "OceanApp.h"
#include "../Common/GeometryGenerator.h"

using namespace DirectX;

const int gNumFrameResources = 3;

OceanApp::OceanApp(HINSTANCE hInstance)
	: MainWindow(hInstance)
{
	// Estimate the scene bounding sphere manually since we know how the scene was constructed.
	// The grid is the "widest object" with a width of 20 and depth of 30.0f, and centered at
	// the world space origin.  In general, you need to loop over every world space vertex
	// position and compute the bounding sphere.
	mSceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
	mSceneBounds.Radius = sqrtf(10.0f * 10.0f + 15.0f * 15.0f);


}

OceanApp::~OceanApp()
{
	if (device->GetD3DDevice() != nullptr)
		device->FlushCommandQueue();
}

bool OceanApp::Initialize()
{
	if (!MainWindow::Initialize())
		return false;

	auto commandList = device->GetCommandList();
	auto commandListAllocator = device->GetCommandListAllocator();
	auto commandQueue = device->GetCommandQueue();

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(commandList->Reset(commandListAllocator.Get(), nullptr));

	mCamera.SetPosition(0.0f, 2.0f, -15.0f);

	mShadowMap = std::make_unique<ShadowMap>(device->GetD3DDevice().Get(),
		2048, 2048);

	mSsao = std::make_unique<Ssao>(
		device->GetD3DDevice().Get(),
		commandList.Get(),
		device->GetClientWidth(), device->GetClientHeight());

	mOceanMap = std::make_unique<OceanMap>(
		device->GetD3DDevice().Get(),
		256,
		256
		);

	LoadTextures();
	BuildRootSignature();
	BuildSsaoRootSignature();
	BuildOceanBasisRootSignature();
	BuildOceanFrequencyRootSignature();
	BuildOceanDisplacementRootSignature();
	BuildOceanDebugRootSignature();
	BuildDescriptorHeaps();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildMaterials();
	BuildRenderItems();
	BuildFrameResources();
	BuildPSOs();

	mSsao->SetPSOs(mPSOs["ssao"].Get(), mPSOs["ssaoBlur"].Get());


	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mOceanMap->BuildOceanBasis(
		commandList.Get(), 
		mOceanBasisRootSignature.Get(), 
		mPSOs["oceanBasis"].Get()
	);

	// Execute the initialization commands.
	ThrowIfFailed(commandList->Close());
	ID3D12CommandList* cmdsLists[] = { commandList.Get() };
	commandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until initialization is complete.
	device->FlushCommandQueue();

	return true;
}

void OceanApp::OnResize()
{
	MainWindow::OnResize();

	mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);

	if (mSsao != nullptr)
	{
		mSsao->OnResize(device->GetClientWidth(), device->GetClientHeight());

		// Resources changed, so need to rebuild descriptors.
		mSsao->RebuildDescriptors(device->GetDepthStencilBuffer());
	}
}

void OceanApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);

	// Cycle through the circular frame resource array.
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	auto fence = device->GetFence();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (mCurrFrameResource->Fence != 0 && fence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(fence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	mLightRotationAngle += 0.1f * gt.DeltaTime();

	XMMATRIX R = XMMatrixRotationY(mLightRotationAngle);
	for (int i = 0; i < 3; ++i)
	{
		XMVECTOR lightDir = XMLoadFloat3(&mBaseLightDirections[i]);
		lightDir = XMVector3TransformNormal(lightDir, R);
		XMStoreFloat3(&mRotatedLightDirections[i], lightDir);
	}

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialBuffer(gt);
	UpdateShadowTransform(gt);
	UpdateMainPassCB(gt);
	UpdateShadowPassCB(gt);
	UpdateSsaoCB(gt);
}

void OceanApp::Draw(const GameTimer& gt)
{
	auto commandList = device->GetCommandList();
	auto commandQueue = device->GetCommandQueue();
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(cmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	ThrowIfFailed(commandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mOceanMap->ComputeOceanFrequency(
		commandList.Get(),
		mOceanFrequencyRootSignature.Get(),
		mPSOs["oceanFrequency"].Get(),
		gt.TotalTime());

	mOceanMap->ComputeOceanDisplacement(
		commandList.Get(),
		mOceanDisplacementRootSignature.Get(),
		mPSOs["oceanShift"].Get(),
		mPSOs["oceanBitReversal"].Get(),
		mPSOs["oceanFft1d"].Get(),
		mPSOs["oceanTranspose"].Get(),
		mPSOs["oceanMakeDisplacement"].Get(),
		mPSOs["oceanCalculateNormal"].Get()
		);

	commandList->SetGraphicsRootSignature(mRootSignature.Get());

	//
	// Shadow map pass.
	//

	// Bind all the mMaterials used in this scene.  For structured buffers, we can bypass the heap and 
	// set as a root descriptor.
	auto matBuffer = mCurrFrameResource->MaterialBuffer->Resource();
	commandList->SetGraphicsRootShaderResourceView(MAIN_ROOT_SLOT_MATERIAL_SRV, matBuffer->GetGPUVirtualAddress());

	// Bind null SRV for shadow map pass.
	commandList->SetGraphicsRootDescriptorTable(MAIN_ROOT_SLOT_CUBE_SHADOW_SSAO_TABLE, mNullSrv);

	// Bind all the mTextures used in this scene.  Observe
	// that we only have to specify the first descriptor in the table.  
	// The root signature knows how many descriptors are expected in the table.
	commandList->SetGraphicsRootDescriptorTable(MAIN_ROOT_SLOT_TEXTURE_TABLE, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	DrawSceneToShadowMap();

	//
	// Normal/depth pass.
	//

	DrawNormalsAndDepth();

	//
	// Compute SSAO.
	// 

	commandList->SetGraphicsRootSignature(mSsaoRootSignature.Get());
	mSsao->ComputeSsao(commandList.Get(), mCurrFrameResource, 3);


	//
	// Main rendering pass.
	//

	commandList->SetGraphicsRootSignature(mRootSignature.Get());

	// Rebind state whenever graphics root signature changes.

	// Bind all the mMaterials used in this scene.  For structured buffers, we can bypass the heap and 
	// set as a root descriptor.
	matBuffer = mCurrFrameResource->MaterialBuffer->Resource();
	commandList->SetGraphicsRootShaderResourceView(MAIN_ROOT_SLOT_MATERIAL_SRV, matBuffer->GetGPUVirtualAddress());


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
	commandList->OMSetRenderTargets(1, &currentBackBufferView, true, &depthStencilView);
	
	// Bind all the mTextures used in this scene.  Observe
	// that we only have to specify the first descriptor in the table.  
	// The root signature knows how many descriptors are expected in the table.
	commandList->SetGraphicsRootDescriptorTable(MAIN_ROOT_SLOT_TEXTURE_TABLE, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	commandList->SetGraphicsRootConstantBufferView(MAIN_ROOT_SLOT_PASS_CB, passCB->GetGPUVirtualAddress());

	// Bind the sky cube map.  For our demos, we just use one "world" cube map representing the environment
	// from far away, so all objects will use the same cube map and we only need to set it once per-frame.  
	// If we wanted to use "local" cube maps, we would have to change them per-object, or dynamically
	// index into an array of cube maps.

	CD3DX12_GPU_DESCRIPTOR_HANDLE skyTexDescriptor(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	skyTexDescriptor.Offset(mSkyTexHeapIndex, device->GetCbvSrvUavDescriptorSize());
	commandList->SetGraphicsRootDescriptorTable(MAIN_ROOT_SLOT_CUBE_SHADOW_SSAO_TABLE, skyTexDescriptor);

	commandList->SetPipelineState(mPSOs["opaque"].Get());
	DrawRenderItems(commandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

	//commandList->SetPipelineState(mPSOs["debugSsao"].Get());
	//DrawRenderItems(commandList.Get(), mRitemLayer[(int)RenderLayer::DebugSsao]);

	commandList->SetPipelineState(mPSOs["sky"].Get());
	DrawRenderItems(commandList.Get(), mRitemLayer[(int)RenderLayer::Sky]);

	auto oceanDisplacementDescriptor = mOceanMap->GetGpuDisplacementMapSrv();
	commandList->SetGraphicsRootDescriptorTable(MAIN_ROOT_SLOT_OCEAN_TABLE, oceanDisplacementDescriptor);
	commandList->SetGraphicsRootDescriptorTable(MAIN_ROOT_SLOT_OCEAN_NORMAL_TABLE, mOceanMap->GetGpuNormalMapSrv());

	if(mIsWireframe)
	{
		commandList->SetPipelineState(mPSOs["oceanWireframe"].Get());
	}
	else
	{
		commandList->SetPipelineState(mPSOs["ocean"].Get());
	}

	DrawRenderItems(commandList.Get(), mRitemLayer[(int)RenderLayer::Ocean]);

	if (mIsDebugging)
	{
		DrawDebugThings(commandList.Get());
	}


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
	mCurrFrameResource->Fence = device->IncreaseFence();

	// Add an instruction to the command queue to set a new fence point. 
	// Because we are on the GPU timeline, the new fence point won't be 
	// set until the GPU finishes processing all the commands prior to this Signal().
	commandQueue->Signal(device->GetFence().Get(), device->GetCurrentFence());
}


void OceanApp::DrawDebugThings(ComPtr<ID3D12GraphicsCommandList>  commandList)
{
	commandList->SetGraphicsRootSignature(mOceanDebugRootSignature.Get());

	struct DebugConstants
	{
		DirectX::XMFLOAT2 Pos;
		float TexZ;
		float Gain;
	};

	DebugConstants c = { {0.0f, 0.0f} , 0.0f, 1000.0f };
	const int NUM_32_BITS = sizeof(DebugConstants) / 4;
	const float Z_INDEX_GAP = 1.0f / mOceanMap->NUM_OCEAN_FREQUENCY + 0.01f;

	// Draw htilde0, htilde0*
	commandList->SetGraphicsRoot32BitConstants(OCEAN_DEBUG_ROOT_SLOT_PASS_CB, NUM_32_BITS, &c, 0);
	auto oceanDebugDescriptor = mOceanMap->GetGpuHTilde0Srv();
	commandList->SetGraphicsRootDescriptorTable(
		OCEAN_DEBUG_ROOT_SLOT_HTILDE0_SRV, 
		mOceanMap->GetGpuHTilde0Srv());
	commandList->SetPipelineState(mPSOs["debugOcean"].Get());
	DrawOceanDebug(commandList.Get(), mRitemLayer[(int)RenderLayer::DebugOcean]);

	c.Pos.x = 0.5f;
	commandList->SetGraphicsRoot32BitConstants(OCEAN_DEBUG_ROOT_SLOT_PASS_CB, NUM_32_BITS, &c, 0);
	commandList->SetGraphicsRootDescriptorTable(
		OCEAN_DEBUG_ROOT_SLOT_HTILDE0_SRV,
		mOceanMap->GetGpuHTilde0ConjSrv());
	DrawOceanDebug(commandList.Get(), mRitemLayer[(int)RenderLayer::DebugOcean]);

	c.Pos.y = DEBUG_SIZE_Y * -1;
	c.TexZ = Z_INDEX_GAP * 1;
	commandList->SetGraphicsRoot32BitConstants(OCEAN_DEBUG_ROOT_SLOT_PASS_CB, NUM_32_BITS, &c, 0);
	commandList->SetGraphicsRootDescriptorTable(
		OCEAN_DEBUG_ROOT_SLOT_HTILDE0_SRV,
		mOceanMap->GetGpuHTilde0ConjSrv());
	DrawOceanDebug(commandList.Get(), mRitemLayer[(int)RenderLayer::DebugOcean]);

	c.Pos.x = 0.0f;
	commandList->SetGraphicsRoot32BitConstants(OCEAN_DEBUG_ROOT_SLOT_PASS_CB, NUM_32_BITS, &c, 0);
	commandList->SetGraphicsRootDescriptorTable(
		OCEAN_DEBUG_ROOT_SLOT_HTILDE0_SRV,
		mOceanMap->GetGpuHTilde0Srv());
	DrawOceanDebug(commandList.Get(), mRitemLayer[(int)RenderLayer::DebugOcean]);

	c.Pos.y = DEBUG_SIZE_Y * -2;
	c.TexZ = Z_INDEX_GAP * 2;
	commandList->SetGraphicsRoot32BitConstants(OCEAN_DEBUG_ROOT_SLOT_PASS_CB, NUM_32_BITS, &c, 0);
	commandList->SetGraphicsRootDescriptorTable(
		OCEAN_DEBUG_ROOT_SLOT_HTILDE0_SRV,
		mOceanMap->GetGpuHTilde0Srv());
	DrawOceanDebug(commandList.Get(), mRitemLayer[(int)RenderLayer::DebugOcean]);

	c.Pos.x = 0.5f;
	commandList->SetGraphicsRoot32BitConstants(OCEAN_DEBUG_ROOT_SLOT_PASS_CB, NUM_32_BITS, &c, 0);
	commandList->SetGraphicsRootDescriptorTable(
		OCEAN_DEBUG_ROOT_SLOT_HTILDE0_SRV,
		mOceanMap->GetGpuHTilde0ConjSrv());
	DrawOceanDebug(commandList.Get(), mRitemLayer[(int)RenderLayer::DebugOcean]);

	// draw htilde
	c.Pos.x = 1.0f;
	c.Gain = 1.0f;
	commandList->SetGraphicsRoot32BitConstants(OCEAN_DEBUG_ROOT_SLOT_PASS_CB, NUM_32_BITS, &c, 0);
	commandList->SetGraphicsRootDescriptorTable(
		OCEAN_DEBUG_ROOT_SLOT_HTILDE0_SRV,
		mOceanMap->GetGpuHTildeSrv());
	DrawOceanDebug(commandList.Get(), mRitemLayer[(int)RenderLayer::DebugOcean]);

	c.Pos.y = DEBUG_SIZE_Y * -1;
	c.TexZ = Z_INDEX_GAP * 1;
	commandList->SetGraphicsRoot32BitConstants(OCEAN_DEBUG_ROOT_SLOT_PASS_CB, NUM_32_BITS, &c, 0);
	commandList->SetGraphicsRootDescriptorTable(
		OCEAN_DEBUG_ROOT_SLOT_HTILDE0_SRV,
		mOceanMap->GetGpuHTildeSrv());
	DrawOceanDebug(commandList.Get(), mRitemLayer[(int)RenderLayer::DebugOcean]);

	c.Pos.y = DEBUG_SIZE_Y * 0;
	c.TexZ = Z_INDEX_GAP * 0;
	commandList->SetGraphicsRoot32BitConstants(OCEAN_DEBUG_ROOT_SLOT_PASS_CB, NUM_32_BITS, &c, 0);
	commandList->SetGraphicsRootDescriptorTable(
		OCEAN_DEBUG_ROOT_SLOT_HTILDE0_SRV,
		mOceanMap->GetGpuHTildeSrv());
	DrawOceanDebug(commandList.Get(), mRitemLayer[(int)RenderLayer::DebugOcean]);

	// draw htilde spatial domain
	c.Pos.x = 1.5f;
	c.Gain = 100.0f;
	commandList->SetGraphicsRoot32BitConstants(OCEAN_DEBUG_ROOT_SLOT_PASS_CB, NUM_32_BITS, &c, 0);
	commandList->SetGraphicsRootDescriptorTable(
		OCEAN_DEBUG_ROOT_SLOT_HTILDE0_SRV,
		mOceanMap->GetGpuDisplacementMapSrv());
	DrawOceanDebug(commandList.Get(), mRitemLayer[(int)RenderLayer::DebugOcean]);

	c.Pos.y = DEBUG_SIZE_Y * -1;
	c.TexZ = Z_INDEX_GAP * 1;
	commandList->SetGraphicsRoot32BitConstants(OCEAN_DEBUG_ROOT_SLOT_PASS_CB, NUM_32_BITS, &c, 0);
	commandList->SetGraphicsRootDescriptorTable(
		OCEAN_DEBUG_ROOT_SLOT_HTILDE0_SRV,
		mOceanMap->GetGpuDisplacementMapSrv());
	DrawOceanDebug(commandList.Get(), mRitemLayer[(int)RenderLayer::DebugOcean]);

	c.Pos.y = DEBUG_SIZE_Y * -2;
	c.TexZ = Z_INDEX_GAP * 2;
	commandList->SetGraphicsRoot32BitConstants(OCEAN_DEBUG_ROOT_SLOT_PASS_CB, NUM_32_BITS, &c, 0);
	commandList->SetGraphicsRootDescriptorTable(
		OCEAN_DEBUG_ROOT_SLOT_HTILDE0_SRV,
		mOceanMap->GetGpuDisplacementMapSrv());
	DrawOceanDebug(commandList.Get(), mRitemLayer[(int)RenderLayer::DebugOcean]);

	c.Pos.y = DEBUG_SIZE_Y * -3;
	c.TexZ = Z_INDEX_GAP * 6;
	c.Gain = 100.f;
	commandList->SetGraphicsRoot32BitConstants(OCEAN_DEBUG_ROOT_SLOT_PASS_CB, NUM_32_BITS, &c, 0);
	commandList->SetGraphicsRootDescriptorTable(
		OCEAN_DEBUG_ROOT_SLOT_HTILDE0_SRV,
		mOceanMap->GetGpuDisplacementMapSrv());
	DrawOceanDebug(commandList.Get(), mRitemLayer[(int)RenderLayer::DebugOcean]);

	c.Pos.x = 1.0f;
	c.TexZ = Z_INDEX_GAP * 7;
	commandList->SetGraphicsRoot32BitConstants(OCEAN_DEBUG_ROOT_SLOT_PASS_CB, NUM_32_BITS, &c, 0);
	DrawOceanDebug(commandList.Get(), mRitemLayer[(int)RenderLayer::DebugOcean]);

	c.Pos.x = 0.5f;
	c.TexZ = Z_INDEX_GAP * 8;
	commandList->SetGraphicsRoot32BitConstants(OCEAN_DEBUG_ROOT_SLOT_PASS_CB, NUM_32_BITS, &c, 0);
	DrawOceanDebug(commandList.Get(), mRitemLayer[(int)RenderLayer::DebugOcean]);

	c.Pos.x = 0.0f;
	c.TexZ = Z_INDEX_GAP * 0;
	c.Gain = 1.0f;
	commandList->SetGraphicsRoot32BitConstants(OCEAN_DEBUG_ROOT_SLOT_PASS_CB, NUM_32_BITS, &c, 0);
	commandList->SetGraphicsRootDescriptorTable(
		OCEAN_DEBUG_ROOT_SLOT_HTILDE0_SRV,
		mOceanMap->GetGpuNormalMapSrv());
	DrawOceanDebug(commandList.Get(), mRitemLayer[(int)RenderLayer::DebugOcean]);
}


void OceanApp::OnMouseLeftDown(int x, int y, short keyState)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(m_hwnd);
}

void OceanApp::OnMouseLeftUp(int x, int y, short keyState)
{
	ReleaseCapture();
}

void OceanApp::OnMouseMove(int x, int y, short keyState)
{
	if ((keyState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = DirectX::XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = DirectX::XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		mCamera.Pitch(dy);
		mCamera.RotateY(dx);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void OceanApp::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();

	if (GetAsyncKeyState('W') & 0x8000)
		mCamera.Walk(10.0f * dt);

	if (GetAsyncKeyState('S') & 0x8000)
		mCamera.Walk(-10.0f * dt);

	if (GetAsyncKeyState('A') & 0x8000)
		mCamera.Strafe(-10.0f * dt);

	if (GetAsyncKeyState('D') & 0x8000)
		mCamera.Strafe(10.0f * dt);

	if (GetAsyncKeyState('I') & 0x8000)
	{
		if (mWireframeChangedTime < gt.TotalTime() - 1.0f)
		{
			mIsWireframe = !mIsWireframe;
			mWireframeChangedTime = gt.TotalTime();
		}
	}

	if (GetAsyncKeyState('J') & 0x8000)
	{
		if (mDebuggingChangedTime < gt.TotalTime() - 1.0f)
		{
			mIsDebugging = !mIsDebugging;
			mDebuggingChangedTime = gt.TotalTime();
		}
	}

	mCamera.UpdateViewMatrix();
}

void OceanApp::AnimateMaterials(const GameTimer& gt)
{

}

void OceanApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
			objConstants.MaterialIndex = e->Mat->MatCBIndex;

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void OceanApp::UpdateMaterialBuffer(const GameTimer& gt)
{
	auto currMaterialBuffer = mCurrFrameResource->MaterialBuffer.get();

	for (auto& each : mMaterials)
	{
		auto mat = each.second.get();

		if (mat->NumFramesDirty <= 0)
			continue;
		
		XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

		MaterialData matData;
		matData.DiffuseAlbedo = mat->DiffuseAlbedo;
		matData.FresnelR0 = mat->FresnelR0;
		matData.Roughness = mat->Roughness;
		XMStoreFloat4x4(&matData.MatTransform, XMMatrixTranspose(matTransform));
		matData.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;
		matData.NormalMapIndex = mat->NormalSrvHeapIndex;

		currMaterialBuffer->CopyData(mat->MatCBIndex, matData);

		mat->NumFramesDirty--;
	}
}

void OceanApp::UpdateShadowTransform(const GameTimer& gt)
{
	// Only the first "main" light casts a shadow.
	XMVECTOR lightDir = XMLoadFloat3(&mRotatedLightDirections[0]);
	XMVECTOR lightPos = -2.0f * mSceneBounds.Radius * lightDir;
	XMVECTOR targetPos = XMLoadFloat3(&mSceneBounds.Center);
	XMVECTOR lightUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMMATRIX lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);

	XMStoreFloat3(&mLightPosW, lightPos);

	// Transform bounding sphere to light space.
	XMFLOAT3 sphereCenterLS;
	XMStoreFloat3(&sphereCenterLS, XMVector3TransformCoord(targetPos, lightView));

	// Ortho frustum in light space encloses scene.
	float l = sphereCenterLS.x - mSceneBounds.Radius;
	float b = sphereCenterLS.y - mSceneBounds.Radius;
	float n = sphereCenterLS.z - mSceneBounds.Radius;
	float r = sphereCenterLS.x + mSceneBounds.Radius;
	float t = sphereCenterLS.y + mSceneBounds.Radius;
	float f = sphereCenterLS.z + mSceneBounds.Radius;

	mLightNearZ = n;
	mLightFarZ = f;
	XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

	// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	XMMATRIX S = lightView * lightProj * T;
	XMStoreFloat4x4(&mLightView, lightView);
	XMStoreFloat4x4(&mLightProj, lightProj);
	XMStoreFloat4x4(&mShadowTransform, S);
}

void OceanApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	XMMATRIX viewProjTex = XMMatrixMultiply(viewProj, T);
	XMMATRIX shadowTransform = XMLoadFloat4x4(&mShadowTransform);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProjTex, XMMatrixTranspose(viewProjTex));
	XMStoreFloat4x4(&mMainPassCB.ShadowTransform, XMMatrixTranspose(shadowTransform));
	mMainPassCB.EyePosW = mCamera.GetPosition3f();
	auto clientWidth = static_cast<float>(device->GetClientWidth());
	auto clientHeight = static_cast<float>(device->GetClientHeight());
	mMainPassCB.RenderTargetSize = XMFLOAT2(clientWidth, clientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / clientWidth, 1.0f / clientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.4f, 0.4f, 0.6f, 1.0f };
	mMainPassCB.Lights[0].Direction = mRotatedLightDirections[0];
	mMainPassCB.Lights[0].Strength = { 0.4f, 0.4f, 0.5f };
	mMainPassCB.Lights[1].Direction = mRotatedLightDirections[1];
	mMainPassCB.Lights[1].Strength = { 0.1f, 0.1f, 0.1f };
	mMainPassCB.Lights[2].Direction = mRotatedLightDirections[2];
	mMainPassCB.Lights[2].Strength = { 0.0f, 0.0f, 0.0f };

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void OceanApp::UpdateShadowPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&mLightView);
	XMMATRIX proj = XMLoadFloat4x4(&mLightProj);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	UINT w = mShadowMap->Width();
	UINT h = mShadowMap->Height();

	XMStoreFloat4x4(&mShadowPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mShadowPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mShadowPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mShadowPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mShadowPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mShadowPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mShadowPassCB.EyePosW = mLightPosW;
	mShadowPassCB.RenderTargetSize = XMFLOAT2((float)w, (float)h);
	mShadowPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / w, 1.0f / h);
	mShadowPassCB.NearZ = mLightNearZ;
	mShadowPassCB.FarZ = mLightFarZ;

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(1, mShadowPassCB);
}

void OceanApp::UpdateSsaoCB(const GameTimer& gt)
{
	SsaoConstants ssaoCB;

	XMMATRIX P = mCamera.GetProj();

	// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	ssaoCB.Proj = mMainPassCB.Proj;
	ssaoCB.InvProj = mMainPassCB.InvProj;
	XMStoreFloat4x4(&ssaoCB.ProjTex, XMMatrixTranspose(P * T));

	mSsao->GetOffsetVectors(ssaoCB.OffsetVectors);

	auto blurWeights = mSsao->CalcGaussWeights(2.5f);
	ssaoCB.BlurWeights[0] = XMFLOAT4(&blurWeights[0]);
	ssaoCB.BlurWeights[1] = XMFLOAT4(&blurWeights[4]);
	ssaoCB.BlurWeights[2] = XMFLOAT4(&blurWeights[8]);

	ssaoCB.InvRenderTargetSize = XMFLOAT2(1.0f / mSsao->SsaoMapWidth(), 1.0f / mSsao->SsaoMapHeight());

	// Coordinates given in view space.
	ssaoCB.OcclusionRadius = 0.5f;
	ssaoCB.OcclusionFadeStart = 0.2f;
	ssaoCB.OcclusionFadeEnd = 1.0f;
	ssaoCB.SurfaceEpsilon = 0.05f;

	auto currSsaoCB = mCurrFrameResource->SsaoCB.get();
	currSsaoCB->CopyData(0, ssaoCB);
}

void OceanApp::LoadTextures()
{
	std::vector<std::string> texNames =
	{
		"bricksDiffuseMap",
		"bricksNormalMap",
		"tileDiffuseMap",
		"tileNormalMap",
		"waterDiffuseMap",
		"defaultDiffuseMap",
		"defaultNormalMap",
		"skyCubeMap"
	};

	std::vector<std::wstring> texFilenames =
	{
		L"Textures/bricks2.dds",
		L"Textures/bricks2_nmap.dds",
		L"Textures/tile.dds",
		L"Textures/tile_nmap.dds",
		L"Textures/water.dds",
		L"Textures/white1x1.dds",
		L"Textures/default_nmap.dds",
		L"Textures/sunsetcube1024.dds"
	};

	for (int i = 0; i < (int)texNames.size(); ++i)
	{
		auto texMap = std::make_unique<Texture>();
		texMap->Name = texNames[i];
		texMap->Filename = texFilenames[i];
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(device->GetD3DDevice().Get(),
			device->GetCommandList().Get(), texMap->Filename.c_str(),
			texMap->Resource, texMap->UploadHeap));

		mTextures[texMap->Name] = std::move(texMap);
	}
}


void OceanApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE cubeShadowSsaoTable;
	cubeShadowSsaoTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0);

	CD3DX12_DESCRIPTOR_RANGE oceanTable;
	oceanTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 0);

	CD3DX12_DESCRIPTOR_RANGE oceanNormalTable;
	oceanNormalTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4, 0);

	CD3DX12_DESCRIPTOR_RANGE texTable1;
	texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 10, 4, 0);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[7];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[MAIN_ROOT_SLOT_OBJECT_CB].InitAsConstantBufferView(0);
	slotRootParameter[MAIN_ROOT_SLOT_PASS_CB].InitAsConstantBufferView(1);
	slotRootParameter[MAIN_ROOT_SLOT_MATERIAL_SRV].InitAsShaderResourceView(0, 1);
	slotRootParameter[MAIN_ROOT_SLOT_CUBE_SHADOW_SSAO_TABLE].InitAsDescriptorTable(1, &cubeShadowSsaoTable, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[MAIN_ROOT_SLOT_OCEAN_TABLE].InitAsDescriptorTable(1, &oceanTable, D3D12_SHADER_VISIBILITY_DOMAIN);
	slotRootParameter[MAIN_ROOT_SLOT_OCEAN_NORMAL_TABLE].InitAsDescriptorTable(1, &oceanNormalTable, D3D12_SHADER_VISIBILITY_DOMAIN);
	slotRootParameter[MAIN_ROOT_SLOT_TEXTURE_TABLE].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_PIXEL);

	auto staticSamplers = DxUtil::GetStaticSamplersWithShadowSampler();

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(_countof(slotRootParameter), slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
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
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}


void OceanApp::BuildSsaoRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable0;
	texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);

	CD3DX12_DESCRIPTOR_RANGE texTable1;
	texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[SSAO_ROOT_SLOT_PASS_CB].InitAsConstantBufferView(0);
	slotRootParameter[SSAO_ROOT_SLOT_CONSTANTS].InitAsConstants(1, 1);
	slotRootParameter[SSAO_ROOT_SLOT_NORMAL_DEPTH_SRV].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[SSAO_ROOT_SLOT_RANDOM_VECTOR_SRV].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_PIXEL);

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC depthMapSam(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressW
		0.0f,
		0,
		D3D12_COMPARISON_FUNC_LESS_EQUAL,
		D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE);

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	std::array<CD3DX12_STATIC_SAMPLER_DESC, 4> staticSamplers =
	{
		pointClamp, linearClamp, depthMapSam, linearWrap
	};

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(_countof(slotRootParameter), slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
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
		IID_PPV_ARGS(mSsaoRootSignature.GetAddressOf())));
}

void OceanApp::BuildOceanBasisRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable0;
	texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);

	CD3DX12_DESCRIPTOR_RANGE texTable1;
	texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE texTable2;
	texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, 0);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[3];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[OCEAN_BASIS_ROOT_SLOT_PASS_CB].InitAsConstants(mOceanMap->GetNumBasisConstants(), 0);
	slotRootParameter[OCEAN_BASIS_ROOT_SLOT_HTILDE0_UAV].InitAsDescriptorTable(1, &texTable0);
	slotRootParameter[OCEAN_BASIS_ROOT_SLOT_HTILDE0CONJ_UAV].InitAsDescriptorTable(1, &texTable1);
	
	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(_countof(slotRootParameter), slotRootParameter,
		0, nullptr,
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
		IID_PPV_ARGS(mOceanBasisRootSignature.GetAddressOf())));
}

void OceanApp::BuildOceanFrequencyRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE hTilde0Table;
	hTilde0Table.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);

	CD3DX12_DESCRIPTOR_RANGE hTilde0ConjTable;
	hTilde0ConjTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE hTildeTable;
	hTildeTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[OCEAN_FREQUENCY_ROOT_SLOT_PASS_CB].InitAsConstants(mOceanMap->GetNumFrequencyConstants(), 0);
	slotRootParameter[OCEAN_FREQUENCY_ROOT_SLOT_HTILDE0_SRV].InitAsDescriptorTable(1, &hTilde0Table);
	slotRootParameter[OCEAN_FREQUENCY_ROOT_SLOT_HTILDE0CONJ_SRV].InitAsDescriptorTable(1, &hTilde0ConjTable);
	slotRootParameter[OCEAN_FREQUENCY_ROOT_SLOT_HTILDE_UAV].InitAsDescriptorTable(1, &hTildeTable);

	auto staticSamplers = DxUtil::GetStaticSamplers();

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(_countof(slotRootParameter), slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
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
		IID_PPV_ARGS(mOceanFrequencyRootSignature.GetAddressOf())));
}

void OceanApp::BuildOceanDisplacementRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable0;
	texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);

	CD3DX12_DESCRIPTOR_RANGE texTable1;
	texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, 0);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[3];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[OCEAN_DISPLACEMENT_ROOT_SLOT_PASS_CB].InitAsConstants(mOceanMap->GetNumFftConstants(), 0);
	slotRootParameter[OCEAN_DISPLACEMENT_ROOT_SLOT_HTILDE_UAV].InitAsDescriptorTable(1, &texTable0);
	slotRootParameter[OCEAN_DISPLACEMENT_ROOT_SLOT_DISPLACEMENT_UAV].InitAsDescriptorTable(1, &texTable1);

	auto staticSamplers = DxUtil::GetStaticSamplers();

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(_countof(slotRootParameter), slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
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
		IID_PPV_ARGS(mOceanDisplacementRootSignature.GetAddressOf())));
}

void OceanApp::BuildOceanDebugRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable0;
	texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);

	CD3DX12_DESCRIPTOR_RANGE texTable1;
	texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[3];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[OCEAN_DEBUG_ROOT_SLOT_PASS_CB].InitAsConstants(4, 0);
	slotRootParameter[OCEAN_DEBUG_ROOT_SLOT_HTILDE0_SRV].InitAsDescriptorTable(1, &texTable0);
	slotRootParameter[OCEAN_DEBUG_ROOT_SLOT_DISPLACEMENT_SRV].InitAsDescriptorTable(1, &texTable1);

	auto staticSamplers = DxUtil::GetStaticSamplers();

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(_countof(slotRootParameter), slotRootParameter,
		staticSamplers.size(), staticSamplers.data(),
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
		IID_PPV_ARGS(mOceanDebugRootSignature.GetAddressOf())));
}


void OceanApp::BuildDescriptorHeaps()
{
	//
	// Create the SRV heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 26;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(device->GetD3DDevice()->CreateDescriptorHeap(
		&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	std::vector<ComPtr<ID3D12Resource>> tex2DList =
	{
		mTextures["bricksDiffuseMap"]->Resource,
		mTextures["bricksNormalMap"]->Resource,
		mTextures["tileDiffuseMap"]->Resource,
		mTextures["tileNormalMap"]->Resource,
		mTextures["defaultDiffuseMap"]->Resource,
		mTextures["defaultNormalMap"]->Resource
	};

	auto skyCubeMap = mTextures["skyCubeMap"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	for (UINT i = 0; i < (UINT)tex2DList.size(); ++i)
	{
		srvDesc.Format = tex2DList[i]->GetDesc().Format;
		srvDesc.Texture2D.MipLevels = tex2DList[i]->GetDesc().MipLevels;
		device->GetD3DDevice()->CreateShaderResourceView(tex2DList[i].Get(), &srvDesc, hDescriptor);

		// next descriptor
		hDescriptor.Offset(1, device->GetCbvSrvUavDescriptorSize());
	}

	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.MipLevels = skyCubeMap->GetDesc().MipLevels;
	srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
	srvDesc.Format = skyCubeMap->GetDesc().Format;
	device->GetD3DDevice()->CreateShaderResourceView(skyCubeMap.Get(), &srvDesc, hDescriptor);

	mSkyTexHeapIndex = (UINT)tex2DList.size();
	mSsaoAmbientMapIndex = mShadowMapHeapIndex = mSkyTexHeapIndex + 1;
	mSsaoHeapIndexStart = mShadowMapHeapIndex + 1;

	mOceanMapHeapIndex = mSsaoHeapIndexStart + 5;

	mShadowMap->BuildDescriptors(
		GetCpuSrv(mShadowMapHeapIndex),
		GetGpuSrv(mShadowMapHeapIndex),
		GetDsv(1));

	mSsao->BuildDescriptors(
		device->GetDepthStencilBuffer(),
		GetCpuSrv(mSsaoHeapIndexStart),
		GetGpuSrv(mSsaoHeapIndexStart),
		GetRtv(device->GetSwapChainBufferCount()),
		device->GetCbvSrvUavDescriptorSize(),
		device->GetRtvDescriptorSize());

	mOceanMap->BuildDescriptors(
		GetCpuSrv(mOceanMapHeapIndex),
		GetGpuSrv(mOceanMapHeapIndex));

	mNullCubeSrvIndex = mOceanMapHeapIndex + mOceanMap->GetNumDescriptors();
	mNullTexSrvIndex1 = mNullCubeSrvIndex + 1;
	mNullTexSrvIndex2 = mNullTexSrvIndex1 + 1;


	auto nullSrv = GetCpuSrv(mNullCubeSrvIndex);
	mNullSrv = GetGpuSrv(mNullCubeSrvIndex);

	device->GetD3DDevice()->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);
	nullSrv.Offset(1, device->GetCbvSrvUavDescriptorSize());

	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	device->GetD3DDevice()->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);

	nullSrv.Offset(1, device->GetCbvSrvUavDescriptorSize());
	device->GetD3DDevice()->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);

}

void OceanApp::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	mShaders["standardVS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["shadowVS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\Shadows.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["shadowOpaquePS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\Shadows.hlsl", nullptr, "PS", "ps_5_1");
	mShaders["shadowAlphaTestedPS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\Shadows.hlsl", alphaTestDefines, "PS", "ps_5_1");

	mShaders["debugSsaoVS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\ShadowDebug.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["debugSsaoPS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\ShadowDebug.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["debugOceanVS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\OceanDebug.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["debugOceanPS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\OceanDebug.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["drawNormalsVS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\DrawNormals.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["drawNormalsPS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\DrawNormals.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["ssaoVS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\Ssao.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["ssaoPS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\Ssao.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["ssaoBlurVS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\SsaoBlur.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["ssaoBlurPS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\SsaoBlur.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["skyVS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\Sky.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["skyPS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\Sky.hlsl", nullptr, "PS", "ps_5_1");

	mShaders["oceanBasisCS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\OceanBasis.hlsl", nullptr, "OceanBasisCS", "cs_5_1");
	mShaders["oceanFrequencyCS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\OceanFrequency.hlsl", nullptr, "HTildeCS", "cs_5_1");

	mShaders["oceanShiftCS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\OceanCompute.hlsl", nullptr, "ShiftCS", "cs_5_1");
	mShaders["oceanBitReversalCS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\OceanCompute.hlsl", nullptr, "BitReversalCS", "cs_5_1");
	mShaders["oceanFft1dCS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\OceanCompute.hlsl", nullptr, "Fft1dCS", "cs_5_1");
	mShaders["oceanTransposeCS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\OceanCompute.hlsl", nullptr, "TransposeCS", "cs_5_1");
	mShaders["oceanMakeDisplacementCS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\OceanCompute.hlsl", nullptr, "MakeDisplacementCS", "cs_5_1");
	mShaders["oceanCalculateNormalCS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\OceanCompute.hlsl", nullptr, "CalculateNormalCS", "cs_5_1");

	mShaders["tessVS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\Tessellation.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["tessHS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\Tessellation.hlsl", nullptr, "HS", "hs_5_1");
	mShaders["tessDS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\Tessellation.hlsl", nullptr, "DS", "ds_5_1");
	mShaders["tessPS"] = DxUtil::CompileShader(L"24Ocean\\Shaders\\Tessellation.hlsl", nullptr, "PS", "ps_5_1");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void OceanApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);
	GeometryGenerator::MeshData quadSsao = geoGen.CreateQuad(0.5f, 0.0f, 0.5f, 0.5f, 0.0f);
	GeometryGenerator::MeshData quadOcean = geoGen.CreateQuad(-1.0f, 1.0f, 0.5f, DEBUG_SIZE_Y, 0.0f);

	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
	UINT quadSsaoVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();
	UINT quadOceanVertexOffset = quadSsaoVertexOffset + (UINT)quadSsao.Vertices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
	UINT quadSsaoIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();
	UINT quadOceanIndexOffset = quadSsaoIndexOffset + (UINT)quadSsao.Indices32.size();

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	SubmeshGeometry quadSsaoSubmesh;
	quadSsaoSubmesh.IndexCount = (UINT)quadSsao.Indices32.size();
	quadSsaoSubmesh.StartIndexLocation = quadSsaoIndexOffset;
	quadSsaoSubmesh.BaseVertexLocation = quadSsaoVertexOffset;

	SubmeshGeometry quadOceanSubmesh;
	quadOceanSubmesh.IndexCount = (UINT)quadOcean.Indices32.size();
	quadOceanSubmesh.StartIndexLocation = quadOceanIndexOffset;
	quadOceanSubmesh.BaseVertexLocation = quadOceanVertexOffset;

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size() +
		quadSsao.Vertices.size() +
		quadOcean.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].TexC = box.Vertices[i].TexC;
		vertices[k].TangentU = box.Vertices[i].TangentU;
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
		vertices[k].TexC = grid.Vertices[i].TexC;
		vertices[k].TangentU = grid.Vertices[i].TangentU;
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TexC = sphere.Vertices[i].TexC;
		vertices[k].TangentU = sphere.Vertices[i].TangentU;
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].TexC = cylinder.Vertices[i].TexC;
		vertices[k].TangentU = cylinder.Vertices[i].TangentU;
	}

	for (int i = 0; i < quadSsao.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = quadSsao.Vertices[i].Position;
		vertices[k].Normal = quadSsao.Vertices[i].Normal;
		vertices[k].TexC = quadSsao.Vertices[i].TexC;
		vertices[k].TangentU = quadSsao.Vertices[i].TangentU;
	}

	for (int i = 0; i < quadOcean.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = quadOcean.Vertices[i].Position;
		vertices[k].Normal = quadOcean.Vertices[i].Normal;
		vertices[k].TexC = quadOcean.Vertices[i].TexC;
		vertices[k].TangentU = quadOcean.Vertices[i].TangentU;
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
	indices.insert(indices.end(), std::begin(quadSsao.GetIndices16()), std::end(quadSsao.GetIndices16()));
	indices.insert(indices.end(), std::begin(quadOcean.GetIndices16()), std::end(quadOcean.GetIndices16()));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = DxUtil::CreateDefaultBuffer(device->GetD3DDevice().Get(),
		device->GetCommandList().Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = DxUtil::CreateDefaultBuffer(device->GetD3DDevice().Get(),
		device->GetCommandList().Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;
	geo->DrawArgs["quadSsao"] = quadSsaoSubmesh;
	geo->DrawArgs["quadOcean"] = quadOceanSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void OceanApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc;

	ZeroMemory(&basePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	basePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	basePsoDesc.pRootSignature = mRootSignature.Get();
	basePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
		mShaders["standardVS"]->GetBufferSize()
	};
	basePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	basePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	basePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	basePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	basePsoDesc.SampleMask = UINT_MAX;
	basePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	basePsoDesc.NumRenderTargets = 1;
	basePsoDesc.RTVFormats[0] = device->GetBackBufferFormat();
	basePsoDesc.SampleDesc.Count = device->GetMsaaState() ? 4 : 1;
	basePsoDesc.SampleDesc.Quality = device->GetMsaaState() ? (device->GetMsaaQuality() - 1) : 0;
	basePsoDesc.DSVFormat = device->GetDepthStencilFormat();

	//
	// PSO for opaque objects.
	//

	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc = basePsoDesc;
	opaquePsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	opaquePsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	ThrowIfFailed(device->GetD3DDevice()->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

	//
	// PSO for shadow map pass.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC smapPsoDesc = basePsoDesc;
	smapPsoDesc.RasterizerState.DepthBias = 100000;
	smapPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;
	smapPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
	smapPsoDesc.pRootSignature = mRootSignature.Get();
	smapPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["shadowVS"]->GetBufferPointer()),
		mShaders["shadowVS"]->GetBufferSize()
	};
	smapPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["shadowOpaquePS"]->GetBufferPointer()),
		mShaders["shadowOpaquePS"]->GetBufferSize()
	};

	// Shadow map pass does not have a render target.
	smapPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	smapPsoDesc.NumRenderTargets = 0;
	ThrowIfFailed(device->GetD3DDevice()->CreateGraphicsPipelineState(&smapPsoDesc, IID_PPV_ARGS(&mPSOs["shadow_opaque"])));

	//
	// PSO for debug layer.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC debugSsaoPsoDesc = basePsoDesc;
	debugSsaoPsoDesc.pRootSignature = mRootSignature.Get();
	debugSsaoPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["debugSsaoVS"]->GetBufferPointer()),
		mShaders["debugSsaoVS"]->GetBufferSize()
	};
	debugSsaoPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["debugSsaoPS"]->GetBufferPointer()),
		mShaders["debugSsaoPS"]->GetBufferSize()
	};
	ThrowIfFailed(device->GetD3DDevice()->CreateGraphicsPipelineState(&debugSsaoPsoDesc, IID_PPV_ARGS(&mPSOs["debugSsao"])));


	//
	// PSO for debug layer.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC debugOceanPsoDesc = basePsoDesc;
	debugOceanPsoDesc.pRootSignature = mOceanDebugRootSignature.Get();
	debugOceanPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["debugOceanVS"]->GetBufferPointer()),
		mShaders["debugOceanVS"]->GetBufferSize()
	};
	debugOceanPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["debugOceanPS"]->GetBufferPointer()),
		mShaders["debugOceanPS"]->GetBufferSize()
	};
	ThrowIfFailed(device->GetD3DDevice()->CreateGraphicsPipelineState(&debugOceanPsoDesc, IID_PPV_ARGS(&mPSOs["debugOcean"])));

	//
	// PSO for drawing normals.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC drawNormalsPsoDesc = basePsoDesc;
	drawNormalsPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["drawNormalsVS"]->GetBufferPointer()),
		mShaders["drawNormalsVS"]->GetBufferSize()
	};
	drawNormalsPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["drawNormalsPS"]->GetBufferPointer()),
		mShaders["drawNormalsPS"]->GetBufferSize()
	};
	drawNormalsPsoDesc.RTVFormats[0] = Ssao::NormalMapFormat;
	drawNormalsPsoDesc.SampleDesc.Count = 1;
	drawNormalsPsoDesc.SampleDesc.Quality = 0;
	drawNormalsPsoDesc.DSVFormat = device->GetDepthStencilFormat(); 
	ThrowIfFailed(device->GetD3DDevice()->CreateGraphicsPipelineState(&drawNormalsPsoDesc, IID_PPV_ARGS(&mPSOs["drawNormals"])));

	//
	// PSO for SSAO.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC ssaoPsoDesc = basePsoDesc;
	ssaoPsoDesc.InputLayout = { nullptr, 0 };
	ssaoPsoDesc.pRootSignature = mSsaoRootSignature.Get();
	ssaoPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["ssaoVS"]->GetBufferPointer()),
		mShaders["ssaoVS"]->GetBufferSize()
	};
	ssaoPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["ssaoPS"]->GetBufferPointer()),
		mShaders["ssaoPS"]->GetBufferSize()
	};

	// SSAO effect does not need the depth buffer.
	ssaoPsoDesc.DepthStencilState.DepthEnable = false;
	ssaoPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	ssaoPsoDesc.RTVFormats[0] = Ssao::AmbientMapFormat;
	ssaoPsoDesc.SampleDesc.Count = 1;
	ssaoPsoDesc.SampleDesc.Quality = 0;
	ssaoPsoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	ThrowIfFailed(device->GetD3DDevice()->CreateGraphicsPipelineState(&ssaoPsoDesc, IID_PPV_ARGS(&mPSOs["ssao"])));

	//
	// PSO for SSAO blur.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC ssaoBlurPsoDesc = ssaoPsoDesc;
	ssaoBlurPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["ssaoBlurVS"]->GetBufferPointer()),
		mShaders["ssaoBlurVS"]->GetBufferSize()
	};
	ssaoBlurPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["ssaoBlurPS"]->GetBufferPointer()),
		mShaders["ssaoBlurPS"]->GetBufferSize()
	};
	ThrowIfFailed(device->GetD3DDevice()->CreateGraphicsPipelineState(&ssaoBlurPsoDesc, IID_PPV_ARGS(&mPSOs["ssaoBlur"])));

	//
	// PSO for sky.
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC skyPsoDesc = basePsoDesc;

	// The camera is inside the sky sphere, so just turn off culling.
	skyPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	// Make sure the depth function is LESS_EQUAL and not just LESS.  
	// Otherwise, the normalized depth values at z = 1 (NDC) will 
	// fail the depth test if the depth buffer was cleared to 1.
	skyPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	skyPsoDesc.pRootSignature = mRootSignature.Get();
	skyPsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["skyVS"]->GetBufferPointer()),
		mShaders["skyVS"]->GetBufferSize()
	};
	skyPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["skyPS"]->GetBufferPointer()),
		mShaders["skyPS"]->GetBufferSize()
	};
	ThrowIfFailed(device->GetD3DDevice()->CreateGraphicsPipelineState(&skyPsoDesc, IID_PPV_ARGS(&mPSOs["sky"])));

	D3D12_COMPUTE_PIPELINE_STATE_DESC oceanShiftPSO = {};
	oceanShiftPSO.pRootSignature = mOceanDisplacementRootSignature.Get();
	oceanShiftPSO.CS =
	{
		reinterpret_cast<BYTE*>(mShaders["oceanShiftCS"]->GetBufferPointer()),
		mShaders["oceanShiftCS"]->GetBufferSize()
	};
	oceanShiftPSO.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	ThrowIfFailed(
		device->GetD3DDevice()->CreateComputePipelineState(
			&oceanShiftPSO, IID_PPV_ARGS(&mPSOs["oceanShift"])
		)
	);

	D3D12_COMPUTE_PIPELINE_STATE_DESC oceanBitReversalPSO = {};
	oceanBitReversalPSO.pRootSignature = mOceanDisplacementRootSignature.Get();
	oceanBitReversalPSO.CS =
	{
		reinterpret_cast<BYTE*>(mShaders["oceanBitReversalCS"]->GetBufferPointer()),
		mShaders["oceanBitReversalCS"]->GetBufferSize()
	};
	oceanBitReversalPSO.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	ThrowIfFailed(
		device->GetD3DDevice()->CreateComputePipelineState(
			&oceanBitReversalPSO, IID_PPV_ARGS(&mPSOs["oceanBitReversal"])
		)
	);

	D3D12_COMPUTE_PIPELINE_STATE_DESC oceanFft1dPSO = {};
	oceanFft1dPSO.pRootSignature = mOceanDisplacementRootSignature.Get();
	oceanFft1dPSO.CS =
	{
		reinterpret_cast<BYTE*>(mShaders["oceanFft1dCS"]->GetBufferPointer()),
		mShaders["oceanFft1dCS"]->GetBufferSize()
	};
	oceanFft1dPSO.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	ThrowIfFailed(
		device->GetD3DDevice()->CreateComputePipelineState(
			&oceanFft1dPSO, IID_PPV_ARGS(&mPSOs["oceanFft1d"])
		)
	);

	D3D12_COMPUTE_PIPELINE_STATE_DESC oceanTransposePSO = {};
	oceanTransposePSO.pRootSignature = mOceanDisplacementRootSignature.Get();
	oceanTransposePSO.CS =
	{
		reinterpret_cast<BYTE*>(mShaders["oceanTransposeCS"]->GetBufferPointer()),
		mShaders["oceanTransposeCS"]->GetBufferSize()
	};
	oceanTransposePSO.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	ThrowIfFailed(
		device->GetD3DDevice()->CreateComputePipelineState(
			&oceanTransposePSO, IID_PPV_ARGS(&mPSOs["oceanTranspose"])
		)
	);

	D3D12_COMPUTE_PIPELINE_STATE_DESC oceanMakeDisplacementPSO = {};
	oceanMakeDisplacementPSO.pRootSignature = mOceanDisplacementRootSignature.Get();
	oceanMakeDisplacementPSO.CS =
	{
		reinterpret_cast<BYTE*>(mShaders["oceanMakeDisplacementCS"]->GetBufferPointer()),
		mShaders["oceanMakeDisplacementCS"]->GetBufferSize()
	};
	oceanMakeDisplacementPSO.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	ThrowIfFailed(
		device->GetD3DDevice()->CreateComputePipelineState(
			&oceanMakeDisplacementPSO, IID_PPV_ARGS(&mPSOs["oceanMakeDisplacement"])
		)
	);

	D3D12_COMPUTE_PIPELINE_STATE_DESC oceanCalculateNormalPSO = {};
	oceanCalculateNormalPSO.pRootSignature = mOceanDisplacementRootSignature.Get();
	oceanCalculateNormalPSO.CS =
	{
		reinterpret_cast<BYTE*>(mShaders["oceanCalculateNormalCS"]->GetBufferPointer()),
		mShaders["oceanCalculateNormalCS"]->GetBufferSize()
	};
	oceanCalculateNormalPSO.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	ThrowIfFailed(
		device->GetD3DDevice()->CreateComputePipelineState(
			&oceanCalculateNormalPSO, IID_PPV_ARGS(&mPSOs["oceanCalculateNormal"])
		)
	);

	D3D12_COMPUTE_PIPELINE_STATE_DESC oceanBasisPSO = {};
	oceanBasisPSO.pRootSignature = mOceanBasisRootSignature.Get();
	oceanBasisPSO.CS =
	{
		reinterpret_cast<BYTE*>(mShaders["oceanBasisCS"]->GetBufferPointer()),
		mShaders["oceanBasisCS"]->GetBufferSize()
	};
	oceanBasisPSO.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	ThrowIfFailed(
		device->GetD3DDevice()->CreateComputePipelineState(
			&oceanBasisPSO, IID_PPV_ARGS(&mPSOs["oceanBasis"])
		)
	);

	D3D12_COMPUTE_PIPELINE_STATE_DESC oceanFrequencyPSO = {};
	oceanFrequencyPSO.pRootSignature = mOceanFrequencyRootSignature.Get();
	oceanFrequencyPSO.CS =
	{
		reinterpret_cast<BYTE*>(mShaders["oceanFrequencyCS"]->GetBufferPointer()),
		mShaders["oceanFrequencyCS"]->GetBufferSize()
	};
	oceanFrequencyPSO.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	ThrowIfFailed(
		device->GetD3DDevice()->CreateComputePipelineState(
			&oceanFrequencyPSO, IID_PPV_ARGS(&mPSOs["oceanFrequency"])
		)
	);
	auto oceanPsoDesc = opaquePsoDesc;
	oceanPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
	oceanPsoDesc.VS =
	{
	reinterpret_cast<BYTE*>(mShaders["tessVS"]->GetBufferPointer()),
	mShaders["tessVS"]->GetBufferSize()
	};
	oceanPsoDesc.HS =
	{
	reinterpret_cast<BYTE*>(mShaders["tessHS"]->GetBufferPointer()),
	mShaders["tessHS"]->GetBufferSize()
	};
	oceanPsoDesc.DS =
	{
	reinterpret_cast<BYTE*>(mShaders["tessDS"]->GetBufferPointer()),
	mShaders["tessDS"]->GetBufferSize()
	};
	oceanPsoDesc.PS =
	{
	reinterpret_cast<BYTE*>(mShaders["tessPS"]->GetBufferPointer()),
	mShaders["tessPS"]->GetBufferSize()
	};
	ThrowIfFailed(
		device->GetD3DDevice()->CreateGraphicsPipelineState(
			&oceanPsoDesc, IID_PPV_ARGS(&mPSOs["ocean"])));


	auto wireframePsoDesc = oceanPsoDesc;
	wireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	ThrowIfFailed(
		device->GetD3DDevice()->CreateGraphicsPipelineState(
			&wireframePsoDesc, IID_PPV_ARGS(&mPSOs["oceanWireframe"])));
}

void OceanApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(device->GetD3DDevice().Get(),
			2, static_cast<UINT>(mAllRitems.size()), static_cast<UINT>(mMaterials.size())));
	}
}

void OceanApp::BuildMaterials()
{
	auto bricks0 = std::make_unique<Material>();
	bricks0->Name = "bricks0";
	bricks0->MatCBIndex = 0;
	bricks0->DiffuseSrvHeapIndex = 0;
	bricks0->NormalSrvHeapIndex = 1;
	bricks0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	bricks0->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	bricks0->Roughness = 0.3f;

	auto tile0 = std::make_unique<Material>();
	tile0->Name = "tile0";
	tile0->MatCBIndex = 2;
	tile0->DiffuseSrvHeapIndex = 2;
	tile0->NormalSrvHeapIndex = 3;
	tile0->DiffuseAlbedo = XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f);
	tile0->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
	tile0->Roughness = 0.1f;

	auto mirror0 = std::make_unique<Material>();
	mirror0->Name = "mirror0";
	mirror0->MatCBIndex = 3;
	mirror0->DiffuseSrvHeapIndex = 4;
	mirror0->NormalSrvHeapIndex = 5;
	mirror0->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
	mirror0->FresnelR0 = XMFLOAT3(0.98f, 0.97f, 0.95f);
	mirror0->Roughness = 0.1f;

	auto skullMat = std::make_unique<Material>();
	skullMat->Name = "skullMat";
	skullMat->MatCBIndex = 3;
	skullMat->DiffuseSrvHeapIndex = 4;
	skullMat->NormalSrvHeapIndex = 5;
	skullMat->DiffuseAlbedo = XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
	skullMat->FresnelR0 = XMFLOAT3(0.6f, 0.6f, 0.6f);
	skullMat->Roughness = 0.2f;

	auto sky = std::make_unique<Material>();
	sky->Name = "sky";
	sky->MatCBIndex = 4;
	sky->DiffuseSrvHeapIndex = 6;
	sky->NormalSrvHeapIndex = 7;
	sky->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	sky->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	sky->Roughness = 1.0f;

	auto waterMat = std::make_unique<Material>();
	waterMat->Name = "waterMat";
	waterMat->MatCBIndex = 5;
	waterMat->NormalSrvHeapIndex = 7;
	waterMat->DiffuseSrvHeapIndex = 5;
	waterMat->DiffuseAlbedo = XMFLOAT4(0.1f, 0.2f, 0.5f, 1.0f);
	waterMat->FresnelR0 = XMFLOAT3(0.9f, 0.9f, 0.9f);
	waterMat->Roughness = 0.99f;

	mMaterials["bricks0"] = std::move(bricks0);
	mMaterials["tile0"] = std::move(tile0);
	mMaterials["mirror0"] = std::move(mirror0);
	mMaterials["skullMat"] = std::move(skullMat);
	mMaterials["sky"] = std::move(sky);
	mMaterials["waterMat"] = std::move(waterMat);
}

void OceanApp::BuildRenderItems()
{
	auto skyRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&skyRitem->World, XMMatrixScaling(5000.0f, 5000.0f, 5000.0f));
	skyRitem->TexTransform = MathHelper::Identity4x4();
	skyRitem->ObjCBIndex = 0;
	skyRitem->Mat = mMaterials["sky"].get();
	skyRitem->Geo = mGeometries["shapeGeo"].get();
	skyRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	skyRitem->IndexCount = skyRitem->Geo->DrawArgs["sphere"].IndexCount;
	skyRitem->StartIndexLocation = skyRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	skyRitem->BaseVertexLocation = skyRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Sky].push_back(skyRitem.get());
	mAllRitems.push_back(std::move(skyRitem));

	auto quadRitem = std::make_unique<RenderItem>();
	quadRitem->World = MathHelper::Identity4x4();
	quadRitem->TexTransform = MathHelper::Identity4x4();
	quadRitem->ObjCBIndex = 1;
	quadRitem->Mat = mMaterials["bricks0"].get();
	quadRitem->Geo = mGeometries["shapeGeo"].get();
	quadRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	quadRitem->IndexCount = quadRitem->Geo->DrawArgs["quadSsao"].IndexCount;
	quadRitem->StartIndexLocation = quadRitem->Geo->DrawArgs["quadSsao"].StartIndexLocation;
	quadRitem->BaseVertexLocation = quadRitem->Geo->DrawArgs["quadSsao"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::DebugSsao].push_back(quadRitem.get());
	mAllRitems.push_back(std::move(quadRitem));

	auto quadOceanRitem = std::make_unique<RenderItem>();
	quadOceanRitem->World = MathHelper::Identity4x4();
	quadOceanRitem->TexTransform = MathHelper::Identity4x4();
	quadOceanRitem->ObjCBIndex = 1;
	quadOceanRitem->Mat = mMaterials["bricks0"].get();
	quadOceanRitem->Geo = mGeometries["shapeGeo"].get();
	quadOceanRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	quadOceanRitem->IndexCount = quadOceanRitem->Geo->DrawArgs["quadOcean"].IndexCount;
	quadOceanRitem->StartIndexLocation = quadOceanRitem->Geo->DrawArgs["quadOcean"].StartIndexLocation;
	quadOceanRitem->BaseVertexLocation = quadOceanRitem->Geo->DrawArgs["quadOcean"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::DebugOcean].push_back(quadOceanRitem.get());
	mAllRitems.push_back(std::move(quadOceanRitem));

	auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 1.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
	XMStoreFloat4x4(&boxRitem->TexTransform, XMMatrixScaling(1.0f, 0.5f, 1.0f));
	boxRitem->ObjCBIndex = 2;
	boxRitem->Mat = mMaterials["bricks0"].get();
	boxRitem->Geo = mGeometries["shapeGeo"].get();
	boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Opaque].push_back(boxRitem.get());
	mAllRitems.push_back(std::move(boxRitem));

	auto gridRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&gridRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(.0f, 8.0f, 1.0f));
	gridRitem->ObjCBIndex = 3;
	gridRitem->Mat = mMaterials["waterMat"].get();
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Ocean].push_back(gridRitem.get());
	mAllRitems.push_back(std::move(gridRitem));
}


void OceanApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = DxUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();

	// For each render item...
	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;

		cmdList->SetGraphicsRootConstantBufferView(MAIN_ROOT_SLOT_OBJECT_CB, objCBAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

void OceanApp::DrawOceanDebug(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	// For each render item...
	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}


void OceanApp::DrawSceneToShadowMap()
{
	auto commandList = device->GetCommandList();
	commandList->RSSetViewports(1, &mShadowMap->Viewport());
	commandList->RSSetScissorRects(1, &mShadowMap->ScissorRect());

	// Change to DEPTH_WRITE.
	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
		D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE));

	// Clear the back buffer and depth buffer.
	commandList->ClearDepthStencilView(mShadowMap->Dsv(),
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	commandList->OMSetRenderTargets(0, nullptr, false, &mShadowMap->Dsv());

	// Bind the pass constant buffer for the shadow map pass.
	UINT passCBByteSize = DxUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
	auto passCB = mCurrFrameResource->PassCB->Resource();
	D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = passCB->GetGPUVirtualAddress() + 1 * passCBByteSize;
	commandList->SetGraphicsRootConstantBufferView(MAIN_ROOT_SLOT_PASS_CB, passCBAddress);

	commandList->SetPipelineState(mPSOs["shadow_opaque"].Get());

	DrawRenderItems(commandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

	// Change back to GENERIC_READ so we can read the texture in a shader.
	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ));
}

void OceanApp::DrawNormalsAndDepth()
{
	auto commandList = device->GetCommandList();
	commandList->RSSetViewports(1, &device->GetScreenViewport());
	commandList->RSSetScissorRects(1, &device->GetScissorRect());

	auto normalMap = mSsao->NormalMap();
	auto normalMapRtv = mSsao->NormalMapRtv();

	// Change to RENDER_TARGET.
	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(normalMap,
		D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Clear the screen normal map and depth buffer.
	float clearValue[] = { 0.0f, 0.0f, 1.0f, 0.0f };
	commandList->ClearRenderTargetView(normalMapRtv, clearValue, 0, nullptr);
	commandList->ClearDepthStencilView(device->DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	commandList->OMSetRenderTargets(1, &normalMapRtv, true, &device->DepthStencilView());

	// Bind the constant buffer for this pass.
	auto passCB = mCurrFrameResource->PassCB->Resource();
	commandList->SetGraphicsRootConstantBufferView(MAIN_ROOT_SLOT_PASS_CB, passCB->GetGPUVirtualAddress());

	commandList->SetPipelineState(mPSOs["drawNormals"].Get());

	DrawRenderItems(commandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

	// Change back to GENERIC_READ so we can read the texture in a shader.
	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(normalMap,
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));
}


CD3DX12_CPU_DESCRIPTOR_HANDLE OceanApp::GetCpuSrv(int index)const
{
	auto srv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	srv.Offset(index, device->GetCbvSrvUavDescriptorSize());
	return srv;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE OceanApp::GetGpuSrv(int index)const
{
	auto srv = CD3DX12_GPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	srv.Offset(index, device->GetCbvSrvUavDescriptorSize());
	return srv;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE OceanApp::GetDsv(int index)const
{
	auto dsv = CD3DX12_CPU_DESCRIPTOR_HANDLE(device->GetDsvHeap()->GetCPUDescriptorHandleForHeapStart());
	dsv.Offset(index, device->GetDsvDescriptorSize());
	return dsv;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE OceanApp::GetRtv(int index)const
{
	auto rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(device->GetRtvHeap()->GetCPUDescriptorHandleForHeapStart());
	rtv.Offset(index, device->GetRtvDescriptorSize());
	return rtv;
}