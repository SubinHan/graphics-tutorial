#include "LandAndWavesApp.h"
#include "../Common/GeometryGenerator.h"

using namespace DirectX;

const int gNumFrameResources = 3;

LandAndWavesApp::LandAndWavesApp(HINSTANCE hInstance)
    : MainWindow(hInstance)
{
}

LandAndWavesApp::~LandAndWavesApp()
{
    if (device->GetD3DDevice() != nullptr)
        device->FlushCommandQueue();
}

bool LandAndWavesApp::Initialize()
{
    if (!MainWindow::Initialize())
        return false;

    auto commandList = device->GetCommandList();
    auto commandListAllocator = device->GetCommandListAllocator();
    auto commandQueue = device->GetCommandQueue();

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(commandList->Reset(commandListAllocator.Get(), nullptr));

    waves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);

    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildLandGeometry();
    BuildWavesGeometry();
    BuildRenderItems();
    BuildFrameResources();
    BuildDescriptorHeaps();
    BuildConstantBufferViews();
    BuildPSOs();

    // Execute the initialization commands.
    ThrowIfFailed(commandList->Close());
    ID3D12CommandList* cmdsLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    device->FlushCommandQueue();

    return true;
}

void LandAndWavesApp::OnResize()
{
    MainWindow::OnResize();

    // The window resized, so update the aspect ratio and recompute the projection matrix.
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);
}

void LandAndWavesApp::Update(const GameTimer& gt)
{
    //OnKeyboardInput(gt);
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
    UpdateMainPassCB(gt);
    UpdateWaves(gt);
}


void LandAndWavesApp::Draw(const GameTimer& gt)
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

    ID3D12DescriptorHeap* descriptorHeaps[] = { cbvHeap.Get() };
    commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    commandList->SetGraphicsRootSignature(rootSignature.Get());

    auto passCB = currFrameResource->PassCB->Resource();
    commandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

    DrawRenderItems(commandList.Get(), opaqueRitems);

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

void LandAndWavesApp::OnMouseLeftDown(int x, int y, short keyState)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(m_hwnd);
}

void LandAndWavesApp::OnMouseLeftUp(int x, int y, short keyState)
{
    ReleaseCapture();
}

void LandAndWavesApp::OnMouseMove(int x, int y, short keyState)
{
    if ((keyState & MK_LBUTTON) != 0)
    {
        // Make each pixel correspond to a quarter of a degree.
        float dx = DirectX::XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
        float dy = DirectX::XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

        // Update angles based on input to orbit camera around box.
        mTheta += dx;
        mPhi += dy;

        // Restrict the angle mPhi.
        mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
    }
    else if ((keyState & MK_RBUTTON) != 0)
    {
        // Make each pixel correspond to 0.2 unit in the scene.
        float dx = 0.05f * static_cast<float>(x - mLastMousePos.x);
        float dy = 0.05f * static_cast<float>(y - mLastMousePos.y);

        // Update the camera radius based on input.
        mRadius += dx - dy;

        // Restrict the radius.
        mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void LandAndWavesApp::OnKeyDown(WPARAM windowVirtualKeyCode)
{
    if (windowVirtualKeyCode != 'I')
        return;

    isWireframe = !isWireframe;
}

void LandAndWavesApp::UpdateCamera(const GameTimer& gt)
{
    // Convert Spherical to Cartesian coordinates.
    mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
    mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
    mEyePos.y = mRadius * cosf(mPhi);

    // Build the view matrix.
    XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
    XMVECTOR target = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
    XMStoreFloat4x4(&mView, view);
}

void LandAndWavesApp::UpdateObjectCBs(const GameTimer& gt)
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

            currObjectCB->CopyData(e->ObjCBIndex, objConstants);

            // Next FrameResource need to be updated too.
            e->NumFramesDirty--;
        }
    }
}

void LandAndWavesApp::UpdateMainPassCB(const GameTimer& gt)
{
    XMMATRIX view = XMLoadFloat4x4(&mView);
    XMMATRIX proj = XMLoadFloat4x4(&mProj);

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
    mainPassCB.EyePosW = mEyePos;

    auto clientWidth = device->GetClientWidth();
    auto clientHeight = device->GetClientHeight();

    mainPassCB.RenderTargetSize = XMFLOAT2((float)clientWidth, (float)clientHeight);
    mainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / clientWidth, 1.0f / clientHeight);
    mainPassCB.NearZ = 1.0f;
    mainPassCB.FarZ = 1000.0f;
    mainPassCB.TotalTime = gt.TotalTime();
    mainPassCB.DeltaTime = gt.DeltaTime();

    auto currPassCB = currFrameResource->PassCB.get();
    currPassCB->CopyData(0, mainPassCB);
}

void LandAndWavesApp::UpdateWaves(const GameTimer& gt)
{
    static float t_base = 0.0f;
    if(timer.TotalTime() - t_base >= 0.25f)
    {
        t_base += 0.25f;

        int i = MathHelper::Rand(4, waves->RowCount() - 5);
        int j = MathHelper::Rand(4, waves->ColumnCount() - 5);

        float r = MathHelper::RandF(0.2f, 0.5f);

        waves->Disturb(i, j, r);
    }

    waves->Update(gt.DeltaTime());

    auto currWavesVB = currFrameResource->WavesVB.get();
    for(int i = 0; i < waves->VertexCount(); ++i)
    {
        Vertex v;

        v.Pos = waves->Position(i);
        v.Color = XMFLOAT4(DirectX::Colors::Blue);

        currWavesVB->CopyData(i, v);
    }

    wavesRitem->Geo->VertexBufferGPU = currWavesVB->Resource();
}


void LandAndWavesApp::BuildDescriptorHeaps()
{
    UINT objCount = (UINT)opaqueRitems.size();

    // Need a CBV descriptor for each object for each frame resource,
    // +1 for the perPass CBV for each frame resource.
    UINT numDescriptors = (objCount + 1) * gNumFrameResources;

    // Save an offset to the start of the pass CBVs.  These are the last 3 descriptors.
    passCbvOffset = objCount * gNumFrameResources;

    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
    cbvHeapDesc.NumDescriptors = numDescriptors;
    cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    cbvHeapDesc.NodeMask = 0;
    ThrowIfFailed(device->GetD3DDevice()->CreateDescriptorHeap(&cbvHeapDesc,
        IID_PPV_ARGS(&cbvHeap)));
}

void LandAndWavesApp::BuildConstantBufferViews()
{
    UINT objCBByteSize = DxUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    UINT objCount = (UINT)opaqueRitems.size();

    // Need a CBV descriptor for each object for each frame resource.
    for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
    {
        auto objectCB = frameResources[frameIndex]->ObjectCB->Resource();
        for (UINT i = 0; i < objCount; ++i)
        {
            D3D12_GPU_VIRTUAL_ADDRESS cbAddress = objectCB->GetGPUVirtualAddress();

            // Offset to the ith object constant buffer in the buffer.
            cbAddress += i * objCBByteSize;

            // Offset to the object cbv in the descriptor heap.
            int heapIndex = frameIndex * objCount + i;
            auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(cbvHeap->GetCPUDescriptorHandleForHeapStart());
            handle.Offset(heapIndex, device->GetCbvSrvUavDescriptorSize());

            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
            cbvDesc.BufferLocation = cbAddress;
            cbvDesc.SizeInBytes = objCBByteSize;

            device->GetD3DDevice()->CreateConstantBufferView(&cbvDesc, handle);
        }
    }

    UINT passCBByteSize = DxUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

    // Last three descriptors are the pass CBVs for each frame resource.
    for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
    {
        auto passCB = frameResources[frameIndex]->PassCB->Resource();
        D3D12_GPU_VIRTUAL_ADDRESS cbAddress = passCB->GetGPUVirtualAddress();

        // Offset to the pass cbv in the descriptor heap.
        int heapIndex = passCbvOffset + frameIndex;
        auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(cbvHeap->GetCPUDescriptorHandleForHeapStart());
        handle.Offset(heapIndex, device->GetCbvSrvUavDescriptorSize());

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
        cbvDesc.BufferLocation = cbAddress;
        cbvDesc.SizeInBytes = passCBByteSize;

        device->GetD3DDevice()->CreateConstantBufferView(&cbvDesc, handle);
    }
}

void LandAndWavesApp::BuildRootSignature()
{
    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[2];

    // Create root CBVs.
    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsConstantBufferView(1);

    // A root signature is an array of root parameters.
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 0, nullptr,
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


void LandAndWavesApp::BuildShadersAndInputLayout()
{
    shaders["standardVS"] = DxUtil::CompileShader(L"07\\Shaders\\color.hlsl", nullptr, "VS", "vs_5_1");
    shaders["opaquePS"] = DxUtil::CompileShader(L"07\\Shaders\\color.hlsl", nullptr, "PS", "ps_5_1");

    inputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
}

float LandAndWavesApp::GetHillsHeight(float x, float z) const
{
    return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
}

void LandAndWavesApp::BuildLandGeometry()
{
    GeometryGenerator geoGen;
    GeometryGenerator::MeshData grid = geoGen.CreateGrid(160.0f, 160.0f, 50, 50);

    std::vector<Vertex> vertices(grid.Vertices.size());
    for(size_t i = 0; i < grid.Vertices.size(); i++)
    {
        auto& p = grid.Vertices[i].Position;
        vertices[i].Pos = p;
        vertices[i].Pos.y = GetHillsHeight(p.x, p.z);

        if(vertices[i].Pos.y < SAND_HEIGHT)
        {
            vertices[i].Color = XMFLOAT4(1.0f, 0.96f, 0.62f, 1.0f);
            continue;
        }
        if(vertices[i].Pos.y < LAND_LOW_HEIGHT)
        {
            vertices[i].Color = XMFLOAT4(0.48f, 0.77f, 0.46f, 1.0f);
            continue;
        }
        if (vertices[i].Pos.y < LAND_MIDDLE_HEIGHT)
        {
            vertices[i].Color = XMFLOAT4(0.1f, 0.48f, 0.19f, 1.0f);
            continue;
        }
        if (vertices[i].Pos.y < LAND_HIGH_HEIGHT)
        {
            vertices[i].Color = XMFLOAT4(0.45f, 0.39f, 0.34f, 1.0f);
            continue;
        }
        vertices[i].Color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    const UINT vbByteSize = static_cast<UINT>(vertices.size()) * sizeof(Vertex);

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
        commandList.Get(), vertices.data(), vbByteSize,
        geo->VertexBufferUploader);

    geo->IndexBufferGPU = DxUtil::CreateDefaultBuffer(device->GetD3DDevice().Get(),
        commandList.Get(), indices.data(), ibByteSize,
        geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = static_cast<UINT>(indices.size());
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    geo->DrawArgs["grid"] = submesh;

    geometries["landGeo"] = std::move(geo);
}

void LandAndWavesApp::BuildWavesGeometry()
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



void LandAndWavesApp::BuildPSOs()
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
    opaquePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    opaquePsoDesc.SampleMask = UINT_MAX;
    opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    opaquePsoDesc.NumRenderTargets = 1;
    opaquePsoDesc.RTVFormats[0] = device->GetBackBufferFormat();
    opaquePsoDesc.SampleDesc.Count = device->GetMsaaState() ? 4 : 1;
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
}


void LandAndWavesApp::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i)
    {
        frameResources.push_back(std::make_unique<FrameResource>(device->GetD3DDevice().Get(),
            1, static_cast<UINT>(allRitems.size()), waves->VertexCount()));
    }
}

void LandAndWavesApp::BuildRenderItems()
{
    auto gridRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&gridRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
    gridRitem->ObjCBIndex = 0;
    gridRitem->Geo = geometries["landGeo"].get();
    gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
    gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
    allRitems.push_back(std::move(gridRitem));

    auto wavesRitem = std::make_unique<RenderItem>();
    wavesRitem->World = MathHelper::Identity4x4();
    wavesRitem->ObjCBIndex = 0;
    wavesRitem->Geo = geometries["waterGeo"].get();
    wavesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    wavesRitem->IndexCount = wavesRitem->Geo->DrawArgs["grid"].IndexCount;
    wavesRitem->StartIndexLocation = wavesRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    wavesRitem->BaseVertexLocation = wavesRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

    this->wavesRitem = wavesRitem.get();
    allRitems.push_back(std::move(wavesRitem));

    // All the render items are opaque.
    for (auto& e : allRitems)
        opaqueRitems.push_back(e.get());
}

void LandAndWavesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = DxUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

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

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress();
        objCBAddress += ri->ObjCBIndex * objCBByteSize;
        cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}
