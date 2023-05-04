#include "OceanUtil.hlsl"

cbuffer cbWaveConstants : register(b0)
{
	uint gResolutionSize;
	float gWaveLength;
	float gWaveTime;
}

Texture2D gHTilde0 : register(t0);
Texture2D gHTilde0Conj : register(t1);
RWTexture2D<float3> gDisplacementMap : register (u0);

SamplerState gsamPointWrap        : register(s0);
SamplerState gsamPointClamp       : register(s1);
SamplerState gsamLinearWrap       : register(s2);
SamplerState gsamLinearClamp      : register(s3);
SamplerState gsamAnisotropicWrap  : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

float2 HTilde(int n, int m)
{
	float2 hTilde0 = gHTilde0.SampleLevel(gsamAnisotropicWrap, float2(n, m), 0).xy;
	float2 hTilde0Conj = gHTilde0Conj.SampleLevel(gsamAnisotropicWrap, float2(n, m), 0).xy;

	float omegat = Dispersion(n, m, gResolutionSize, gWaveLength) * gWaveTime;
	float cosVal = cos(omegat);
	float sinVal = sin(omegat);

	float2 c0 = float2(cosVal, sinVal);
	float2 c1 = float2(cosVal, -sinVal);

	float2 result = float2(
		hTilde0.x * c0.x - hTilde0.y * c0.y + hTilde0Conj.x * c1.x - hTilde0Conj.y * c1.y,
		hTilde0.x * c0.y + hTilde0.y * c0.x + hTilde0Conj.x * c1.y + hTilde0Conj.y * c1.x
		);

	return result;
}

#define N 256

[numthreads(N, 1, 1)]
void OceanCS(
	int3 groupThreadID : SV_GroupThreadID,
	int3 dispatchThreadID : SV_DispatchThreadID)
{
}
