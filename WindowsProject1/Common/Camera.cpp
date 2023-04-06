#include "Camera.h"

#include "MathHelper.h"

using namespace DirectX;

Camera::Camera()
	: position(1.0f, 1.0f, 1.0f),
	right(1.0f, 0.0f, 0.0f),
	up(0.0f, 1.0f, 0.0f),
	look(0.0f, 0.0f, 1.0f),
	nearZ(0.0f),
	farZ(0.0f),
	aspect(0.0f),
	fovY(0.0f),
	nearWindowHeight(0.0f),
	farWindowHeight(0.0f),
	viewDirty(true),
	view(MathHelper::Identity4x4()),
	proj(MathHelper::Identity4x4())
{
	SetLens(0.25f * MathHelper::Pi, 1.0f, 1.0f, 1000.0f);
}

Camera::~Camera()
{
}

DirectX::XMVECTOR Camera::GetPosition() const
{
	return XMLoadFloat3(&position);
}

DirectX::XMFLOAT3 Camera::GetPosition3f() const
{
	return position;
}

void Camera::SetPosition(float x, float y, float z)
{
	position.x = x;
	position.y = y;
	position.z = z;
	viewDirty = true;
}

void Camera::SetPosition(const DirectX::XMFLOAT3& v)
{
	position = v;
	viewDirty = true;
}

DirectX::XMVECTOR Camera::GetRight() const
{
	return XMLoadFloat3(&right);
}

DirectX::XMFLOAT3 Camera::GetRight3f() const
{
	return right;
}

DirectX::XMVECTOR Camera::GetUp() const
{
	return XMLoadFloat3(&up);
}

DirectX::XMFLOAT3 Camera::GetUp3f() const
{
	return up;
}

DirectX::XMVECTOR Camera::GetLook() const
{
	return XMLoadFloat3(&look);
}

DirectX::XMFLOAT3 Camera::GetLook3f() const
{
	return look;
}

float Camera::GetNearZ() const
{
	return nearZ;
}

float Camera::GetFarZ() const
{
	return farZ;
}

float Camera::GetAspect() const
{
	return aspect;
}

float Camera::GetFovY() const
{
	return fovY;
}

float Camera::GetFovX() const
{
	float halfWidth = 0.5f * GetNearWindowWidth();
	return 2.0f * atan(halfWidth / nearZ);
}

float Camera::GetNearWindowWidth() const
{
	return aspect * nearWindowHeight;
}

float Camera::GetNearWindowHeight() const
{
	return nearWindowHeight;
}

float Camera::GetFarWindowWidth() const
{
	return aspect * farWindowHeight;
}

float Camera::GetFarWindowHeight() const
{
	return farWindowHeight;
}

void Camera::SetLens(float fovY, float aspect, float zn, float zf)
{
	this->fovY = fovY;
	this->aspect = aspect;
	this->nearZ = zn;
	this->farZ = zf;

	nearWindowHeight = 2.0f * nearZ * tanf(0.5f * fovY);
	farWindowHeight = 2.0f * farZ * tanf(0.5f * fovY);

	XMMATRIX p = XMMatrixPerspectiveFovLH(fovY, aspect, nearZ, farZ);
	XMStoreFloat4x4(&proj, p);
}

void Camera::LookAt(DirectX::XMVECTOR pos, DirectX::XMVECTOR target, DirectX::XMVECTOR worldUp)
{
	XMVECTOR l = XMVector3Normalize(XMVectorSubtract(target, pos));
	XMVECTOR r = XMVector3Normalize(XMVector3Cross(worldUp, l));
	XMVECTOR u = XMVector3Cross(l, r);

	XMStoreFloat3(&position, pos);
	XMStoreFloat3(&look, l);
	XMStoreFloat3(&right, r);
	XMStoreFloat3(&up, u);

	viewDirty = true;
}

void Camera::LookAt(DirectX::XMFLOAT3& pos, DirectX::XMFLOAT3& target, DirectX::XMFLOAT3& up)
{
	XMVECTOR p = XMLoadFloat3(&pos);
	XMVECTOR t = XMLoadFloat3(&target);
	XMVECTOR u = XMLoadFloat3(&up);

	LookAt(p, t, u);
}

DirectX::XMMATRIX Camera::GetView() const
{
	assert(!viewDirty);
	return XMLoadFloat4x4(&view);
}

DirectX::XMMATRIX Camera::GetProj() const
{
	return XMLoadFloat4x4(&proj);
}

DirectX::XMFLOAT4X4 Camera::GetView4x4f() const
{
	assert(!viewDirty);
	return view;
}

DirectX::XMFLOAT4X4 Camera::GetProj4x4f() const
{
	return proj;
}

void Camera::Strafe(float d)
{
	XMVECTOR s = XMVectorReplicate(d);
	XMVECTOR r = XMLoadFloat3(&right);
	XMVECTOR p = XMLoadFloat3(&position);
	XMStoreFloat3(&position, XMVectorMultiplyAdd(s, r, p));

	viewDirty = true;
}

void Camera::Walk(float d)
{
	XMVECTOR s = XMVectorReplicate(d);
	XMVECTOR l = XMLoadFloat3(&look);
	XMVECTOR p = XMLoadFloat3(&position);
	XMStoreFloat3(&position, XMVectorMultiplyAdd(s, l, p));

	viewDirty = true;
}

void Camera::Pitch(float angle)
{
	XMMATRIX R = XMMatrixRotationAxis(XMLoadFloat3(&right), angle);

	XMStoreFloat3(&up, XMVector3TransformNormal(XMLoadFloat3(&up), R));
	XMStoreFloat3(&look, XMVector3TransformNormal(XMLoadFloat3(&look), R));

	viewDirty = true;
}

void Camera::RotateY(float angle)
{
	XMMATRIX R = XMMatrixRotationY(angle);

	XMStoreFloat3(&right, XMVector3TransformNormal(XMLoadFloat3(&right), R));
	XMStoreFloat3(&up, XMVector3TransformNormal(XMLoadFloat3(&up), R));
	XMStoreFloat3(&look, XMVector3TransformNormal(XMLoadFloat3(&look), R));

	viewDirty = true;
}

void Camera::Roll(float angle)
{
	XMMATRIX R = XMMatrixRotationAxis(XMLoadFloat3(&look), angle);

	XMStoreFloat3(&up, XMVector3TransformNormal(XMLoadFloat3(&up), R));
	XMStoreFloat3(&right, XMVector3TransformNormal(XMLoadFloat3(&right), R));

	viewDirty = true;
}

void Camera::UpdateViewMatrix()
{
	if(!viewDirty)
		return;

	XMVECTOR r = XMLoadFloat3(&right);
	XMVECTOR l = XMLoadFloat3(&look);
	XMVECTOR p = XMLoadFloat3(&position);
	l = XMVector3Normalize(l);
	XMVECTOR u = XMVector3Normalize(XMVector3Cross(l, r));
	r = XMVector3Cross(u, l);

	float x = -XMVectorGetX(XMVector3Dot(p, r));
	float y = -XMVectorGetX(XMVector3Dot(p, u));
	float z = -XMVectorGetX(XMVector3Dot(p, l));

	XMStoreFloat3(&right, r);
	XMStoreFloat3(&up, u);
	XMStoreFloat3(&look, l);

	view(0, 0) = right.x;
	view(1, 0) = right.y;
	view(2, 0) = right.z;
	view(3, 0) = x;

	view(0, 1) = up.x;
	view(1, 1) = up.y;
	view(2, 1) = up.z;
	view(3, 1) = y;

	view(0, 2) = look.x;
	view(1, 2) = look.y;
	view(2, 2) = look.z;
	view(3, 2) = z;

	view(0, 3) = 0.0f;
	view(1, 3) = 0.0f;
	view(2, 3) = 0.0f;
	view(3, 3) = 1.0f;

	viewDirty = false;
}
