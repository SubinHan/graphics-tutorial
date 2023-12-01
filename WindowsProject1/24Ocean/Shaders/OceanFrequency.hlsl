#include "OceanUtil.hlsl"

cbuffer cbFrequencyConstants : register(b0)
{
	uint gResolutionSize;
	float gWaveLength;
	float gTime;
}

Texture3D<float4> gHTilde0 : register(t0);
Texture3D<float4> gHTilde0Conj : register(t1);

RWTexture3D<float4> gHTilde : register(u0);

#define N 512

[numthreads(N, 1, 1)]
void HTildeCS(
	int3 dispatchThreadID : SV_DispatchThreadID)
{
	uint3 basisIndex = dispatchThreadID;
	basisIndex.z %= 3;

	float2 hTilde0 = gHTilde0[basisIndex].xy;
	float2 hTilde0Conj = gHTilde0Conj[basisIndex].xy;

	float omegat = 
		Dispersion(dispatchThreadID.x, dispatchThreadID.y, gResolutionSize, gWaveLength) * gTime * 0.002f;

	const float cos_ = cos(omegat);
	const float sin_ = sin(omegat);

	float2 c0 = { cos_, sin_ };
	float2 c1 = { cos_, -sin_ };

	float2 res = ComplexMul(hTilde0, c0) + ComplexMul(hTilde0Conj, c1);

	// for convenience, product e^ikx before fourier transform.
	// Afters are not original result of htilde.
	float kx = PI * (2.0f * dispatchThreadID.x - gResolutionSize);
	float kz = PI * (2.0f * dispatchThreadID.y - gResolutionSize);
	float2 k = { kx, kz };
	float2 x =
	{
	dispatchThreadID.x / gResolutionSize - 0.5f,
	dispatchThreadID.y / gResolutionSize - 0.5f
	};

	const float kDotX = dot(k, x);
	const float cosKDotX = cos(kDotX);
	const float sinKDotX = sin(kDotX);
	const float2 kDotXComplex = { cosKDotX, sinKDotX };

	float2 hTilde = ComplexMul(res, kDotXComplex);

	float delta = 1.f / gResolutionSize;
	float2 dx = { delta, 0.0f };
	float2 dz = { 0.0f, delta };

	float len = length(k);

	//if (basisIndex.z == 0)
	//{
	//	if (len < 0.00001f) // 너무 작은 수로 나누면서 문제가 발생하는 듯..
	//	{
	//		res = float2(0.0f, 0.0f);
	//	}
	//	else
	//	{
	//		const float2 ikk = { 0.0f, -kx / len };
	//		res = ComplexMul(hTilde, ikk);
	//	}
	//}

	//if (basisIndex.z == 2)
	//{
	//	if (len < 0.00001f)
	//	{
	//		res = float2(0.0f, 0.0f);
	//	}
	//	else
	//	{
	//		const float2 ikk = { 0.0f, -kz / len };
	//		res = ComplexMul(hTilde, ikk);
	//	}
	//}

	////calculate slope
	//if (dispatchThreadID.z > 5)
	//{
	//	//const float kDotDz = dot(k, dz);
	//	//float2 ik = float2(cos(kDotDz), sin(kDotDz)) - float2(1.0f, 0.0f);
	//	//res = ComplexMul(res, ik);

	//	res = ComplexMul(res, float2(0.0f, kz));
	//}
	//else if (dispatchThreadID.z > 2)
	//{
	//	//const float kDotDx = dot(k, dx);
	//	//float2 ik = float2(cos(kDotDx), sin(kDotDx)) - float2(1.0f, 0.0f);
	//	//res = ComplexMul(res, ik);

	//	res = ComplexMul(res, float2(0.0f, kx));
	//}
	//else
	//{
	//}

	if (dispatchThreadID.z == 3)
	{
		const float kDotDx = dot(k, dx);
		float2 ik = float2(cos(kDotDx), sin(kDotDx)) - float2(1.0f, 0.0f);
		ik = float2(0.0f, kx);
		res = ComplexMul(hTilde, ik);
	}
	else if (dispatchThreadID.z == 6)
	{
		const float kDotDz = dot(k, dz);
		float2 ik = float2(cos(kDotDz), sin(kDotDz)) - float2(1.0f, 0.0f);
		ik = float2(0.0f, kz);
		res = ComplexMul(hTilde, ik);
	}
	else if (
		dispatchThreadID.z == 1 ||
		dispatchThreadID.z == 2 ||
		//dispatchThreadID.z == 4 || 
		//dispatchThreadID.z == 5 ||
		//dispatchThreadID.z == 7 ||
		dispatchThreadID.z == 8
		)
	{
		gHTilde[dispatchThreadID.xyz] = float4(0.0f, 0.0f, 0.0f, 0.0f);
		return;
	}
	gHTilde[dispatchThreadID.xyz] = float4(res, 0.0f, 0.0f);
}