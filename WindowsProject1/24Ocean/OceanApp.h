#pragma once

#include "../Common/MainWindow.h"
#include "../Common/MathHelper.h"
#include "../Common/DxUtil.h"
#include "../Common/DDSTextureLoader.h"
#include "FrameResource.h"
#include "OceanMap.h"
#include "ShadowMap.h"
#include "../Common/Camera.h"
#include "Ssao.h"

extern const int gNumFrameResources;

struct RenderItem
{
	RenderItem() = default;

	// World matrix of the shape that describes the object's local space
	// relative to the world space, which defines the position, orientation,
	// and scale of the object in the world.
	DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

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
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};

enum class RenderLayer : int
{
	Opaque = 0,
	DebugSsao,
	DebugOcean,
	Sky,
	Ocean,
	Count
};

class OceanApp : public MainWindow
{
public:
	OceanApp(HINSTANCE hInstance);
	OceanApp(const OceanApp& rhs) = delete;
	OceanApp& operator=(const OceanApp& rhs) = delete;
	~OceanApp();

	bool Initialize() override;

private:
	void OnResize() override;
	void Update(const GameTimer& gt) override;
	void Draw(const GameTimer& gt) override;

	void OnKeyboardInput(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialBuffer(const GameTimer& gt);
	void UpdateShadowTransform(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void UpdateShadowPassCB(const GameTimer& gt);
	void UpdateSsaoCB(const GameTimer& gt);


	void LoadTextures();
	void BuildRootSignature();
	void BuildSsaoRootSignature();
	void BuildOceanBasisRootSignature();
	void BuildOceanFrequencyRootSignature();
	void BuildOceanDisplacementRootSignature();
	void BuildOceanDebugRootSignature();
	void BuildDescriptorHeaps();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildMaterials();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
	void DrawOceanDebug(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
	void DrawSceneToShadowMap();
	void DrawNormalsAndDepth();
	void DrawDebugThings(ComPtr<ID3D12GraphicsCommandList> commandList);

	CD3DX12_CPU_DESCRIPTOR_HANDLE GetCpuSrv(int index)const;
	CD3DX12_GPU_DESCRIPTOR_HANDLE GetGpuSrv(int index)const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetDsv(int index)const;
	CD3DX12_CPU_DESCRIPTOR_HANDLE GetRtv(int index)const;

public:
	void OnMouseLeftDown(int x, int y, short keyState) override;
	void OnMouseLeftUp(int x, int y, short keyState) override;
	void OnMouseMove(int x, int y, short keyState) override;

private:
	static constexpr int MAIN_ROOT_SLOT_OBJECT_CB = 0;
	static constexpr int MAIN_ROOT_SLOT_PASS_CB = 1;
	static constexpr int MAIN_ROOT_SLOT_MATERIAL_SRV = 2;
	static constexpr int MAIN_ROOT_SLOT_CUBE_SHADOW_SSAO_TABLE = 3;
	static constexpr int MAIN_ROOT_SLOT_OCEAN_TABLE = 4;
	static constexpr int MAIN_ROOT_SLOT_TEXTURE_TABLE = 5;

	static constexpr int SSAO_ROOT_SLOT_PASS_CB = 0;
	static constexpr int SSAO_ROOT_SLOT_CONSTANTS = 1;
	static constexpr int SSAO_ROOT_SLOT_NORMAL_DEPTH_SRV = 2;
	static constexpr int SSAO_ROOT_SLOT_RANDOM_VECTOR_SRV = 3;

	static constexpr int OCEAN_BASIS_ROOT_SLOT_PASS_CB = 0;
	static constexpr int OCEAN_BASIS_ROOT_SLOT_HTILDE0_UAV = 1;
	static constexpr int OCEAN_BASIS_ROOT_SLOT_HTILDE0CONJ_UAV = 2;
	static constexpr int OCEAN_BASIS_ROOT_SLOT_HTILDE_UAV = 3;

	static constexpr int OCEAN_FREQUENCY_ROOT_SLOT_PASS_CB = 0;
	static constexpr int OCEAN_FREQUENCY_ROOT_SLOT_HTILDE0_SRV = 1;
	static constexpr int OCEAN_FREQUENCY_ROOT_SLOT_HTILDE0CONJ_SRV = 2;
	static constexpr int OCEAN_FREQUENCY_ROOT_SLOT_HTILDE_UAV = 3;

	static constexpr int OCEAN_DISPLACEMENT_ROOT_SLOT_PASS_CB = 0;
	static constexpr int OCEAN_DISPLACEMENT_ROOT_SLOT_HTILDE_UAV = 1;
	static constexpr int OCEAN_DISPLACEMENT_ROOT_SLOT_DISPLACEMENT_UAV = 2;

	static constexpr int OCEAN_DEBUG_ROOT_SLOT_PASS_CB = 0;
	static constexpr int OCEAN_DEBUG_ROOT_SLOT_HTILDE0_SRV = 1;
	static constexpr int OCEAN_DEBUG_ROOT_SLOT_DISPLACEMENT_SRV = 2;

	static constexpr float DBEUG_SIZE_Y = 0.5f;

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12RootSignature> mSsaoRootSignature = nullptr;
	ComPtr<ID3D12RootSignature> mOceanBasisRootSignature = nullptr;
	ComPtr<ID3D12RootSignature> mOceanFrequencyRootSignature = nullptr;
	ComPtr<ID3D12RootSignature> mOceanDisplacementRootSignature = nullptr;
	ComPtr<ID3D12RootSignature> mOceanDebugRootSignature = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mRitemLayer[static_cast<int>(RenderLayer::Count)];

	UINT mSkyTexHeapIndex = 0;
	UINT mShadowMapHeapIndex = 0;

	UINT mSsaoHeapIndexStart = 0;
	UINT mSsaoAmbientMapIndex = 0;

	UINT mOceanMapHeapIndex;

	UINT mNullCubeSrvIndex = 0;
	UINT mNullTexSrvIndex1 = 0;
	UINT mNullTexSrvIndex2 = 0;

	bool mIsWireframe;

	CD3DX12_GPU_DESCRIPTOR_HANDLE mNullSrv;

	PassConstants mMainPassCB;  // index 0 of pass cbuffer.
	PassConstants mShadowPassCB;// index 1 of pass cbuffer.

	Camera mCamera;

	std::unique_ptr<ShadowMap> mShadowMap;

	std::unique_ptr<Ssao> mSsao;
	std::unique_ptr<OceanMap> mOceanMap;

	DirectX::BoundingSphere mSceneBounds;

	float mLightNearZ = 0.0f;
	float mLightFarZ = 0.0f;
	DirectX::XMFLOAT3 mLightPosW;
	DirectX::XMFLOAT4X4 mLightView = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 mLightProj = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 mShadowTransform = MathHelper::Identity4x4();

	float mLightRotationAngle = 0.0f;
	DirectX::XMFLOAT3 mBaseLightDirections[3] = {
		DirectX::XMFLOAT3(0.57735f, -0.57735f, 0.57735f),
		DirectX::XMFLOAT3(-0.57735f, -0.57735f, 0.57735f),
		DirectX::XMFLOAT3(0.0f, -0.707f, -0.707f)
	};
	DirectX::XMFLOAT3 mRotatedLightDirections[3];

	POINT mLastMousePos;
};