#include "InstancedRenderItem.h"

InstancedRenderItem::InstancedRenderItem(
	MeshGeometry* geometry,
	D3D12_PRIMITIVE_TOPOLOGY primitiveType,
	UINT indexCount,
	UINT startIndexLocation,
	int baseVertexLocation,
	DirectX::BoundingBox boundingBox
)
	: geometry(geometry),
	primitiveType(primitiveType),
	indexCount(indexCount),
	startIndexLocation(startIndexLocation),
	baseVertexLocation(baseVertexLocation),
	boundingBox(boundingBox),
	uploadedInstanceCount(0),
	bVisible(true),
	instances()
{

}

UINT InstancedRenderItem::GetInstanceCount() const noexcept
{
	return instances.size();
}

UINT InstancedRenderItem::GetIndexCountOfInstance() const noexcept
{
	return indexCount;
}

int InstancedRenderItem::GetBaseVertexLocationInBuffer() const noexcept
{
	return baseVertexLocation;
}

void InstancedRenderItem::SetVisibility(const bool newVisibility) noexcept
{
	bVisible = newVisibility;
}

void InstancedRenderItem::AddInstance(const InstanceData& data)
{
	instances.push_back(data);
}

UINT InstancedRenderItem::UploadWithFrustumCulling(const Camera& camera, UploadBuffer<InstanceData>& instanceBuffer, int bufferOffset)
{
	if (!bVisible)
		return 0;

	DirectX::XMMATRIX view = camera.GetView();
	auto viewDeterminant = XMMatrixDeterminant(view);
	DirectX::XMMATRIX invView = XMMatrixInverse(&viewDeterminant, view);

	int visibleInstanceCount = 0;

	DirectX::BoundingFrustum camFrustum;
	DirectX::BoundingFrustum::CreateFromMatrix(camFrustum, camera.GetProj());
	for (UINT i = 0; i < (UINT)instances.size(); ++i)
	{
		DirectX::XMMATRIX world = DirectX::XMLoadFloat4x4(&instances[i].World);
		DirectX::XMMATRIX texTransform = DirectX::XMLoadFloat4x4(&instances[i].TexTransform);

		auto worldDeterminant = XMMatrixDeterminant(world);
		DirectX::XMMATRIX invWorld = XMMatrixInverse(&worldDeterminant, world);

		// View space to the object's local space.
		DirectX::XMMATRIX viewToLocal = XMMatrixMultiply(invView, invWorld);

		// Transform the camera frustum from view space to the object's local space.
		DirectX::BoundingFrustum localSpaceFrustum;
		camFrustum.Transform(localSpaceFrustum, viewToLocal);

		// Perform the box/frustum intersection test in local space.
		if (localSpaceFrustum.Contains(boundingBox) != DirectX::DISJOINT)
		{
			InstanceData data;
			XMStoreFloat4x4(&data.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&data.TexTransform, XMMatrixTranspose(texTransform));
			data.MaterialIndex = instances[i].MaterialIndex;

			// Write the instance data to structured buffer for the visible objects.
			int elementIndex = bufferOffset + visibleInstanceCount++;
			instanceBuffer.CopyData(elementIndex, data);
		}
	}

	instanceBufferOffset = bufferOffset * instanceBuffer.GetElementByteSize();
	uploadedInstanceCount = visibleInstanceCount;
	bUploadedWithFrustumCulling = true;

	return uploadedInstanceCount;
}

UINT InstancedRenderItem::UploadWithoutFrustumCulling(UploadBuffer<InstanceData>& instanceBuffer, int bufferOffset)
{
	if (!bVisible)
		return 0;

	int uploaded = 0;
	
	for (UINT i = 0; i < (UINT)instances.size(); ++i)
	{
		DirectX::XMMATRIX world = 
			DirectX::XMLoadFloat4x4(&instances[i].World);
		DirectX::XMMATRIX texTransform = 
			DirectX::XMLoadFloat4x4(&instances[i].TexTransform);

		InstanceData data;
		XMStoreFloat4x4(&data.World, XMMatrixTranspose(world));
		XMStoreFloat4x4(&data.TexTransform, XMMatrixTranspose(texTransform));
		data.MaterialIndex = instances[i].MaterialIndex;

		// Write the instance data to structured buffer for the visible objects.
		int elementIndex = bufferOffset + uploaded++;
		instanceBuffer.CopyData(elementIndex, data);
	}

	instanceBufferOffset = bufferOffset * instanceBuffer.GetElementByteSize();
	uploadedInstanceCount = uploaded;
	bUploadedWithFrustumCulling = false;

	return uploadedInstanceCount;
}

void InstancedRenderItem::BeforeDraw(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* instanceBuffer, UINT ibSlotOfRootSignature) const
{
	auto vertexBufferView = geometry->VertexBufferView();
	auto indexBufferView = geometry->IndexBufferView();

	cmdList->IASetVertexBuffers(0, 1, &vertexBufferView);
	cmdList->IASetIndexBuffer(&indexBufferView);
	cmdList->IASetPrimitiveTopology(primitiveType);

	cmdList->SetGraphicsRootShaderResourceView(
		ibSlotOfRootSignature, 
		instanceBuffer->GetGPUVirtualAddress() + instanceBufferOffset);
}

void InstancedRenderItem::DrawFrustumCullednstances(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* instanceBuffer, UINT ibSlotOfRootSignature) const
{
	assert(bUploadedWithFrustumCulling);

	if (!this->bVisible)
		return;

	BeforeDraw(cmdList, instanceBuffer, ibSlotOfRootSignature);

	cmdList->DrawIndexedInstanced(
		indexCount,
		uploadedInstanceCount,
		startIndexLocation,
		baseVertexLocation,
		0
	);
}

void InstancedRenderItem::DrawAllInstances(ID3D12GraphicsCommandList* cmdList, ID3D12Resource* instanceBuffer, UINT ibSlotOfRootSignature) const
{
	assert(!bUploadedWithFrustumCulling);

	if (!this->bVisible)
		return;

	BeforeDraw(cmdList, instanceBuffer, ibSlotOfRootSignature);

	cmdList->DrawIndexedInstanced(
		indexCount, 
		instances.size(),
		startIndexLocation, 
		baseVertexLocation, 
		0
	);
}
