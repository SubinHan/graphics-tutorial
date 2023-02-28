#pragma once

#include "Common/MainWindow.h"
#include "Common/MathHelper.h"
#include "Common/DxUtil.h"
#include "Common/UploadBuffer.h"

struct Vertex
{
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT4 Color;
};

struct ObjectConstants
{
	DirectX::XMFLOAT4X4 WorldViewProj = MathHelper::Identity4x4();
};

class BoxApp : public MainWindow
{
public:
	BoxApp(HINSTANCE hInstance);
	BoxApp(const BoxApp& rhs) = delete;
	BoxApp& operator=(const BoxApp& rhs) = delete;
	~BoxApp();

	virtual bool Initialize() override;

private:
	virtual void OnResize() override;
	virtual void Update(const GameTimer& gt) override;
	virtual void Draw(const GameTimer& gt) override;

	void BuildDescriptorHeaps();
	void BuildConstantBuffers();
	void BuildRootSignature();
	void BuildShadersAndInputLayout();
	void BuildBoxGeometry();
	void BuildPSO();

public:
	void LeftDown(int x, int y, short keyState) override;
	void LeftUp(int x, int y, short keyState) override;
	void MouseMove(int x, int y, short keyState) override;

private:
	ComPtr<ID3D12RootSignature> rootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap> cbvHeap = nullptr;
	
	std::unique_ptr<UploadBuffer<ObjectConstants>> objectConstantBuffer = nullptr;
	std::unique_ptr<MeshGeometry> boxGeometry = nullptr;

	ComPtr<ID3DBlob> mvsByteCode = nullptr;
	ComPtr<ID3DBlob> mpsByteCode = nullptr;

	std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;
	
	ComPtr<ID3D12PipelineState> pso = nullptr;

	DirectX::XMFLOAT4X4 matrixWorld = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 matrixView = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 matrixProjection = MathHelper::Identity4x4();

	float theta = 1.5f * DirectX::XM_PI;
	float phi = DirectX::XM_PIDIV4;
	float radius = 5.0f;

	POINT lastMousePos;

};