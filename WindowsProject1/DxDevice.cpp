#include "DxDevice.h"
#include "DxDebug.h"

DxDevice::DxDevice()
{
	Init();
}

void DxDevice::Init()
{
	CreateDevice();
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