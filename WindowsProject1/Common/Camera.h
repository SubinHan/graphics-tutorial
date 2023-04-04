#pragma once
#include <DirectXMath.h>

class Camera
{
public:
	Camera();
	~Camera();

	Camera(const Camera& rhs) = delete;
	const Camera& operator=(const Camera& rhs) = delete;

	DirectX::XMVECTOR GetPosition() const;
	DirectX::XMFLOAT3 GetPosition3f() const;
	void SetPosition(float x, float y, float z);
	void SetPosition(const DirectX::XMFLOAT3& v);

	DirectX::XMVECTOR GetRight() const;
	DirectX::XMFLOAT3 GetRight3f() const;
	DirectX::XMVECTOR GetUp() const;
	DirectX::XMFLOAT3 GetUp3f() const;
	DirectX::XMVECTOR GetLook() const;
	DirectX::XMFLOAT3 GetLook3f() const;

	float GetNearZ() const;
	float GetFarZ() const;
	float GetAspect() const;
	float GetFovY() const;
	float GetFovX() const;

	float GetNearWindowWidth() const;
	float GetNearWindowHeight() const;
	float GetFarWindowWidth() const;
	float GetFarWindowHeight() const;

	void SetLens(float fovY, float aspect, float zn, float zf);

	void LookAt(
		DirectX::XMVECTOR pos,
		DirectX::XMVECTOR target,
		DirectX::XMVECTOR worldUp
	);
	void LookAt(
		DirectX::XMFLOAT3& pos,
		DirectX::XMFLOAT3& target,
		DirectX::XMFLOAT3& up
	);

	DirectX::XMMATRIX GetView() const;
	DirectX::XMMATRIX GetProj() const;

	DirectX::XMFLOAT4X4 GetView4x4f() const;
	DirectX::XMFLOAT4X4 GetProj4x4f() const;

	void Strafe(float d);
	void Walk(float d);

	void Pitch(float angle);
	void RotateY(float angle);
	void Roll(float angle);

	void UpdateViewMatrix();

private:
	DirectX::XMFLOAT3 position;
	DirectX::XMFLOAT3 right;
	DirectX::XMFLOAT3 up;
	DirectX::XMFLOAT3 look;

	float nearZ;
	float farZ;
	float aspect;
	float fovY;
	float nearWindowHeight;
	float farWindowHeight;

	bool viewDirty;

	DirectX::XMFLOAT4X4 view;
	DirectX::XMFLOAT4X4 proj;
};
