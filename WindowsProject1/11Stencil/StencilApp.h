#pragma once

#include "../Common/MainWindow.h"
#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"
#include "../Common/GeometryGenerator.h"
#include "../Common/DDSTextureLoader.h"

#include "FrameResource.h"
#include "Waves.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

extern const int gNumFrameResources;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;

	// World matrix of the shape that describes the object's local space
	// relative to the world space, which defines the position, orientation,
	// and scale of the object in the world.
	XMFLOAT4X4 World = MathHelper::Identity4x4();

	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};

struct ReflectedRenderItem : RenderItem
{
	ReflectedRenderItem() = default;

	RenderItem* OriginalRenderItem = nullptr;
};

enum class RenderLayer : int
{
	Opaque = 0,
	Transparent = Opaque + 1,
	Mirrors = Transparent + 1,
	Reflected = Mirrors + 1,
	Shadow = Reflected + 1,
	Clear = Shadow + 1,
	Count
};

class StencilApp : public MainWindow
{
public:
	StencilApp(HINSTANCE hInstance);
	StencilApp(const StencilApp& rhs) = delete;
	StencilApp& operator=(const StencilApp& rhs) = delete;
	~StencilApp();

	virtual bool Initialize()override;

private:
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

	void OnMouseLeftDown(int x, int y, short keyState)override;
	void OnMouseLeftUp(int x, int y, short keyState)override;
	void OnMouseMove(int x, int y, short keyState)override;
	void UpdateReflectedItem(const GameTimer& gt);

	void OnKeyboardInput(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void UpdateReflectedPassCB(const GameTimer& gt);
	void UpdateWaves(const GameTimer& gt);
	void LoadTexture(std::wstring filePath, std::string textureName);

	void LoadTextures();
	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();
	void BuildRootSignature();
	void BuildDescriptorHeaps();
	void BuildShaderResourceViews();
	void BuildShadersAndInputLayout();
	void BuildLandGeometry();
	void BuildCrateGeometry();
	void BuildWavesGeometryBuffers();
	void BuildMirrorGeometry();
	void BuildEndOfWorldGeometry();
	void BuildReflectedItems();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildMaterials();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

	float GetHillsHeight(float x, float z)const;
	XMFLOAT3 GetHillsNormal(float x, float z)const;

private:

	std::vector<std::unique_ptr<FrameResource>> frameResources;
	FrameResource* currFrameResource = nullptr;
	int currFrameResourceIndex = 0;

	UINT cbvSrvDescriptorSize = 0;

	ComPtr<ID3D12RootSignature> rootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap> srvHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> geometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> materials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> textures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> shaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> PSOs;

	std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;

	RenderItem* wavesRitem = nullptr;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> allRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> RitemLayer[(int)RenderLayer::Count];

	std::unique_ptr<Waves> waves;

	PassConstants mainPassCB;
	PassConstants reflectedPassCB;

	XMFLOAT3 eyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 view = MathHelper::Identity4x4();
	XMFLOAT4X4 proj = MathHelper::Identity4x4();

	float theta = 1.5f * XM_PI;
	float phi = XM_PIDIV2 - 0.1f;
	float radius = 50.0f;

	float sunTheta = 1.25f * XM_PI;
	float sunPhi = XM_PIDIV4;

	POINT lastMousePos;
};