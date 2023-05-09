#include "OceanUtil.hlsl"

cbuffer cbFrequencyConstants : register(b0)
{
	uint gResolutionSize;
	float gWaveLength;
	float gTime;
}

Texture2D<float4> gHTilde0 : register(t0);
Texture2D<float4> gHTilde0Conj : register(t1);

RWTexture2D<float4> gHTilde : register(u0);

#define N 256

[numthreads(N, 1, 1)]
void HTildeCS(
	int3 groupThreadID : SV_GroupThreadID,
	int3 dispatchThreadID : SV_DispatchThreadID)
{
	float2 hTilde0 = gHTilde0[dispatchThreadID.xy].xy;
	float2 hTilde0Conj = gHTilde0Conj[dispatchThreadID.xy].xy;

	float omegat = 
		Dispersion(dispatchThreadID.x, dispatchThreadID.y, gResolutionSize, gWaveLength) * gTime * 0.1f;

	const float cos_ = cos(omegat);
	const float sin_ = sin(omegat);

	float2 c0 = { cos_, sin_ };
	float2 c1 = { cos_, -sin_ };

	float2 res = ComplexMul(hTilde0, c0) + ComplexMul(hTilde0Conj, c1);

	// for convenience, product e^ikx before fourier transform.
	// original result of htilde is 'res'.

	float kx = 2 * PI * (dispatchThreadID.x - gResolutionSize / 2.0f);
	float kz = 2 * PI * (dispatchThreadID.y - gResolutionSize / 2.0f);
	float2 k = { kx, kz };
	float2 x =
	{
	dispatchThreadID.x / gResolutionSize - 0.5f,
	dispatchThreadID.y / gResolutionSize - 0.5f
	};

	res = ComplexMul(res, dot(k, x));
	gHTilde[dispatchThreadID.xy] = float4(res, 0.0f, 0.0f);

}