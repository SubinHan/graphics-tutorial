#pragma once

#include "DxUtil.h"

#include <vector>

#include "Camera.h"
#include "UploadBuffer.h"

struct InstanceData
{
	DirectX::XMFLOAT4X4 World = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
	UINT MaterialIndex;
	DirectX::XMUINT3 InstancePad;
};

class InstancedRenderItem
{
public:
	InstancedRenderItem(
		MeshGeometry* geometry,
		D3D12_PRIMITIVE_TOPOLOGY primitiveType,
		UINT indexCount,
		UINT startIndexLocation,
		int baseVertexLocation,
		DirectX::BoundingBox boundingBox
	);
	~InstancedRenderItem() = default;
	InstancedRenderItem& operator=(const InstancedRenderItem& rhs) = delete;
	InstancedRenderItem(const InstancedRenderItem& rhs) = delete;

	UINT GetInstanceCount() const noexcept;
	UINT GetIndexCountOfInstance() const noexcept;
	int GetBaseVertexLocationInBuffer() const noexcept;

	void SetVisibility(const bool newVisibility) noexcept;

	//void SetWorldOfInstanceAt(DirectX::XMFLOAT4X4& newWorld, int index);
	//void SetWorldOfInstanceAt(DirectX::XMMATRIX& newWorld, int index);
	//void SetTexTransformOfInstanceAt(DirectX::XMFLOAT4X4& newTexTransform, int index);
	//void SetTexTransformOfInstanceAt(DirectX::XMMATRIX& newTexTransform, int index);
	//void SetMaterialIndexOfInstanceAt(int newMaterialIndex, int index);

	void AddInstance(const InstanceData& data);

	UINT UploadWithFrustumCulling(const Camera& camera, UploadBuffer<InstanceData>& instanceBuffer, int bufferOffset);
	UINT UploadWithoutFrustumCulling(UploadBuffer<InstanceData>& instanceBuffer, int bufferOffset);
	void BeforeDraw(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* instanceBuffer,
	                UINT ibSlotOfRootSignature) const;
	void DrawFrustumCullednstances(
		ID3D12GraphicsCommandList* cmdList, 
		ID3D12Resource* instanceBuffer,
		UINT ibSlotOfRootSignature) const;
	void DrawAllInstances(
		ID3D12GraphicsCommandList* cmdList, 
		ID3D12Resource* instanceBuffer, 
		UINT ibSlotOfRootSignature) const;
	

private:
	MeshGeometry* geometry;
	D3D12_PRIMITIVE_TOPOLOGY primitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	DirectX::BoundingBox boundingBox;
	std::vector<InstanceData> instances;
	int instanceBufferOffset;
	
	UINT uploadedInstanceCount;
	UINT indexCount;
	UINT startIndexLocation;
	int baseVertexLocation;

	bool bVisible;

	mutable bool bUploadedWithFrustumCulling;
};