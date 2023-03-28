//***************************************************************************************
// Waves.h by Frank Luna (C) 2011 All Rights Reserved.
//
// Performs the calculations for the wave simulation.  After the simulation has been
// updated, the client must copy the current solution into vertex buffers for rendering.
// This class only does the calculations, it does not do any drawing.
//***************************************************************************************

#pragma once

#include "../Common/DxUtil.h"

#include <vector>
#include <DirectXMath.h>
#include "../Common/GameTimer.h"


class Waves
{
public:
    Waves(ID3D12Device* device, int m, int n, float dx, float dt, float speed, float damping);
    Waves(const Waves& rhs) = delete;
    Waves& operator=(const Waves& rhs) = delete;
    ~Waves();

    int RowCount()const;
    int ColumnCount()const;
    int VertexCount()const;
    int TriangleCount()const;
    float Width()const;
    float Depth()const;

    float GetSpatialStep();
    ID3D12Resource* GetSolutionTexture();
    D3D12_GPU_DESCRIPTOR_HANDLE GetSolutionSrvHandleGpu();
    D3D12_CPU_DESCRIPTOR_HANDLE GetSolutionSrvHandleCpu();

    // Returns the solution at the ith grid point.
    const DirectX::XMFLOAT3& Position(int i)const { return mCurrSolution[i]; }

    // Returns the solution normal at the ith grid point.
    const DirectX::XMFLOAT3& Normal(int i)const { return mNormals[i]; }

    // Returns the unit tangent vector at the ith grid point in the local x-axis direction.
    const DirectX::XMFLOAT3& TangentX(int i)const { return mTangentX[i]; }

    void InitWaves(ID3D12GraphicsCommandList* cmdList);
    void Update(float dt);
    void Disturb(int i, int j, float magnitude);
    void Execute(
        ID3D12GraphicsCommandList* cmdList,
        const GameTimer& gt
    );
    void BuildDescriptors(
        CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDescriptor,
        CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuDescriptor,
        UINT descriptorSize);

private:
    void BuildResources();
    void BuildRootSignature();
    void BuildShaders();
    void BuildPSOs();

private:
    ID3D12Device* d3dDevice;

    DXGI_FORMAT format = DXGI_FORMAT_R32_FLOAT;

    int mNumRows = 0;
    int mNumCols = 0;

    int mVertexCount = 0;
    int mTriangleCount = 0;

    // Simulation constants we can precompute.
    float mK1 = 0.0f;
    float mK2 = 0.0f;
    float mK3 = 0.0f;

    float mTimeStep = 0.0f;
    float mSpatialStep = 0.0f;

    std::vector<DirectX::XMFLOAT3> mPrevSolution;
    std::vector<DirectX::XMFLOAT3> mCurrSolution;
    std::vector<DirectX::XMFLOAT3> mNormals;
    std::vector<DirectX::XMFLOAT3> mTangentX;

    CD3DX12_CPU_DESCRIPTOR_HANDLE prevSolCpuSrv;
    CD3DX12_CPU_DESCRIPTOR_HANDLE prevSolCpuUav;
    CD3DX12_CPU_DESCRIPTOR_HANDLE currSolCpuSrv;
    CD3DX12_CPU_DESCRIPTOR_HANDLE currSolCpuUav;

    CD3DX12_GPU_DESCRIPTOR_HANDLE prevSolGpuSrv;
    CD3DX12_GPU_DESCRIPTOR_HANDLE prevSolGpuUav;
    CD3DX12_GPU_DESCRIPTOR_HANDLE currSolGpuSrv;
    CD3DX12_GPU_DESCRIPTOR_HANDLE currSolGpuUav;

    Microsoft::WRL::ComPtr<ID3D12Resource> prevSolution = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> currSolution = nullptr;

    Microsoft::WRL::ComPtr<ID3DBlob> shaderCS = nullptr;
    Microsoft::WRL::ComPtr<ID3DBlob> shaderVS = nullptr;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature = nullptr;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso = nullptr;
};
