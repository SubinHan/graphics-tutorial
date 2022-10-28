#include "DxDevice.h"
#include "DxDebug.h"

DxDevice::DxDevice()
{
	Init();
}

void DxDevice::Init()
{
	CreateDevice();
    CreateFence();

}

void DxDevice::CreateDevice()
{
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
    backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
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
