#include "CameraApp.h"
#include "../Common/GeometryGenerator.h"
#include "../Common/DDSTextureLoader.h"

using namespace DirectX;

const int gNumFrameResources = 3;

CameraApp::CameraApp(HINSTANCE hInstance)
    : MainWindow(hInstance)
{
}

CameraApp::~CameraApp()
{
    if (device->GetD3DDevice() != nullptr)
        device->FlushCommandQueue();
}

bool CameraApp::Initialize()
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

void CameraApp::OnResize()
{
    MainWindow::OnResize();

    camera.SetLens(
        0.25 * MathHelper::Pi,
        AspectRatio(),
        1.0f,
        1000.0f
    );
}

void CameraApp::Update(const GameTimer& gt)
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

    UpdateObjectCBs(gt);
    UpdateMaterialBuffer(gt);
    UpdateMainPassCB(gt);
}


void CameraApp::Draw(const GameTimer& gt)
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

    auto passCB = currFrameResource->PassCB->Resource();
    commandList->SetGraphicsRootConstantBufferView(
        1, passCB->GetGPUVirtualAddress());

    auto matBuffer = currFrameResource->MaterialBuffer->Resource();
    commandList->SetGraphicsRootShaderResourceView(
        2, matBuffer->GetGPUVirtualAddress());

    commandList->SetGraphicsRootDescriptorTable(
        3, srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());


    commandList->SetPipelineState(pipelineStateObjects["opaque"].Get());
    DrawRenderItems(
        commandList.Get(), RitemLayer[static_cast<int>(RenderLayer::Opaque)]);

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

void CameraApp::OnMouseLeftDown(int x, int y, short keyState)
{
    lastMousePos.x = x;
    lastMousePos.y = y;

    SetCapture(m_hwnd);
}

void CameraApp::OnMouseLeftUp(int x, int y, short keyState)
{
    ReleaseCapture();
}

void CameraApp::OnMouseMove(int x, int y, short keyState)
{
    if ((keyState & MK_LBUTTON) != 0)
    {
        // Make each pixel correspond to a quarter of a degree.
        float dx = DirectX::XMConvertToRadians(0.25f * static_cast<float>(x - lastMousePos.x));
        float dy = DirectX::XMConvertToRadians(0.25f * static_cast<float>(y - lastMousePos.y));
        
        camera.Pitch(dy);
        camera.RotateY(dx);
    }

    camera.UpdateViewMatrix();

    lastMousePos.x = x;
    lastMousePos.y = y;
}

void CameraApp::OnKeyDown(WPARAM windowVirtualKeyCode)
{
    if (windowVirtualKeyCode != 'I')
        return;

    isWireframe = !isWireframe;
}

void CameraApp::UpdateCamera(const GameTimer& gt)
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

    camera.UpdateViewMatrix();
}

void CameraApp::UpdateObjectCBs(const GameTimer& gt)
{
    auto currObjectCB = currFrameResource->ObjectCB.get();
    for (auto& e : allRitems)
    {
        // Only update the cbuffer data if the constants have changed.  
        // This needs to be tracked per frame resource.
        if (e->NumFramesDirty > 0)
        {
            XMMATRIX world = XMLoadFloat4x4(&e->World);

            ObjectConstants objConstants;
            XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
            objConstants.Textransform = e->TexTransform;
            objConstants.MaterialIndex = e->Mat->MatCBIndex;

            currObjectCB->CopyData(e->ObjCBIndex, objConstants);

            // Next FrameResource need to be updated too.
            e->NumFramesDirty--;
        }
    }
}

void CameraApp::UpdateMainPassCB(const GameTimer& gt)
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

void CameraApp::UpdateMaterialBuffer(const GameTimer& gt)
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

void CameraApp::LoadTexture(std::wstring filePath, std::string textureName)
{
    auto texture = std::make_unique<Texture>();
    texture->Name = textureName;
    texture->Filename = filePath;
    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(device->GetD3DDevice().Get(),
        device->GetCommandList().Get(), texture->Filename.c_str(),
        texture->Resource, texture->UploadHeap));

    textures[texture->Name] = std::move(texture);
}

void CameraApp::LoadTextures()
{
    LoadTexture(L"Textures/white1x1.dds", "whiteTex");
    LoadTexture(L"Textures/tile.dds", "tileTex");
    LoadTexture(L"Textures/stone.dds", "stoneTex");
    LoadTexture(L"Textures/bricks.dds", "bricksTex");
}

void CameraApp::BuildDescriptorHeaps()
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

void CameraApp::BuildRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE texTable;
    texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0);

    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[4];

    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsConstantBufferView(1);
    slotRootParameter[2].InitAsShaderResourceView(0, 1);
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


void CameraApp::BuildShadersAndInputLayout()
{
    shaders["standardVS"] = DxUtil::CompileShader(L"15Camera\\Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
    shaders["opaquePS"] = DxUtil::CompileShader(L"15Camera\\Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");

    defaultInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
}

void CameraApp::BuildMaterials()
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
}

void CameraApp::BuildShapeGeometry()
{
    GeometryGenerator geoGen;
    GeometryGenerator::MeshData box = geoGen.CreateBox(1.5f, 0.5f, 1.5f, 3);
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

    // Define the SubmeshGeometry that cover different 
    // regions of the vertex/index buffers.

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

    auto d3dDevice = device->GetD3DDevice();
    auto commandList = device->GetCommandList();

    geo->VertexBufferGPU = DxUtil::CreateDefaultBuffer(d3dDevice.Get(),
        commandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = DxUtil::CreateDefaultBuffer(d3dDevice.Get(),
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

void CameraApp::BuildPSOs()
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


void CameraApp::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i)
    {
        frameResources.push_back(std::make_unique<FrameResource>(device->GetD3DDevice().Get(),
            1, static_cast<UINT>(allRitems.size()), static_cast<UINT>(materials.size())));
    }
}

void CameraApp::BuildRenderItems()
{
    auto boxRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
    boxRitem->ObjCBIndex = 0;
    boxRitem->Mat = materials["bricksMat"].get();
    boxRitem->Geo = geometries["shapeGeo"].get();
    boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
    boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
    boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
    RitemLayer[static_cast<int>(RenderLayer::Opaque)].push_back(boxRitem.get());
    allRitems.push_back(std::move(boxRitem));

    auto gridRitem = std::make_unique<RenderItem>();
    gridRitem->World = MathHelper::Identity4x4();
    gridRitem->ObjCBIndex = 1;
    XMStoreFloat4x4(
        &gridRitem->TexTransform,
        XMMatrixScaling(4.0f, 4.0f, 4.0f));
    gridRitem->Mat = materials["tileMat"].get();
    gridRitem->Geo = geometries["shapeGeo"].get();
    gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
    gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
    RitemLayer[static_cast<int>(RenderLayer::Opaque)].push_back(gridRitem.get());
    allRitems.push_back(std::move(gridRitem));

    UINT objCBIndex = 2;
    for (int i = 0; i < 5; ++i)
    {
        auto leftCylRitem = std::make_unique<RenderItem>();
        auto rightCylRitem = std::make_unique<RenderItem>();
        auto leftSphereRitem = std::make_unique<RenderItem>();
        auto rightSphereRitem = std::make_unique<RenderItem>();

        XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
        XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);

        XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
        XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

        XMStoreFloat4x4(&leftCylRitem->World, rightCylWorld);
        leftCylRitem->ObjCBIndex = objCBIndex++;
        leftCylRitem->Mat = materials["stoneMat"].get();
        leftCylRitem->Geo = geometries["shapeGeo"].get();
        leftCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
        leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
        leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

        XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
        rightCylRitem->ObjCBIndex = objCBIndex++;
        rightCylRitem->Mat = materials["stoneMat"].get();
        rightCylRitem->Geo = geometries["shapeGeo"].get();
        rightCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
        rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
        rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

        XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
        leftSphereRitem->ObjCBIndex = objCBIndex++;
        leftSphereRitem->Mat = materials["stoneMat"].get();
        leftSphereRitem->Geo = geometries["shapeGeo"].get();
        leftSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
        leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
        leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

        XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
        rightSphereRitem->ObjCBIndex = objCBIndex++;
        rightSphereRitem->Mat = materials["stoneMat"].get();
        rightSphereRitem->Geo = geometries["shapeGeo"].get();
        rightSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
        rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
        rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

        RitemLayer[static_cast<int>(RenderLayer::Opaque)].push_back(leftCylRitem.get());
        RitemLayer[static_cast<int>(RenderLayer::Opaque)].push_back(rightCylRitem.get());
        RitemLayer[static_cast<int>(RenderLayer::Opaque)].push_back(leftSphereRitem.get());
        RitemLayer[static_cast<int>(RenderLayer::Opaque)].push_back(rightSphereRitem.get());

        allRitems.push_back(std::move(leftCylRitem));
        allRitems.push_back(std::move(rightCylRitem));
        allRitems.push_back(std::move(leftSphereRitem));
        allRitems.push_back(std::move(rightSphereRitem));
    }
}

void CameraApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = DxUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT matCBByteSize = DxUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

    auto objectCB = currFrameResource->ObjectCB->Resource();

    // For each render item...
    for (size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

        auto vertexBufferView = ri->Geo->VertexBufferView();
        auto indexBufferView = ri->Geo->IndexBufferView();

        cmdList->IASetVertexBuffers(0, 1, &vertexBufferView);
        cmdList->IASetIndexBuffer(&indexBufferView);
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

        CD3DX12_GPU_DESCRIPTOR_HANDLE tex(srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
        tex.Offset(ri->Mat->DiffuseSrvHeapIndex, device->GetCbvSrvUavDescriptorSize());

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
        
        cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}