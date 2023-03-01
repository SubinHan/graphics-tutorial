#pragma once

#include "../Common/MainWindow.h"
#include "../Common/MathHelper.h"
#include "../Common/DxUtil.h"
#include "../Common/FrameResource.h"

extern const int gNumFrameResources;

struct RenderItem
{
	RenderItem() = default;

	// World matrix of the shape that describes the object's local space
	// relative to the world space, which defines the position, orientation,
	// and scale of the object in the world.
	DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	MeshGeometry* Geo = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};

class ShapeApp : public MainWindow
{
public:
	ShapeApp(HINSTANCE hInstance);
	ShapeApp(const ShapeApp& rhs) = delete;
	ShapeApp& operator=(const ShapeApp& rhs) = delete;
	~ShapeApp();

	bool Initialize() override;

private:
	void OnResize() override;
	void Update(const GameTimer& gt) override;
	void Draw(const GameTimer& gt) override;
	
	void UpdateCamera(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

	void BuildDescriptorHeaps();
	void BuildConstantBufferViews();
	void BuildRootSignature();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);


public:
	void OnMouseLeftDown(int x, int y, short keyState) override;
	void OnMouseLeftUp(int x, int y, short keyState) override;
	void OnMouseMove(int x, int y, short keyState) override;
	void OnKeyDown(WPARAM windowVirtualKeyCode) override;

private:

	std::vector<std::unique_ptr<FrameResource>> frameResources;
	FrameResource* currFrameResource = nullptr;
	int currFrameResourceIndex = 0;

	ComPtr<ID3D12RootSignature> rootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap> cbvHeap = nullptr;

	ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> geometries;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> shaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> pipelineStateObjects;

	std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> allRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> opaqueRitems;

	PassConstants mainPassCB;

	UINT passCbvOffset = 0;

	bool isWireframe = false;

	DirectX::XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	DirectX::XMFLOAT4X4 mView = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = 1.5f * DirectX::XM_PI;
	float mPhi = 0.2f * DirectX::XM_PI;
	float mRadius = 15.0f;

	POINT mLastMousePos;
};