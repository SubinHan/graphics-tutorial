#include "TexShapeApp.h"
#include "../Common/GeometryGenerator.h"

using namespace DirectX;

const int gNumFrameResources = 3;

TexShapeApp::TexShapeApp(HINSTANCE hInstance)
    : MainWindow(hInstance)
{
}

TexShapeApp::~TexShapeApp()
{
    if (device->GetD3DDevice() != nullptr)
        device->FlushCommandQueue();
}

void TexShapeApp::LoadTextures()
{
    auto stoneTex = std::make_unique<Texture>();
    stoneTex->Name = "stoneTex";
    stoneTex->Filename = L"Textures/stone.dds";

    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(device->GetD3DDevice().Get(),
        device->GetCommandList().Get(), stoneTex->Filename.c_str(),
        stoneTex->Resource, stoneTex->UploadHeap));

    textures[stoneTex->Name] = std::move(stoneTex);

    auto tileTex = std::make_unique<Texture>();
    tileTex->Name = "tileTex";
    tileTex->Filename = L"Textures/tile.dds";

    ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(device->GetD3DDevice().Get(),
        device->GetCommandList().Get(), tileTex->Filename.c_str(),
        tileTex->Resource, tileTex->UploadHeap));

    textures[tileTex->Name] = std::move(tileTex);
}

void TexShapeApp::BuildMaterials()
{
    auto stoneMat = std::make_unique<Material>();

    stoneMat->Name = "stone";
    stoneMat->MatCBIndex = 0;
    stoneMat->DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    stoneMat->FresnelR0 = { 0.3f, 0.3f, 0.3f };
    stoneMat->DiffuseSrvHeapIndex = 0;
    stoneMat->Roughness = 0.01f;

    materials[stoneMat->Name] = std::move(stoneMat);

    auto tileMat = std::make_unique<Material>();

    tileMat->Name = "tile";
    tileMat->MatCBIndex = 0;
    tileMat->DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    tileMat->FresnelR0 = { 0.3f, 0.3f, 0.3f };
    tileMat->DiffuseSrvHeapIndex = 1;
    tileMat->Roughness = 0.01f;

    materials[tileMat->Name] = std::move(tileMat);
}

void TexShapeApp::BuildShaderResourceViews()
{
    int textureIndex = 0;
    for (auto& each : textures)
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(srvCbvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
        hDescriptor.Offset(textureSrvOffset + textureIndex++, device->GetCbvSrvUavDescriptorSize());

        auto texture = each.second.get();
        auto textureResource = texture->Resource;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = textureResource->GetDesc().Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = textureResource->GetDesc().MipLevels;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        device->GetD3DDevice()->CreateShaderResourceView(
            textureResource.Get(), &srvDesc, hDescriptor);
    }
}

bool TexShapeApp::Initialize()
{
    if (!MainWindow::Initialize())
        return false;

    auto commandList = device->GetCommandList();
    auto commandListAllocator = device->GetCommandListAllocator();
    auto commandQueue = device->GetCommandQueue();

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(commandList->Reset(commandListAllocator.Get(), nullptr));

    LoadTextures();
    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildMaterials();
    BuildShapeGeometry();
    BuildRenderItems();
    BuildDescriptorHeaps();
    BuildFrameResources();
    BuildConstantBufferViews();
    BuildShaderResourceViews();
    BuildPSOs();

    // Execute the initialization commands.
    ThrowIfFailed(commandList->Close());
    ID3D12CommandList* cmdsLists[] = { commandList.Get() };
    commandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    device->FlushCommandQueue();

    return true;
}

void TexShapeApp::OnResize()
{
    MainWindow::OnResize();

    // The window resized, so update the aspect ratio and recompute the projection matrix.
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&proj, P);
}

void TexShapeApp::UpdateMaterialCBs(const GameTimer& gt)
{
    auto matCB = currFrameResource->MaterialCB.get();

    for(auto& each : materials)
    {
        auto mat = each.second.get();

        if (mat->NumFramesDirty <= 0)
            continue;

        MaterialConstants matConstants;
        matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
        matConstants.MatTransform = mat->MatTransform;
        matConstants.FresnelR0 = mat->FresnelR0;
        matConstants.Roughness = mat->Roughness;

        matCB->CopyData(mat->MatCBIndex, matConstants);

        mat->NumFramesDirty--;
    }
}

void TexShapeApp::Update(const GameTimer& gt)
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
    UpdateMaterialCBs(gt);
    UpdateMainPassCB(gt);
}


void TexShapeApp::Draw(const GameTimer& gt)
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

    ID3D12DescriptorHeap* descriptorHeaps[] = { srvCbvDescriptorHeap.Get()};
    commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    commandList->SetGraphicsRootSignature(rootSignature.Get());

    int passCbvIndex = passCbvOffset + currFrameResourceIndex;
    auto passCbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(srvCbvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
    passCbvHandle.Offset(passCbvIndex, device->GetCbvSrvUavDescriptorSize());
    commandList->SetGraphicsRootDescriptorTable(2, passCbvHandle);

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

void TexShapeApp::OnMouseLeftDown(int x, int y, short keyState)
{
    lastMousePos.x = x;
    lastMousePos.y = y;

    SetCapture(m_hwnd);
}

void TexShapeApp::OnMouseLeftUp(int x, int y, short keyState)
{
    ReleaseCapture();
}

void TexShapeApp::OnMouseMove(int x, int y, short keyState)
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

void TexShapeApp::OnKeyDown(WPARAM windowVirtualKeyCode)
{
    if (windowVirtualKeyCode != 'I')
        return;

    isWireframe = !isWireframe;
}

void TexShapeApp::UpdateCamera(const GameTimer& gt)
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

void TexShapeApp::UpdateObjectCBs(const GameTimer& gt)
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
            XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

            currObjectCB->CopyData(e->ObjCBIndex, objConstants);

            // Next FrameResource need to be updated too.
            e->NumFramesDirty--;
        }
    }
}

void TexShapeApp::UpdateMainPassCB(const GameTimer& gt)
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
    mainPassCB.Lights[0].Strength = { 1.0f, 1.0f, 0.9f };

    auto currPassCB = currFrameResource->PassCB.get();
    currPassCB->CopyData(0, mainPassCB);
}

void TexShapeApp::BuildDescriptorHeaps()
{
    UINT texCount = static_cast<UINT>(textures.size());
    UINT objCount = static_cast<UINT>(opaqueRitems.size());
    UINT matCount = static_cast<UINT>(materials.size());

    //--objs--mats--...--objs--mats--mainpass--...--mainpass--textures//

    // Need a CBV descriptor for each object for each frame resource,
    // +1 for the perPass CBV for each frame resource.
    UINT numDescriptors =
        (objCount + matCount + 1) * gNumFrameResources + texCount;

    // Save an offset to the start of the pass CBVs.  These are the last 3 descriptors.
    passCbvOffset = (objCount + matCount) * gNumFrameResources;
    textureSrvOffset = passCbvOffset + gNumFrameResources;

    D3D12_DESCRIPTOR_HEAP_DESC cbvSrvHeapDesc = {};
    cbvSrvHeapDesc.NumDescriptors = numDescriptors;
    cbvSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    cbvSrvHeapDesc.NodeMask = 0;
    ThrowIfFailed(device->GetD3DDevice()->CreateDescriptorHeap(&cvSrvHeapDesc, IID_PPV_ARGS(&srvCbvDescriptorHeap)));
}

void TexShapeApp::BuildConstantBufferViews()
{
    UINT objCBByteSize = DxUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT matCBByteSize = DxUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

    UINT objCount = static_cast<UINT>(opaqueRitems.size());
    UINT matCount = static_cast<UINT>(materials.size());

    const int frameIndexSize = static_cast<const int>(objCount + matCount);

    // Need a CBV descriptor for each object for each frame resource.
    for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
    {
        auto objectCB = frameResources[frameIndex]->ObjectCB->Resource();
        auto matCB = frameResources[frameIndex]->MaterialCB->Resource();
        for (UINT i = 0; i < objCount; ++i)
        {
            D3D12_GPU_VIRTUAL_ADDRESS cbAddress = objectCB->GetGPUVirtualAddress();

            // Offset to the ith object constant buffer in the buffer.
            cbAddress += i * objCBByteSize;

            // Offset to the object cbv in the descriptor heap.
            int heapIndex = frameIndex * frameIndexSize + i;
            auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(srvCbvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
            handle.Offset(heapIndex, device->GetCbvSrvUavDescriptorSize());

            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
            cbvDesc.BufferLocation = cbAddress;
            cbvDesc.SizeInBytes = objCBByteSize;

            device->GetD3DDevice()->CreateConstantBufferView(&cbvDesc, handle);
        }

        for (UINT i = 0; i < matCount; ++i)
        {
            D3D12_GPU_VIRTUAL_ADDRESS cbAddress = matCB->GetGPUVirtualAddress();

            // Offset to the ith object constant buffer in the buffer.
            cbAddress += i * matCBByteSize;

            // Offset to the object cbv in the descriptor heap.
            int heapIndex = frameIndex * frameIndexSize + objCount + i;
            auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(srvCbvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
            handle.Offset(heapIndex, device->GetCbvSrvUavDescriptorSize());

            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
            cbvDesc.BufferLocation = cbAddress;
            cbvDesc.SizeInBytes = matCBByteSize;

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
        auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(srvCbvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
        handle.Offset(heapIndex, device->GetCbvSrvUavDescriptorSize());

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
        cbvDesc.BufferLocation = cbAddress;
        cbvDesc.SizeInBytes = passCBByteSize;

        device->GetD3DDevice()->CreateConstantBufferView(&cbvDesc, handle);
    }
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> TexShapeApp::GetStaticSamplers()
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

void TexShapeApp::BuildRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE srvTex;
    srvTex.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    CD3DX12_DESCRIPTOR_RANGE cbvPerObject;
    cbvPerObject.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

    CD3DX12_DESCRIPTOR_RANGE cbvPass;
    cbvPass.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);

    CD3DX12_DESCRIPTOR_RANGE cbvMat;
    cbvMat.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 2);

    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[4];

    // Create root CBVs.
    slotRootParameter[0].InitAsDescriptorTable(1, &srvTex, D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[1].InitAsDescriptorTable(1, &cbvPerObject);
    slotRootParameter[2].InitAsDescriptorTable(1, &cbvPass);
    slotRootParameter[3].InitAsDescriptorTable(1, &cbvMat);

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


void TexShapeApp::BuildShadersAndInputLayout()
{
    shaders["standardVS"] = DxUtil::CompileShader(L"09TexShape\\Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
    shaders["opaquePS"] = DxUtil::CompileShader(L"09TexShape\\Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");

    inputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
}

void TexShapeApp::BuildShapeGeometry()
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

void TexShapeApp::BuildPSOs()
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
}


void TexShapeApp::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i)
    {
        frameResources.push_back(std::make_unique<FrameResource>(device->GetD3DDevice().Get(),
            1, static_cast<UINT>(allRitems.size()), static_cast<UINT>(materials.size())));
    }
}

void TexShapeApp::BuildRenderItems()
{
    auto boxRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
    boxRitem->ObjCBIndex = 0;
    boxRitem->Geo = geometries["shapeGeo"].get();
    boxRitem->Mat = materials["stone"].get();
    boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
    boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
    boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
    boxRitem->MatCBIndex = 0;
    allRitems.push_back(std::move(boxRitem));

    auto gridRitem = std::make_unique<RenderItem>();
    gridRitem->World = MathHelper::Identity4x4();
    gridRitem->ObjCBIndex = 1;
    gridRitem->Geo = geometries["shapeGeo"].get();
    gridRitem->Mat = materials["tile"].get();
    gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
    gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
    gridRitem->MatCBIndex = 0;
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixIdentity() * 5.0f);
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
        leftCylRitem->Geo = geometries["shapeGeo"].get();
        leftCylRitem->Mat = materials["stone"].get();
        leftCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
        leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
        leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
        leftCylRitem->MatCBIndex = 0;

        XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
        rightCylRitem->ObjCBIndex = objCBIndex++;
        rightCylRitem->Geo = geometries["shapeGeo"].get();
        rightCylRitem->Mat = materials["stone"].get();
        rightCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
        rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
        rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;
        rightCylRitem->MatCBIndex = 0;

        XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
        leftSphereRitem->ObjCBIndex = objCBIndex++;
        leftSphereRitem->Geo = geometries["shapeGeo"].get();
        leftSphereRitem->Mat = materials["stone"].get();
        leftSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
        leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
        leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
        leftSphereRitem->MatCBIndex = 0;

        XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
        rightSphereRitem->ObjCBIndex = objCBIndex++;
        rightSphereRitem->Geo = geometries["shapeGeo"].get();
        rightSphereRitem->Mat = materials["stone"].get();
        rightSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
        rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
        rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
        rightSphereRitem->MatCBIndex = 0;

        allRitems.push_back(std::move(leftCylRitem));
        allRitems.push_back(std::move(rightCylRitem));
        allRitems.push_back(std::move(leftSphereRitem));
        allRitems.push_back(std::move(rightSphereRitem));
    }

    // All the render items are opaque.
    for (auto& e : allRitems)
        opaqueRitems.push_back(e.get());
}

void TexShapeApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
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

        CD3DX12_GPU_DESCRIPTOR_HANDLE tex(srvCbvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
        tex.Offset(textureSrvOffset + ri->Mat->DiffuseSrvHeapIndex, device->GetCbvSrvUavDescriptorSize());

        cmdList->SetGraphicsRootDescriptorTable(0, tex);

        const UINT frameSize = ritems.size() + materials.size();

        // Offset to the CBV in the descriptor heap for this object and for this frame resource.
        UINT objIndex = currFrameResourceIndex * static_cast<UINT>(frameSize)
    		+ ri->ObjCBIndex;
        auto objHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(srvCbvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
        objHandle.Offset(objIndex, device->GetCbvSrvUavDescriptorSize());

        cmdList->SetGraphicsRootDescriptorTable(1, objHandle);

        UINT matIndex = currFrameResourceIndex * static_cast<UINT>(frameSize)
            + ritems.size()
    		+ ri->MatCBIndex;
        auto matHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(srvCbvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
        matHandle.Offset(matIndex, device->GetCbvSrvUavDescriptorSize());

        cmdList->SetGraphicsRootDescriptorTable(3, matHandle);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}