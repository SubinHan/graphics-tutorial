#include "CubeMapApp.h"

#include <iostream>

#include "../Common/GeometryGenerator.h"
#include "../Common/DDSTextureLoader.h"

using namespace DirectX;

const int gNumFrameResources = 3;

CubeMapApp::CubeMapApp(HINSTANCE hInstance)
    : MainWindow(hInstance)
{
}

CubeMapApp::~CubeMapApp()
{
    if (device->GetD3DDevice() != nullptr)
        device->FlushCommandQueue();
}

bool CubeMapApp::Initialize()
{
    if (!MainWindow::Initialize())
        return false;

    auto commandList = device->GetCommandList();
    auto commandListAllocator = device->GetCommandListAllocator();
    auto commandQueue = device->GetCommandQueue();

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(commandList->Reset(commandListAllocator.Get(), nullptr));
    
    camera.SetPosition(0.0f, 2.0f, -15.0f);

    LoadTextures();
    BuildRootSignature();
    BuildDescriptorHeaps();
    BuildShadersAndInputLayout();
    BuildMaterials();
    BuildSkullGeometry();
    BuildShapeGeometry();
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

void CubeMapApp::OnResize()
{
    MainWindow::OnResize();

    camera.SetLens(
        0.25 * MathHelper::Pi,
        AspectRatio(),
        1.0f,
        1000.0f
    );

    BoundingFrustum::CreateFromMatrix(camFrustum, camera.GetProj());
}

void CubeMapApp::Update(const GameTimer& gt)
{
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
    UpdateInstanceBuffer(gt);
    UpdateMaterialBuffer(gt);
    UpdateMainPassCB(gt);
}


void CubeMapApp::Draw(const GameTimer& gt)
{
    auto commandList = device->GetCommandList();
    auto commandQueue = device->GetCommandQueue();
    auto cmdListAlloc = currFrameResource->CmdListAlloc;

    // Reuse the memory associated with command recording.
    // We can only reset when the associated command lists have finished execution on the GPU.
    ThrowIfFailed(cmdListAlloc->Reset());

    // A command list can be reset after it has been added to the command queue via ExecuteCommandList.
    // Reusing the command list reuses memory.
    if (isWireframe)
    {
        ThrowIfFailed(commandList->Reset(cmdListAlloc.Get(), pipelineStateObjects["opaque_wireframe"].Get()));
    }
    else
    {
        ThrowIfFailed(commandList->Reset(cmdListAlloc.Get(), pipelineStateObjects["opaque"].Get()));
    }

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

    ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap.Get() };
    commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    commandList->SetGraphicsRootSignature(rootSignature.Get());

    auto matBuffer = currFrameResource->MaterialBuffer->Resource();
    commandList->SetGraphicsRootShaderResourceView(
        1, matBuffer->GetGPUVirtualAddress());

    auto passCB = currFrameResource->PassCB->Resource();
    commandList->SetGraphicsRootConstantBufferView(
        2, passCB->GetGPUVirtualAddress());

    commandList->SetGraphicsRootDescriptorTable(
        4, srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

    commandList->SetGraphicsRootDescriptorTable(
        3, cubeMapGpuHandle
    );

    commandList->SetPipelineState(pipelineStateObjects["opaque"].Get());
    DrawRenderItems(
	    commandList.Get(),
        RitemLayer[static_cast<int>(RenderLayer::OpaqueFrustumCull)]
    );

    DrawRenderItemsWithoutFrustumCulling(
        commandList.Get(),
        RitemLayer[static_cast<int>(RenderLayer::OpaqueNonFrustumCull)]
    );

    commandList->SetPipelineState(pipelineStateObjects["sky"].Get());
    DrawRenderItemsWithoutFrustumCulling(
        commandList.Get(),
        RitemLayer[static_cast<int>(RenderLayer::Sky)]
    );

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

void CubeMapApp::OnMouseLeftDown(int x, int y, short keyState)
{
    lastMousePos.x = x;
    lastMousePos.y = y;
    
    SetCapture(m_hwnd);
}

void CubeMapApp::OnMouseLeftUp(int x, int y, short keyState)
{
    ReleaseCapture();
}

void CubeMapApp::OnMouseMove(int x, int y, short keyState)
{
    if ((keyState & MK_LBUTTON) != 0)
    {
        // Make each pixel correspond to a quarter of a degree.
        float dx = DirectX::XMConvertToRadians(0.25f * static_cast<float>(x - lastMousePos.x));
        float dy = DirectX::XMConvertToRadians(0.25f * static_cast<float>(y - lastMousePos.y));
        
        camera.Pitch(dy);
        camera.RotateY(dx);
    }

    lastMousePos.x = x;
    lastMousePos.y = y;
}

void CubeMapApp::OnKeyDown(WPARAM windowVirtualKeyCode)
{
    if (windowVirtualKeyCode != 'I')
        return;

    isWireframe = !isWireframe;
}

void CubeMapApp::UpdateCamera(const GameTimer& gt)
{
    const float dt = gt.DeltaTime();

    if (GetAsyncKeyState('W') & 0x8000)
        camera.Walk(10.0f * dt);
    if (GetAsyncKeyState('S') & 0x8000)
        camera.Walk(-10.0f * dt);
    if (GetAsyncKeyState('A') & 0x8000)
        camera.Strafe(-10.0f * dt);
    if (GetAsyncKeyState('D') & 0x8000)
        camera.Strafe(10.0f * dt);
    if (GetAsyncKeyState('E') & 0x8000)
        camera.Roll(-1.0f * dt);
    if (GetAsyncKeyState('Q') & 0x8000)
        camera.Roll(1.0f * dt);

    camera.UpdateViewMatrix();
}

void CubeMapApp::UpdateInstanceBuffer(const GameTimer& gt)
{
    int bufferOffset = 0;
    auto instanceBuffer = currFrameResource->InstanceBuffer.get();
    for(auto& e : RitemLayer[static_cast<int>(RenderLayer::OpaqueFrustumCull)])
    {
        bufferOffset += 
            e->UploadWithFrustumCulling(camera, *instanceBuffer, bufferOffset);
    }

    for (auto& e : RitemLayer[static_cast<int>(RenderLayer::OpaqueNonFrustumCull)])
    {
        bufferOffset +=
            e->UploadWithoutFrustumCulling(*instanceBuffer, bufferOffset);
    }

    for (auto& e : RitemLayer[static_cast<int>(RenderLayer::Sky)])
    {
        bufferOffset +=
            e->UploadWithoutFrustumCulling(*instanceBuffer, bufferOffset);
    }
}

void CubeMapApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = camera.GetView();
	XMMATRIX proj = camera.GetProj();

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
	mainPassCB.EyePosW = camera.GetPosition3f();
	auto clientWidth = device->GetClientWidth();
	auto clientHeight = device->GetClientHeight();

	mainPassCB.RenderTargetSize = XMFLOAT2((float)clientWidth, (float)clientHeight);
	mainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / clientWidth, 1.0f / clientHeight);
	mainPassCB.NearZ = camera.GetNearZ();
	mainPassCB.FarZ = camera.GetFarZ();
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

void CubeMapApp::UpdateMaterialBuffer(const GameTimer& gt)
{
    auto currMaterialBuffer = currFrameResource->MaterialBuffer.get();
    for (auto& e : materials)
    {
        // Only update the cbuffer data if the constants have changed.  If the cbuffer
        // data changes, it needs to be updated for each FrameResource.
        Material* mat = e.second.get();
        if (mat->NumFramesDirty > 0)
        {
            XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

            MaterialData matData;
            matData.DiffuseAlbedo = mat->DiffuseAlbedo;
            matData.FresnelR0 = mat->FresnelR0;
            matData.Roughness = mat->Roughness;
            XMStoreFloat4x4(&matData.MatTransform, XMMatrixTranspose(matTransform));
            matData.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;

            currMaterialBuffer->CopyData(mat->MatCBIndex, matData);

            // Next FrameResource need to be updated too.
            mat->NumFramesDirty--;
        }
    }

}

void CubeMapApp::LoadTexture(std::wstring filePath, std::string textureName)
{
    auto texture = std::make_unique<Texture>();
    texture->Name = textureName;
    texture->Filename = filePath;
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(device->GetD3DDevice().Get(),
        device->GetCommandList().Get(), texture->Filename.c_str(),
        texture->Resource, texture->UploadHeap));

    textures[texture->Name] = std::move(texture);
}

void CubeMapApp::LoadCubeMap(std::wstring filePath, std::string textureName)
{
    auto cubeMap = std::make_unique<Texture>();
    cubeMap->Name = textureName;
    cubeMap->Filename = filePath;
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(device->GetD3DDevice().Get(),
        device->GetCommandList().Get(), cubeMap->Filename.c_str(),
        cubeMap->Resource, cubeMap->UploadHeap));

    cubeMaps[cubeMap->Name] = std::move(cubeMap);
}

void CubeMapApp::LoadTextures()
{
    LoadTexture(L"Textures/white1x1.dds", "whiteTex");
    LoadTexture(L"Textures/tile.dds", "tileTex");
    LoadTexture(L"Textures/stone.dds", "stoneTex");
    LoadTexture(L"Textures/bricks.dds", "bricksTex");
    LoadTexture(L"Textures/WoodCrate01.dds", "crateTex");
    LoadCubeMap(L"Textures/grasscube1024.dds", "skyCubeMap");
}

void CubeMapApp::BuildDescriptorHeaps()
{
    //
    // Create the SRV heap.
    //
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = textures.size() + cubeMaps.size();
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(
        device->GetD3DDevice()->CreateDescriptorHeap(
            &srvHeapDesc,
            IID_PPV_ARGS(srvDescriptorHeap.GetAddressOf()))
    );

    int descriptorIndex = 0;
    for (auto& each : textures)
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
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
    CD3DX12_GPU_DESCRIPTOR_HANDLE cubeMapGpuHandle(srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
    cubeMapGpuHandle.Offset(descriptorIndex, device->GetCbvSrvUavDescriptorSize());
    this->cubeMapGpuHandle = cubeMapGpuHandle;

    for (auto& each : cubeMaps)
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
        hDescriptor.Offset(descriptorIndex, device->GetCbvSrvUavDescriptorSize());

        auto cubeMap = each.second.get();
        auto cubeMapResource = cubeMap->Resource;
        each.second->SrvIndex = descriptorIndex;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = cubeMapResource->GetDesc().Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = cubeMapResource->GetDesc().MipLevels;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        device->GetD3DDevice()->CreateShaderResourceView(
            cubeMapResource.Get(), &srvDesc, hDescriptor);

        descriptorIndex++;
    }
}

void CubeMapApp::BuildRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE cubeMapTable;
    cubeMapTable.Init(
        D3D12_DESCRIPTOR_RANGE_TYPE_SRV, cubeMaps.size(), 0, 0
    );

    CD3DX12_DESCRIPTOR_RANGE texTable;
    texTable.Init(
        D3D12_DESCRIPTOR_RANGE_TYPE_SRV, textures.size(), 1, 0
    );

    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[5];


	slotRootParameter[0].InitAsShaderResourceView(0, 1);
    slotRootParameter[1].InitAsShaderResourceView(1, 1);
    slotRootParameter[2].InitAsConstantBufferView(0);
    slotRootParameter[3].InitAsDescriptorTable(1, &cubeMapTable,
        D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[4].InitAsDescriptorTable(1, &texTable,
        D3D12_SHADER_VISIBILITY_PIXEL);

    auto staticSamplers = DxUtil::GetStaticSamplers();

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


void CubeMapApp::BuildShadersAndInputLayout()
{
    shaders["standardVS"] = DxUtil::CompileShader(L"18CubeMapping\\Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
    shaders["opaquePS"] = DxUtil::CompileShader(L"18CubeMapping\\Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");
    shaders["skyVS"] = DxUtil::CompileShader(L"18CubeMapping\\Shaders\\sky.hlsl", nullptr, "VS", "vs_5_1");
    shaders["skyPS"] = DxUtil::CompileShader(L"18CubeMapping\\Shaders\\sky.hlsl", nullptr, "PS", "ps_5_1");

    defaultInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
}

void CubeMapApp::BuildMaterials()
{
    auto white = std::make_unique<Material>();
    white->Name = "whiteMat";
    white->MatCBIndex = 0;
    white->DiffuseSrvHeapIndex = textures["whiteTex"]->SrvIndex;
    white->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    white->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    white->Roughness = 0.0f;

    materials["whiteMat"] = std::move(white);

    auto stone = std::make_unique<Material>();
    stone->Name = "stoneMat";
    stone->MatCBIndex = 1;
    stone->DiffuseSrvHeapIndex = textures["stoneTex"]->SrvIndex;
    stone->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    stone->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    stone->Roughness = 0.0f;

    materials["stoneMat"] = std::move(stone);

    auto tile = std::make_unique<Material>();
    tile->Name = "tileMat";
    tile->MatCBIndex = 2;
    tile->DiffuseSrvHeapIndex = textures["tileTex"]->SrvIndex;
    tile->DiffuseAlbedo = XMFLOAT4(0.6f, 0.6f, 0.6f, 1.0f);
    tile->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    tile->Roughness = 0.0f;

    materials["tileMat"] = std::move(tile);

    auto bricks = std::make_unique<Material>();
    bricks->Name = "bricksMat";
    bricks->MatCBIndex = 3;
    bricks->DiffuseSrvHeapIndex = textures["bricksTex"]->SrvIndex;
    bricks->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    bricks->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    bricks->Roughness = 0.0f;

    materials["bricksMat"] = std::move(bricks);

    auto crateMat = std::make_unique<Material>();
    crateMat->Name = "crateMat";
    crateMat->MatCBIndex = 4;
    crateMat->DiffuseSrvHeapIndex = textures["crateTex"]->SrvIndex;
    crateMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    crateMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    crateMat->Roughness = 0.5f;

    materials["crateMat"] = std::move(crateMat);

    auto mirrorMat = std::make_unique<Material>();
    mirrorMat->Name = "mirrorMat";
    mirrorMat->MatCBIndex = 5;
    mirrorMat->DiffuseSrvHeapIndex = 2;
    mirrorMat->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.1f, 1.0f);
    mirrorMat->FresnelR0 = XMFLOAT3(0.98f, 0.97f, 0.95f);
    mirrorMat->Roughness = 0.1f;

    materials["mirrorMat"] = std::move(mirrorMat);

    auto skyMat = std::make_unique<Material>();
    skyMat->Name = "skyMat";
    skyMat->MatCBIndex = 6;
    skyMat->DiffuseSrvHeapIndex = cubeMaps["skyCubeMap"]->SrvIndex;
    skyMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    skyMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    skyMat->Roughness = 0.5f;

    materials["skyMat"] = std::move(skyMat);
}

void CubeMapApp::BuildSkullGeometry()
{
    std::ifstream fin("Models/skull.txt");

    if (!fin)
    {
        MessageBox(0, L"Models/skull.txt not found.", 0, 0);
        return;
    }

    UINT vcount = 0;
    UINT tcount = 0;
    std::string ignore;

    fin >> ignore >> vcount;
    fin >> ignore >> tcount;
    fin >> ignore >> ignore >> ignore >> ignore;

    XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
    XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);

    XMVECTOR vMin = XMLoadFloat3(&vMinf3);
    XMVECTOR vMax = XMLoadFloat3(&vMaxf3);

    std::vector<Vertex> vertices(vcount);
    for (UINT i = 0; i < vcount; ++i)
    {
        fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
        fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;

        XMVECTOR P = XMLoadFloat3(&vertices[i].Pos);

        // Project point onto unit sphere and generate spherical texture coordinates.
        XMFLOAT3 spherePos;
        XMStoreFloat3(&spherePos, XMVector3Normalize(P));

        float theta = atan2f(spherePos.z, spherePos.x);

        // Put in [0, 2pi].
        if (theta < 0.0f)
            theta += XM_2PI;

        float phi = acosf(spherePos.y);

        float u = theta / (2.0f * XM_PI);
        float v = phi / XM_PI;

        vertices[i].TexC = { u, v };

        vMin = XMVectorMin(vMin, P);
        vMax = XMVectorMax(vMax, P);
    }

    BoundingBox bounds;
    XMStoreFloat3(&bounds.Center, 0.5f * (vMin + vMax));
    XMStoreFloat3(&bounds.Extents, 0.5f * (vMax - vMin));

    fin >> ignore;
    fin >> ignore;
    fin >> ignore;

    std::vector<std::int32_t> indices(3 * tcount);
    for (UINT i = 0; i < tcount; ++i)
    {
        fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
    }

    fin.close();

    //
    // Pack the indices of all the meshes into one index buffer.
    //

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::int32_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "skullGeo";

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
    geo->IndexFormat = DXGI_FORMAT_R32_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;
    submesh.Bounds = bounds;

    geo->DrawArgs["skull"] = submesh;

    geometries[geo->Name] = std::move(geo);
}

void CubeMapApp::BuildShapeGeometry()
{
    GeometryGenerator geoGen;
    GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
    GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
    GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
    GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

    //
    // We are concatenating all the geometry into one big vertex/index buffer.  So
    // define the regions in the buffer each submesh covers.
    //

    // Cache the vertex offsets to each object in the concatenated vertex buffer.
    UINT boxVertexOffset = 0;
    UINT gridVertexOffset = (UINT)box.Vertices.size();
    UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
    UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

    // Cache the starting index for each object in the concatenated index buffer.
    UINT boxIndexOffset = 0;
    UINT gridIndexOffset = (UINT)box.Indices32.size();
    UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
    UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

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

    //
    // Extract the vertex elements we are interested in and pack the
    // vertices of all the meshes into one vertex buffer.
    //

    auto totalVertexCount =
        box.Vertices.size() +
        grid.Vertices.size() +
        sphere.Vertices.size() +
        cylinder.Vertices.size();

    std::vector<Vertex> vertices(totalVertexCount);

    UINT k = 0;
    for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = box.Vertices[i].Position;
        vertices[k].Normal = box.Vertices[i].Normal;
        vertices[k].TexC = box.Vertices[i].TexC;
    }

    for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = grid.Vertices[i].Position;
        vertices[k].Normal = grid.Vertices[i].Normal;
        vertices[k].TexC = grid.Vertices[i].TexC;
    }

    for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = sphere.Vertices[i].Position;
        vertices[k].Normal = sphere.Vertices[i].Normal;
        vertices[k].TexC = sphere.Vertices[i].TexC;
    }

    for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
    {
        vertices[k].Pos = cylinder.Vertices[i].Position;
        vertices[k].Normal = cylinder.Vertices[i].Normal;
        vertices[k].TexC = cylinder.Vertices[i].TexC;
    }

    std::vector<std::uint16_t> indices;
    indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
    indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
    indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
    indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "shapeGeo";

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

    geo->DrawArgs["box"] = boxSubmesh;
    geo->DrawArgs["grid"] = gridSubmesh;
    geo->DrawArgs["sphere"] = sphereSubmesh;
    geo->DrawArgs["cylinder"] = cylinderSubmesh;

    geometries[geo->Name] = std::move(geo);
}

void CubeMapApp::BuildPSOs()
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
    opaquePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    opaquePsoDesc.SampleMask = UINT_MAX;
    opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    opaquePsoDesc.NumRenderTargets = 1;
    opaquePsoDesc.RTVFormats[0] = device->GetBackBufferFormat();
    opaquePsoDesc.SampleDesc.Count = device->GetMsaaState()? 4 : 1;
    opaquePsoDesc.SampleDesc.Quality = device->GetMsaaState() ? (device->GetMsaaQuality() - 1) : 0;
    opaquePsoDesc.DSVFormat = device->GetDepthStencilFormat();

    auto d3dDevice = device->GetD3DDevice();

    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&pipelineStateObjects["opaque"])));

    //
    // PSO for opaque wireframe objects.
    //

    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueWireframePsoDesc = opaquePsoDesc;
    opaqueWireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    ThrowIfFailed(d3dDevice->CreateGraphicsPipelineState(&opaqueWireframePsoDesc, IID_PPV_ARGS(&pipelineStateObjects["opaque_wireframe"])));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC skyPsoDesc = opaquePsoDesc;

    skyPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    skyPsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    skyPsoDesc.pRootSignature = rootSignature.Get();
    skyPsoDesc.VS =
    {
        reinterpret_cast<BYTE*>(shaders["skyVS"]->GetBufferPointer()),
        shaders["skyVS"]->GetBufferSize()
    };
    skyPsoDesc.PS =
    {
        reinterpret_cast<BYTE*>(shaders["skyPS"]->GetBufferPointer()),
        shaders["skyPS"]->GetBufferSize()
    };
    ThrowIfFailed(
        d3dDevice->CreateGraphicsPipelineState(
			&skyPsoDesc, IID_PPV_ARGS(&pipelineStateObjects["sky"])
		)
    );
}


void CubeMapApp::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i)
    {
        frameResources.push_back(std::make_unique<FrameResource>(device->GetD3DDevice().Get(),
            1, instanceCount, static_cast<UINT>(materials.size())));
    }
}

void CubeMapApp::BuildSkullRenderItem(std::unique_ptr<InstancedRenderItem>& skullRitem)
{
	auto skullGeo = geometries["skullGeo"].get();
	auto skullDrawArgs = skullGeo->DrawArgs["skull"];

	skullRitem = std::make_unique<InstancedRenderItem>(
		skullGeo,
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
		skullDrawArgs.IndexCount,
		skullDrawArgs.StartIndexLocation,
		skullDrawArgs.BaseVertexLocation,
		skullDrawArgs.Bounds
	);

	// Generate instance data.
	const int n = 5;

	float width = 200.0f;
	float height = 200.0f;
	float depth = 200.0f;

	float x = -0.5f * width;
	float y = -0.5f * height;
	float z = -0.5f * depth;
	float dx = width / (n - 1);
	float dy = height / (n - 1);
	float dz = depth / (n - 1);
	for (int k = 0; k < n; ++k)
	{
		for (int i = 0; i < n; ++i)
		{
			for (int j = 0; j < n; ++j)
			{
				int index = k * n * n + i * n + j;

				InstanceData instance;
				instance.World = XMFLOAT4X4(
					1.0f, 0.0f, 0.0f, 0.0f,
					0.0f, 1.0f, 0.0f, 0.0f,
					0.0f, 0.0f, 1.0f, 0.0f,
					x + j * dx, y + i * dy, z + k * dz, 1.0f);
				XMStoreFloat4x4(&instance.TexTransform, XMMatrixScaling(2.0f, 2.0f, 1.0f));
				instance.MaterialIndex = index % materials.size();

				skullRitem->AddInstance(instance);
			}
		}
	}

	RitemLayer[static_cast<UINT>(RenderLayer::OpaqueFrustumCull)].push_back(skullRitem.get());
}

void CubeMapApp::BuildRenderItems()
{
	std::unique_ptr<InstancedRenderItem> skullRitem;

	BuildSkullRenderItem(skullRitem);

    auto skyGeo = geometries["shapeGeo"].get();
    auto skyDrawArgs = skyGeo->DrawArgs["sphere"];
    std::unique_ptr<InstancedRenderItem> skyRitem;

    skyRitem = std::make_unique<InstancedRenderItem>(
        skyGeo,
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
        skyDrawArgs.IndexCount,
        skyDrawArgs.StartIndexLocation,
        skyDrawArgs.BaseVertexLocation,
        skyDrawArgs.Bounds
        );

    InstanceData skyInstanceData;
    skyInstanceData.TexTransform = MathHelper::Identity4x4();
    skyInstanceData.MaterialIndex = materials["skyMat"]->MatCBIndex;
	XMStoreFloat4x4(&skyInstanceData.World, XMMatrixScaling(5000.0f, 5000.0f, 5000.0f));
    skyRitem->AddInstance(skyInstanceData);

    RitemLayer[static_cast<int>(RenderLayer::Sky)].push_back(skyRitem.get());


    auto boxGeo = geometries["shapeGeo"].get();
    auto boxDrawArgs = boxGeo->DrawArgs["box"];
    std::unique_ptr<InstancedRenderItem> boxRitem;

    boxRitem = std::make_unique<InstancedRenderItem>(
        boxGeo,
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
        boxDrawArgs.IndexCount,
        boxDrawArgs.StartIndexLocation,
        boxDrawArgs.BaseVertexLocation,
        boxDrawArgs.Bounds
        );

    InstanceData boxInstanceData;
    boxInstanceData.MaterialIndex = materials["bricksMat"]->MatCBIndex;
    XMStoreFloat4x4(&boxInstanceData.World, XMMatrixScaling(2.0f, 1.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
    XMStoreFloat4x4(&boxInstanceData.TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
    boxRitem->AddInstance(boxInstanceData);

    RitemLayer[static_cast<int>(RenderLayer::OpaqueNonFrustumCull)].push_back(boxRitem.get());


    auto gridGeo = geometries["shapeGeo"].get();
    auto gridDrawArgs = gridGeo->DrawArgs["grid"];
    std::unique_ptr<InstancedRenderItem> gridRitem;

    gridRitem = std::make_unique<InstancedRenderItem>(
        gridGeo,
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
        gridDrawArgs.IndexCount,
        gridDrawArgs.StartIndexLocation,
        gridDrawArgs.BaseVertexLocation,
        gridDrawArgs.Bounds
        );

    InstanceData gridInstanceData;
    gridInstanceData.MaterialIndex = materials["tileMat"]->MatCBIndex;
    gridInstanceData.World = MathHelper::Identity4x4();
    XMStoreFloat4x4(&gridInstanceData.TexTransform, XMMatrixScaling(8.0f, 8.0f, 1.0f));
    gridRitem->AddInstance(gridInstanceData);

    RitemLayer[static_cast<int>(RenderLayer::OpaqueNonFrustumCull)].push_back(gridRitem.get());


    auto cylinderGeo = geometries["shapeGeo"].get();
    auto cylinderDrawArgs = cylinderGeo->DrawArgs["cylinder"];
    auto cylinderRitem = std::make_unique<InstancedRenderItem>(
		cylinderGeo,
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
        cylinderDrawArgs.IndexCount,
        cylinderDrawArgs.StartIndexLocation,
        cylinderDrawArgs.BaseVertexLocation,
        cylinderDrawArgs.Bounds
        );

    auto sphereGeo = geometries["shapeGeo"].get();
    auto sphereDrawArgs = sphereGeo->DrawArgs["sphere"];
    auto sphereRitem = std::make_unique<InstancedRenderItem>(
        sphereGeo,
        D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
        sphereDrawArgs.IndexCount,
        sphereDrawArgs.StartIndexLocation,
        sphereDrawArgs.BaseVertexLocation,
        sphereDrawArgs.Bounds
        );

    XMMATRIX brickTexTransform = XMMatrixScaling(1.5f, 2.0f, 1.0f);
    UINT objCBIndex = 4;
    for (int i = 0; i < 5; ++i)
    {
        InstanceData leftCylInstanceData;
        InstanceData rightCylInstanceData;

        XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
        XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);

        XMStoreFloat4x4(&leftCylInstanceData.World, leftCylWorld);
        XMStoreFloat4x4(&leftCylInstanceData.TexTransform, brickTexTransform);
    	leftCylInstanceData.MaterialIndex = materials["bricksMat"]->MatCBIndex;

        XMStoreFloat4x4(&rightCylInstanceData.World, rightCylWorld);
        XMStoreFloat4x4(&rightCylInstanceData.TexTransform, brickTexTransform);
        rightCylInstanceData.MaterialIndex = materials["bricksMat"]->MatCBIndex;

        cylinderRitem->AddInstance(leftCylInstanceData);
        cylinderRitem->AddInstance(rightCylInstanceData);


        InstanceData leftSphereInstanceData;
        InstanceData rightSphereInstanceData;

        XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
        XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

        XMStoreFloat4x4(&leftSphereInstanceData.World, leftSphereWorld);
        XMStoreFloat4x4(&leftSphereInstanceData.TexTransform, brickTexTransform);
        leftSphereInstanceData.MaterialIndex = materials["mirrorMat"]->MatCBIndex;

        XMStoreFloat4x4(&rightSphereInstanceData.World, rightSphereWorld);
        XMStoreFloat4x4(&rightSphereInstanceData.TexTransform, brickTexTransform);
        rightSphereInstanceData.MaterialIndex = materials["mirrorMat"]->MatCBIndex;

        sphereRitem->AddInstance(leftSphereInstanceData);
        sphereRitem->AddInstance(rightSphereInstanceData);
    }

    RitemLayer[static_cast<int>(RenderLayer::OpaqueNonFrustumCull)].push_back(cylinderRitem.get());
    RitemLayer[static_cast<int>(RenderLayer::OpaqueNonFrustumCull)].push_back(sphereRitem.get());


	allRitems.push_back(std::move(skullRitem));
    allRitems.push_back(std::move(skyRitem));
    allRitems.push_back(std::move(boxRitem));
    allRitems.push_back(std::move(gridRitem));
	allRitems.push_back(std::move(cylinderRitem));
    allRitems.push_back(std::move(sphereRitem));

    for(auto& e : allRitems)
    {
        instanceCount += e->GetInstanceCount();
    }
}


void CubeMapApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<InstancedRenderItem*>& ritems)
{
    for(auto e : ritems)
    {
        e->DrawFrustumCullednstances(cmdList, currFrameResource->InstanceBuffer->Resource(), 0);
    }
}

void CubeMapApp::DrawRenderItemsWithoutFrustumCulling(ID3D12GraphicsCommandList* cmdList, const std::vector<InstancedRenderItem*>& ritems)
{
    for (auto e : ritems)
    {
        e->DrawAllInstances(cmdList, currFrameResource->InstanceBuffer->Resource(), 0);
    }
}