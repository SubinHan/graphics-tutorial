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

#define N 256

[numthreads(N, 1, 1)]
void HTildeCS(
	int3 dispatchThreadID : SV_DispatchThreadID)
{
	uint3 basisIndex = dispatchThreadID;
	basisIndex.z %= 3;

	float2 hTilde0 = gHTilde0[basisIndex].xy;
	float2 hTilde0Conj = gHTilde0Conj[basisIndex].xy;

	float omegat = 
		Dispersion(dispatchThreadID.x, dispatchThreadID.y, gResolutionSize, gWaveLength) * gTime * 0.1f;

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

	res = ComplexMul(res, kDotXComplex);

	float delta = 1.f / gResolutionSize;
	float2 dx = { delta, 0.0f };
	float2 dz = { 0.0f, delta };

	float len = length(k);

	if (basisIndex.z == 0)
	{
		{
			const float2 ikk = { 0.0f, kx / len };
			res = ComplexMul(res, ikk);
		}
	}

	if (basisIndex.z == 2)
	{
		{
			const float2 ikk = { 0.0f, kz / len };
			res = ComplexMul(res, ikk);
		}
	}

	// calculate slope
	if (dispatchThreadID.z > 5)
	{
		const float kDotDz = dot(k, dz);
		float2 ik = { cos(kDotDz), sin(kDotDz) };
		res = ComplexMul(res, ik) - res;
		//res = ComplexMul(res, float2(0.0f, kz));
	}
	else if (dispatchThreadID.z > 2)
	{
		const float kDotDx = dot(k, dx);
		float2 ik = { cos(kDotDx), sin(kDotDx) };
		res = ComplexMul(res, ik) - res;
		//res = ComplexMul(res, float2(0.0f, kx));
	}


	gHTilde[dispatchThreadID.xyz] = float4(res, 0.0f, 0.0f);
}