#include "OceanUtil.hlsl"

cbuffer cbBasisConstants : register(b0)
{
	float gAmplitude;
	float2 gWind;
	uint gResolutionSize;
	float gWaveLength;
}

RWTexture2DArray<float4> gHTilde0 : register(u0);
RWTexture2DArray<float4> gHTilde0Conj : register(u1);

#define N 16

[numthreads(N, N, 1)]
void OceanBasisCS(
	int3 groupThreadID : SV_GroupThreadID,
	int3 dispatchThreadID : SV_DispatchThreadID)
{
	float2 hTilde0 = HTilde0(
		dispatchThreadID.x,
		dispatchThreadID.y,
		gAmplitude,
		gWind,
		gResolutionSize,
		gWaveLength,
		0.52743f * dispatchThreadID.z
	);

	float2 beforeConj = HTilde0(
		gResolutionSize - dispatchThreadID.x,
		gResolutionSize - dispatchThreadID.y,
		gAmplitude,
		gWind,
		gResolutionSize,
		gWaveLength,
		0.52743f * dispatchThreadID.z
	);

	gHTilde0[dispatchThreadID.xyz] = float4(hTilde0.xy, 0.0f, 0.0f);
	gHTilde0Conj[dispatchThreadID.xyz] = float4(beforeConj.xy, 0.0f, 0.0f);
}