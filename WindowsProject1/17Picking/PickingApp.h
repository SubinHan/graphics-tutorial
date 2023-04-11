#pragma once

#include "../Common/MainWindow.h"
#include "../Common/MathHelper.h"
#include "../Common/DxUtil.h"
#include "../Common/Camera.h"
#include "FrameResource.h"

extern const int gNumFrameResources;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	DirectX::BoundingBox Bounds;
	std::vector<InstanceData> Instances;

	// DrawIndexedInstanced parameters.
	UINT InstanceCount = 0;
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;

	bool Visible = true;
};

enum class RenderLayer : int
{
	OpaqueFrustumCull = 0,
	OpaqueNonFrustumCull = OpaqueFrustumCull + 1,
	Highlight = OpaqueNonFrustumCull + 1,
	Count
};

class PickingApp : public MainWindow
{
public:
	PickingApp(HINSTANCE hInstance);
	PickingApp(const PickingApp& rhs) = delete;
	PickingApp& operator=(const PickingApp& rhs) = delete;
	~PickingApp();

	bool Initialize() override;

private:
	void OnResize() override;
	void Update(const GameTimer& gt) override;
	void Draw(const GameTimer& gt) override;
	
	void UpdateCamera(const GameTimer& gt);
	void UpdateInstanceBuffer(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void UpdateMaterialBuffer(const GameTimer& gt);

	void LoadTexture(std::wstring filePath, std::string textureName);

	void LoadTextures();
	void BuildRootSignature();
	void BuildDescriptorHeaps();
	void BuildShadersAndInputLayout();
	void BuildMaterials();
	void BuildSkullGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildRenderItems();
	void DrawRenderItemsWithInstancing(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);


public:
	void OnMouseLeftDown(int x, int y, short keyState) override;
	void OnMouseLeftUp(int x, int y, short keyState) override;
	void OnMouseMove(int x, int y, short keyState) override;
	void OnKeyDown(WPARAM windowVirtualKeyCode) override;
	void Pick(int sx, int sy);

private:

	std::vector<std::unique_ptr<FrameResource>> frameResources;
	FrameResource* currFrameResource = nullptr;
	int currFrameResourceIndex = 0;

	ComPtr<ID3D12RootSignature> rootSignature = nullptr;

	ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> geometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> materials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> textures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> shaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> pipelineStateObjects;

	std::vector<D3D12_INPUT_ELEMENT_DESC> defaultInputLayout;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> allRitems;
	RenderItem* pickedRitem;
	int instanceCount;

	DirectX::BoundingFrustum camFrustum;

	// Render items divided by PSO.
	std::vector<RenderItem*> RitemLayer[(int)RenderLayer::Count];

	PassConstants mainPassCB;

	bool isWireframe = false;

	float sunTheta = 1.25f * DirectX::XM_PI;
	float sunPhi = DirectX::XM_PIDIV4;

	POINT lastMousePos;

	Camera camera;
};