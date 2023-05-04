#include "OceanUtil.hlsl"

cbuffer cbBasisConstants : register(b0)
{
	float gAmplitude;
	float2 gWind;
	uint gResolutionSize;
	float gWaveLength;
}

RWTexture2D<float4> gHTilde0 : register(u0);
RWTexture2D<float4> gHTilde0Conj : register(u1);

#define N 16

[numthreads(N, N, 1)]
void OceanBasisCS(
	int3 groupThreadID : SV_GroupThreadID,
	int3 dispatchThreadID : SV_DispatchThreadID)
{
	float2 beforeConj = HTilde0(
		gResolutionSize - dispatchThreadID.x,
		gResolutionSize - dispatchThreadID.y,
		gAmplitude,
		gWind,
		gResolutionSize,
		gWaveLength
	);

	gHTilde0[dispatchThreadID.xy] = float4(0.0f, 0.5f, 1.0f, 1.0f);
	gHTilde0Conj[dispatchThreadID.xy] = float4(0.0f, 0.5f, 1.0f, 1.0f);
}