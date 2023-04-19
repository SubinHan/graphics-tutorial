#include "InstancingAndCullingApp.h"
#include "../Common/GeometryGenerator.h"
#include "../Common/DDSTextureLoader.h"

using namespace DirectX;

const int gNumFrameResources = 3;

InstancingAndCullingApp::InstancingAndCullingApp(HINSTANCE hInstance)
    : MainWindow(hInstance)
{
}

InstancingAndCullingApp::~InstancingAndCullingApp()
{
    if (device->GetD3DDevice() != nullptr)
        device->FlushCommandQueue();
}

bool InstancingAndCullingApp::Initialize()
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

void InstancingAndCullingApp::OnResize()
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

void InstancingAndCullingApp::Update(const GameTimer& gt)
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


void InstancingAndCullingApp::Draw(const GameTimer& gt)
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
        3, srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());


    commandList->SetPipelineState(pipelineStateObjects["opaque"].Get());
    DrawRenderItems(
        commandList.Get(), RitemLayer[static_cast<int>(RenderLayer::OpaqueFrustumCull)]);

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

void InstancingAndCullingApp::OnMouseLeftDown(int x, int y, short keyState)
{
    lastMousePos.x = x;
    lastMousePos.y = y;

    SetCapture(m_hwnd);
}

void InstancingAndCullingApp::OnMouseLeftUp(int x, int y, short keyState)
{
    ReleaseCapture();
}

void InstancingAndCullingApp::OnMouseMove(int x, int y, short keyState)
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

void InstancingAndCullingApp::OnKeyDown(WPARAM windowVirtualKeyCode)
{
    if (windowVirtualKeyCode != 'I')
        return;

    isWireframe = !isWireframe;
}

void InstancingAndCullingApp::UpdateCamera(const GameTimer& gt)
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

void InstancingAndCullingApp::UpdateInstanceBuffer(const GameTimer& gt)
{
    XMMATRIX view = camera.GetView();
    auto viewDeterminant = XMMatrixDeterminant(view);
    XMMATRIX invView = XMMatrixInverse(&viewDeterminant, view);

    auto currInstanceBuffer = currFrameResource->InstanceBuffer.get();
    for (auto& e : allRitems)
    {
        const auto& instanceData = e->Instances;

        int visibleInstanceCount = 0;

        for (UINT i = 0; i < (UINT)instanceData.size(); ++i)
        {
            XMMATRIX world = XMLoadFloat4x4(&instanceData[i].World);
            XMMATRIX texTransform = XMLoadFloat4x4(&instanceData[i].TexTransform);

            auto worldDeterminant = XMMatrixDeterminant(world);
            XMMATRIX invWorld = XMMatrixInverse(&worldDeterminant, world);

            // View space to the object's local space.
            XMMATRIX viewToLocal = XMMatrixMultiply(invView, invWorld);

            // Transform the camera frustum from view space to the object's local space.
            BoundingFrustum localSpaceFrustum;
            camFrustum.Transform(localSpaceFrustum, viewToLocal);

            // Perform the box/frustum intersection test in local space.
            if (localSpaceFrustum.Contains(e->Bounds) != DirectX::DISJOINT)
            {
                InstanceData data;
                XMStoreFloat4x4(&data.World, XMMatrixTranspose(world));
                XMStoreFloat4x4(&data.TexTransform, XMMatrixTranspose(texTransform));
                data.MaterialIndex = instanceData[i].MaterialIndex;

                // Write the instance data to structured buffer for the visible objects.
                currInstanceBuffer->CopyData(visibleInstanceCount++, data);
            }
        }

        e->InstanceCount = visibleInstanceCount;

        std::wostringstream outs;
        outs.precision(6);
        outs << L"Instancing and Culling Demo" <<
            L"    " << e->InstanceCount <<
            L" objects visible out of " << e->Instances.size();
        mainWndCaption = outs.str();
    }
}

void InstancingAndCullingApp::UpdateMainPassCB(const GameTimer& gt)
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

void InstancingAndCullingApp::UpdateMaterialBuffer(const GameTimer& gt)
{
    auto currMaterialBuffer = currFrameResource->MaterialBuffer.get();
    for (auto& e : materials)
    {
        // Only update the cbuffer data if the constants have changed.  If the cbuffer
        // data changes, it needs to be updated for each FrameResource.
        Material* mat = e.second.get();
        if (mat->NumFramesDirty > 0)
        {
            XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform1);

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

void InstancingAndCullingApp::LoadTexture(std::wstring filePath, std::string textureName)
{
    auto texture = std::make_unique<Texture>();
    texture->Name = textureName;
    texture->Filename = filePath;
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(device->GetD3DDevice().Get(),
        device->GetCommandList().Get(), texture->Filename.c_str(),
        texture->Resource, texture->UploadHeap));

    textures[texture->Name] = std::move(texture);
}

void InstancingAndCullingApp::LoadTextures()
{
    LoadTexture(L"Textures/white1x1.dds", "whiteTex");
    LoadTexture(L"Textures/tile.dds", "tileTex");
    LoadTexture(L"Textures/stone.dds", "stoneTex");
    LoadTexture(L"Textures/bricks.dds", "bricksTex");
    LoadTexture(L"Textures/WoodCrate01.dds", "crateTex");
}

void InstancingAndCullingApp::BuildDescriptorHeaps()
{
    //
    // Create the SRV heap.
    //
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = textures.size();
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
}

void InstancingAndCullingApp::BuildRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE texTable;
    texTable.Init(
        D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 0, 0
    );

    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[4];

    slotRootParameter[0].InitAsShaderResourceView(0, 1);
    slotRootParameter[1].InitAsShaderResourceView(1, 1);
    slotRootParameter[2].InitAsConstantBufferView(0);
    slotRootParameter[3].InitAsDescriptorTable(1, &texTable,
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


void InstancingAndCullingApp::BuildShadersAndInputLayout()
{
    shaders["standardVS"] = DxUtil::CompileShader(L"16InstancingAndCulling\\Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
    shaders["opaquePS"] = DxUtil::CompileShader(L"16InstancingAndCulling\\Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");
    
    defaultInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
}

void InstancingAndCullingApp::BuildMaterials()
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
}

void InstancingAndCullingApp::BuildSkullGeometry()
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

void InstancingAndCullingApp::BuildPSOs()
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
            &transparentPsoDesc, IID_PPV_ARGS(&pipelineStateObjects["transparent"])
        )
    );
}


void InstancingAndCullingApp::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i)
    {
        frameResources.push_back(std::make_unique<FrameResource>(device->GetD3DDevice().Get(),
            1, instanceCount, static_cast<UINT>(materials.size())));
    }
}

void InstancingAndCullingApp::BuildRenderItems()
{
    auto skullRitem = std::make_unique<RenderItem>();
    skullRitem->World = MathHelper::Identity4x4();
    skullRitem->TexTransform = MathHelper::Identity4x4();
    skullRitem->ObjCBIndex = 0;
    skullRitem->Mat = materials["whiteMat"].get();
    skullRitem->Geo = geometries["skullGeo"].get();
    skullRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    skullRitem->InstanceCount = 0;
    skullRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
    skullRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["skull"].StartIndexLocation;
    skullRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["skull"].BaseVertexLocation;
    skullRitem->Bounds = skullRitem->Geo->DrawArgs["skull"].Bounds;

    // Generate instance data.
    const int n = 5;
    instanceCount = n * n * n;
    skullRitem->Instances.resize(instanceCount);

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
                // Position instanced along a 3D grid.
                skullRitem->Instances[index].World = XMFLOAT4X4(
                    1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    x + j * dx, y + i * dy, z + k * dz, 1.0f);

                XMStoreFloat4x4(&skullRitem->Instances[index].TexTransform, XMMatrixScaling(2.0f, 2.0f, 1.0f));
                skullRitem->Instances[index].MaterialIndex = index % materials.size();
            }
        }
    }


    allRitems.push_back(std::move(skullRitem));

    // All the render items are opaque.
    for (auto& e : allRitems)
        RitemLayer[static_cast<UINT>(RenderLayer::OpaqueFrustumCull)].push_back(e.get());
}

void InstancingAndCullingApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    // For each render item...
    for (size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

        auto vertexBufferView = ri->Geo->VertexBufferView();
        auto indexBufferView = ri->Geo->IndexBufferView();

        cmdList->IASetVertexBuffers(0, 1, &vertexBufferView);
        cmdList->IASetIndexBuffer(&indexBufferView);
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

        auto instanceBuffer =
            currFrameResource->InstanceBuffer->Resource();

    	cmdList->SetGraphicsRootShaderResourceView(
            0, instanceBuffer->GetGPUVirtualAddress());

        cmdList->DrawIndexedInstanced(ri->IndexCount, ri->InstanceCount, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}