#pragma once

#include "../Common/MainWindow.h"
#include "../Common/MathHelper.h"
#include "../Common/DxUtil.h"
#include "../Common/Camera.h"
#include "../Common/InstancedRenderItem.h"
#include "../Common/CubeRenderTarget.h"
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
	OpaqueDynamicReflectors = OpaqueNonFrustumCull + 1,
	Sky = OpaqueNonFrustumCull + 1,
	Count
};

class DynamicCubeMapApp : public MainWindow
{
public:
	DynamicCubeMapApp(HINSTANCE hInstance);
	DynamicCubeMapApp(const DynamicCubeMapApp& rhs) = delete;
	DynamicCubeMapApp& operator=(const DynamicCubeMapApp& rhs) = delete;
	~DynamicCubeMapApp();

	bool Initialize() override;

private:
	void OnResize() override;
	void Update(const GameTimer& gt) override;
	void Draw(const GameTimer& gt) override;
	void DrawSceneToCubeMap();
	
	void UpdateCamera(const GameTimer& gt);
	void UpdateInstanceBuffer(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void UpdateCubeMapFacePassCBs();
	void UpdateMaterialBuffer(const GameTimer& gt);

	void LoadTexture(std::wstring filePath, std::string textureName);
	void LoadCubeMap(std::wstring filePath, std::string textureName);

	void BuildCubeFaceCamera(float x, float y, float z);
	void LoadTextures();
	void BuildRootSignature();
	void BuildDescriptorHeaps();
	void BuildCubeDepthStencil();
	void BuildShadersAndInputLayout();
	void BuildMaterials();
	void BuildSkullGeometry();
	void BuildShapeGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildSkullRenderItem(std::unique_ptr<InstancedRenderItem>& skullRitem);
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<InstancedRenderItem*>& ritems);
	void DrawRenderItemsWithoutFrustumCulling(ID3D12GraphicsCommandList* cmdList, const std::vector<InstancedRenderItem*>& ritems);

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

	ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap = nullptr;
	CD3DX12_GPU_DESCRIPTOR_HANDLE cubeMapGpuHandle;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> geometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> materials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> textures;
	std::unordered_map<std::string, std::unique_ptr<Texture>> cubeMaps;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> shaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> pipelineStateObjects;

	std::vector<D3D12_INPUT_ELEMENT_DESC> defaultInputLayout;

	// List of all the render items.
	std::vector<std::unique_ptr<InstancedRenderItem>> allRitems;
	int instanceCount;

	DirectX::BoundingFrustum camFrustum;

	// Render items divided by PSO.
	std::vector<InstancedRenderItem*> RitemLayer[static_cast<int>(RenderLayer::Count)];


	PassConstants mainPassCB;

	bool isWireframe = false;

	float sunTheta = 1.25f * DirectX::XM_PI;
	float sunPhi = DirectX::XM_PIDIV4;

	POINT lastMousePos;

	Camera camera;

	int skyTexHeapIndex;
	int dynamicTexHeapIndex;
	Camera cubeMapCamera[6];
	ComPtr<ID3D12Resource> cubeDepthStencilBuffer;
	CD3DX12_CPU_DESCRIPTOR_HANDLE cubeDsv;

	std::unique_ptr<CubeRenderTarget> dynamicCubeMap = nullptr;
	const UINT64 CUBE_MAP_SIZE = 512;
};

