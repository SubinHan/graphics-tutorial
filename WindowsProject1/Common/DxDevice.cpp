#include "DxDevice.h"
#include "DxDebug.h"
#include <DirectXColors.h>

DxDevice::DxDevice(HWND window)
{
    mainWindow = window;

    RECT windowRect;
    if (GetWindowRect(window, &windowRect))
    {
        clientWidth = windowRect.right - windowRect.left;
        clientHeight = windowRect.bottom - windowRect.top;
    }
    clientRefreshRate = 165;

	Init();

    assert(pD3dDevice);
    assert(swapChain);
    assert(commandListAllocator);
}

void DxDevice::ResetCommandList()
{
    ThrowIfFailed(commandListAllocator->Reset());
    ThrowIfFailed(commandList->Reset(commandListAllocator.Get(), nullptr));

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    commandList->ResourceBarrier(1, &barrier);

    commandList->RSSetViewports(1, &screenViewport);
    commandList->RSSetScissorRects(1, &scissorRect);


    commandList->ClearRenderTargetView(
        CurrentBackBufferView(),
        DirectX::Colors::Black, 0, nullptr);
    commandList->ClearDepthStencilView(
        DepthStencilView(), 
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        1.0f,
        0,
        0,
        nullptr);

    auto currentBackBufferView = CurrentBackBufferView();
    auto depthStencilView = DepthStencilView();

    commandList->OMSetRenderTargets(1, &currentBackBufferView,
        true, &depthStencilView);

    auto barrier2 = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT
    );

    commandList->ResourceBarrier(
        1,
        &barrier2
    );

    ThrowIfFailed(commandList->Close());
    ID3D12CommandList* commandLists[] = { commandList.Get() }; 
    commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

    ThrowIfFailed(swapChain->Present(0, 0));
    currentBackBuffer = (currentBackBuffer + 1) % SWAP_CHAIN_BUFFER_COUNT;

    FlushCommandQueue();

}

ID3D12Resource* DxDevice::CurrentBackBuffer()
{
    return swapChainBuffer[currentBackBuffer].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE DxDevice::CurrentBackBufferView()
{
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        rtvHeap->GetCPUDescriptorHandleForHeapStart(),
        currentBackBuffer,
        rtvDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE DxDevice::DepthStencilView()
{
    return dsvHeap->GetCPUDescriptorHandleForHeapStart();
}

void DxDevice::FlushCommandQueue()
{
    currentFence++;

    ThrowIfFailed(commandQueue->Signal(fence.Get(), currentFence));

    if (fence->GetCompletedValue() < currentFence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, 
            FALSE, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(fence->SetEventOnCompletion(currentFence, eventHandle));

        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

}

void DxDevice::ResetAllSwapChainBuffers()
{
    for (int i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++)
    {
        swapChainBuffer[i].Reset();
    }
}

void DxDevice::ResetDepthStencilBuffer()
{
    depthStencilBuffer.Reset();
}

void DxDevice::ResizeBuffers()
{
    ThrowIfFailed(swapChain->ResizeBuffers(
        SWAP_CHAIN_BUFFER_COUNT,
        clientWidth,
        clientHeight,
        backBufferFormat,
        DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH
    ));

    currentBackBuffer = 0;

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++)
    {
        ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&swapChainBuffer[i])));
        pD3dDevice->CreateRenderTargetView(swapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
        rtvHeapHandle.Offset(1, rtvDescriptorSize);
    }
}

ComPtr<ID3D12Device>& DxDevice::GetD3DDevice()
{
    return pD3dDevice;
}

ComPtr<ID3D12CommandQueue>& DxDevice::GetCommandQueue()
{
    return commandQueue;
}

ComPtr<ID3D12CommandAllocator>& DxDevice::GetCommandListAllocator()
{
    return commandListAllocator;
}

ComPtr<ID3D12GraphicsCommandList>& DxDevice::GetCommandList()
{
    return commandList;
}

ComPtr<IDXGISwapChain>& DxDevice::GetSwapChain()
{
    return swapChain;
}

ID3D12DescriptorHeap* DxDevice::GetRtvHeap()
{
    return rtvHeap.Get();
}

ID3D12DescriptorHeap* DxDevice::GetDsvHeap()
{
    return dsvHeap.Get();
}

void DxDevice::RSSetViewports(UINT numViewports)
{
    commandList->RSSetViewports(numViewports, &screenViewport);
}

void DxDevice::RSSetScissorRects(UINT numRects)
{
    commandList->RSSetScissorRects(numRects, &scissorRect);
}

void DxDevice::SwapBuffers()
{
    ThrowIfFailed(swapChain->Present(0, 0));
    currentBackBuffer = (currentBackBuffer + 1) % SWAP_CHAIN_BUFFER_COUNT;
}

DXGI_FORMAT DxDevice::GetBackBufferFormat()
{
    return backBufferFormat;
}

DXGI_FORMAT DxDevice::GetDepthStencilFormat()
{
    return depthStencilFormat;
}

bool DxDevice::GetMsaaState()
{
    return msaaState;
}

UINT DxDevice::GetMsaaQuality()
{
    return msaaQuality;
}

void DxDevice::Enable4xMsaa()
{
    msaaState = true;
}

UINT DxDevice::GetClientWidth()
{
    return clientWidth;
}

UINT DxDevice::GetClientHeight()
{
    return clientHeight;
}

void DxDevice::SetClientWidth(UINT width)
{
    clientWidth = width;
}

void DxDevice::SetClientHeight(UINT height)
{
    clientHeight = height;
}

UINT DxDevice::GetSwapChainBufferCount()
{
    return SWAP_CHAIN_BUFFER_COUNT;
}

ComPtr<ID3D12Fence> DxDevice::GetFence()
{
    return fence;
}

D3D12_VIEWPORT& DxDevice::GetScreenViewport()
{
    return screenViewport;
}

tagRECT& DxDevice::GetScissorRect()
{
    return scissorRect;
}

UINT DxDevice::GetRtvDescriptorSize()
{
    return rtvDescriptorSize;
}

UINT DxDevice::GetCbvSrvUavDescriptorSize()
{
    return cbvDescriptorSize;
}

UINT DxDevice::GetDsvDescriptorSize()
{
    return dsvDescriptorSize;
}

UINT DxDevice::GetCurrentFence()
{
    return currentFence;
}

UINT DxDevice::IncreaseFence()
{
    return ++currentFence;
}


void DxDevice::Init()
{
	CreateDevice();
    CreateFence();
    CheckMsaa();
    CreateCommandQueueAndList();
    CreateSwapChain();
    CreateRtvAndDsvDescriptorHeaps();
    CreateRenderTargetView();
    CreateDepthStencilView();
}

void DxDevice::CreateDevice()
{
#if defined(DEBUG) || defined(_DEBUG) 
    // Enable the D3D12 debug layer.
    {
        ComPtr<ID3D12Debug> debugController;
        ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
        debugController->EnableDebugLayer();
    }
#endif

    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&pDxgiFactory)));

    HRESULT hardwareResult = D3D12CreateDevice(
        nullptr,
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&pD3dDevice)
    );
}

void DxDevice::CreateFence()
{
    ThrowIfFailed(pD3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    
    rtvDescriptorSize = pD3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    dsvDescriptorSize = pD3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    cbvDescriptorSize = pD3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void DxDevice::CheckMsaa()
{
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels = {};
    msQualityLevels.Format = backBufferFormat;
    msQualityLevels.SampleCount = 4;
    msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
    msQualityLevels.NumQualityLevels = 0;
    ThrowIfFailed(pD3dDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msQualityLevels, sizeof(msQualityLevels)));

    msaaQuality = msQualityLevels.NumQualityLevels;
    assert(msaaQuality > 0 && "Unexpected MSAA quality level.");
}

void DxDevice::CreateCommandQueueAndList()
{
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    ThrowIfFailed(pD3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));
    ThrowIfFailed(pD3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(commandListAllocator.GetAddressOf())));
    ThrowIfFailed(pD3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandListAllocator.Get(), nullptr, IID_PPV_ARGS(commandList.GetAddressOf())));
    commandList->Close();
}

void DxDevice::CreateSwapChain()
{
    swapChain.Reset();

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferDesc.Width = clientWidth;
    sd.BufferDesc.Height = clientHeight;
    sd.BufferDesc.RefreshRate.Numerator = clientRefreshRate;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferDesc.Format = backBufferFormat;
    sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    sd.SampleDesc.Count = msaaState ? 4 : 1;
    sd.SampleDesc.Quality = msaaState ? (msaaQuality - 1) : 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = SWAP_CHAIN_BUFFER_COUNT;
    sd.OutputWindow = mainWindow;
    sd.Windowed = true;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    ThrowIfFailed(pDxgiFactory->CreateSwapChain(commandQueue.Get(), &sd, swapChain.GetAddressOf()));
}

void DxDevice::CreateRtvAndDsvDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = SWAP_CHAIN_BUFFER_COUNT + 6;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvHeapDesc.NodeMask = 0;
    ThrowIfFailed(pD3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(rtvHeap.GetAddressOf())));

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 2;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsvHeapDesc.NodeMask = 0;
    ThrowIfFailed(pD3dDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(dsvHeap.GetAddressOf())));
}

void DxDevice::CreateRenderTargetView()
{
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++)
    {
        ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&swapChainBuffer[i])));
        pD3dDevice->CreateRenderTargetView(swapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
        
        // rtvHeapHandle.Offset(1, rtvDescriptorSize) of a type CD3DX12_CPU_DESCRIPTOR_HANDLE
        rtvHeapHandle.ptr = SIZE_T(INT64(rtvHeapHandle.ptr) + INT64(1) * INT64(rtvDescriptorSize));
    }
}

void DxDevice::CreateDepthStencilView()
{
    D3D12_HEAP_PROPERTIES heapProperties;
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC depthStencilDesc = {};
    depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthStencilDesc.Alignment = 0;
    depthStencilDesc.Width = clientWidth;
    depthStencilDesc.Height = clientHeight;
    depthStencilDesc.DepthOrArraySize = 1;
    depthStencilDesc.MipLevels = 1;
    depthStencilDesc.Format = depthStencilFormat;
    depthStencilDesc.SampleDesc.Count = msaaState ? 4 : 1;
    depthStencilDesc.SampleDesc.Quality = msaaState ? (msaaQuality - 1) : 0;
    depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE optClear = {};
    optClear.Format = depthStencilFormat;
    optClear.DepthStencil.Depth = 1.0f;
    optClear.DepthStencil.Stencil = 0;

    ThrowIfFailed(pD3dDevice->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &depthStencilDesc,
        D3D12_RESOURCE_STATE_COMMON,
        &optClear,
        IID_PPV_ARGS(depthStencilBuffer.GetAddressOf())
    ));

    pD3dDevice->CreateDepthStencilView(depthStencilBuffer.Get(), nullptr, dsvHeap->GetCPUDescriptorHandleForHeapStart());

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        depthStencilBuffer.Get(),
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_DEPTH_WRITE
    );

    commandList->ResourceBarrier(
        1,
        &barrier
    );
}

void DxDevice::InitScreenViewport()
{
    screenViewport.TopLeftX = 0.0f;
    screenViewport.TopLeftY = 0.0f;
    screenViewport.Width = static_cast<float>(clientWidth);
    screenViewport.Height = static_cast<float>(clientHeight);
    screenViewport.MinDepth = 0.0f;
    screenViewport.MaxDepth = 1.0f;
}

void DxDevice::InitScissorRect()
{
    scissorRect = { 0, 0, static_cast<long>(clientWidth), static_cast<long>(clientHeight) };
}

