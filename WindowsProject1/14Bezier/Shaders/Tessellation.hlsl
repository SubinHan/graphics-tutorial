//***************************************************************************************
// Tessellation.hlsl by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************


// Include structures and functions for lighting.
#include "LightingUtil.hlsl"

Texture2D    gDiffuseMap : register(t0);


SamplerState gsamPointWrap        : register(s0);
SamplerState gsamPointClamp       : register(s1);
SamplerState gsamLinearWrap       : register(s2);
SamplerState gsamLinearClamp      : register(s3);
SamplerState gsamAnisotropicWrap  : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

// Constant data that varies per frame.
cbuffer cbPerObject : register(b0)
{
	float4x4 gWorld;
	float4x4 gTexTransform;
};

// Constant data that varies per material.
cbuffer cbPass : register(b1)
{
	float4x4 gView;
	float4x4 gInvView;
	float4x4 gProj;
	float4x4 gInvProj;
	float4x4 gViewProj;
	float4x4 gInvViewProj;
	float3 gEyePosW;
	float cbPerObjectPad1;
	float2 gRenderTargetSize;
	float2 gInvRenderTargetSize;
	float gNearZ;
	float gFarZ;
	float gTotalTime;
	float gDeltaTime;
	float4 gAmbientLight;

	float4 gFogColor;
	float gFogStart;
	float gFogRange;

	// Indices [0, NUM_DIR_LIGHTS) are directional lights;
	// indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
	// indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
	// are spot lights for a maximum of MaxLights per object.
	Light gLights[MaxLights];
};

cbuffer cbMaterial : register(b2)
{
	float4   gDiffuseAlbedo;
	float3   gFresnelR0;
	float    gRoughness;
	float4x4 gMatTransform;
};

struct VertexIn
{
	float3 PosL    : POSITION;
};

struct VertexOut
{
	float3 PosL    : POSITION;
};


struct HullOut
{
	float3 PosL : POSITION;
};


struct PatchTess
{
	float EdgeTess[4]   : SV_TessFactor;
	float InsideTess[2] : SV_InsideTessFactor;
};

float4 BernsteinBasis(float t)
{
	float invT = 1.0f - t;

	return float4(
		invT * invT * invT,
		3.0f * t * invT * invT,
		3.0f * t * t * invT,
		t * t * t);
}

float4 dBernsteinBasis(float t)
{
	float invT = 1.0f - t;

	return float4(
		-3 * invT * invT,
		3 * invT * invT - 6 * t * invT,
		6 * t * invT - 3 * t * t,
		3 * t * t);
}

VertexOut VS(VertexIn vin)
{
	VertexOut vout;

	vout.PosL = vin.PosL;

	return vout;
}

float3 CubicBezierSum(const OutputPatch<HullOut, 16> bezPatch,
	float4 basisU, float4 basisV)
{
	float3 sum = float3(0.0f, 0.0f, 0.0f);
	sum += basisV.x * (basisU.x * bezPatch[0].PosL +
		basisU.y * bezPatch[1].PosL +
		basisU.z * bezPatch[2].PosL +
		basisU.w * bezPatch[3].PosL);
	sum += basisV.y * (basisU.x * bezPatch[4].PosL +
		basisU.y * bezPatch[5].PosL +
		basisU.z * bezPatch[6].PosL +
		basisU.w * bezPatch[7].PosL);
	sum += basisV.z * (basisU.x * bezPatch[8].PosL +
		basisU.y * bezPatch[9].PosL +
		basisU.z * bezPatch[10].PosL +
		basisU.w * bezPatch[11].PosL);
	sum += basisV.w * (basisU.x * bezPatch[12].PosL +
		basisU.y * bezPatch[13].PosL +
		basisU.z * bezPatch[14].PosL +
		basisU.w * bezPatch[15].PosL);

	return sum;
}


PatchTess ConstantHS(InputPatch<VertexOut, 16> patch, uint patchID : SV_PrimitiveID)
{
	PatchTess pt;

	float3 centerL = 0.0625f *
		patch[0].PosL + patch[1].PosL + patch[2].PosL + patch[3].PosL +
		patch[4].PosL + patch[5].PosL + patch[6].PosL + patch[7].PosL +
		patch[8].PosL + patch[9].PosL + patch[10].PosL + patch[11].PosL +
		patch[12].PosL + patch[13].PosL + patch[14].PosL + patch[15].PosL;
		
	float3 centerW = mul(float4(centerL, 1.0f), gWorld).xyz;

	float d = distance(centerW, gEyePosW);

	// Tessellate the patch based on distance from the eye such that
	// the tessellation is 0 if d >= d1 and 64 if d <= d0.  The interval
	// [d0, d1] defines the range we tessellate in.

	const float d0 = 0.0f;
	const float d1 = 150.0f;
	float tess = 10.0f + 54.0f * saturate((d1 - d) / (d1 - d0));

	// Uniformly tessellate the patch.

	pt.EdgeTess[0] = tess;
	pt.EdgeTess[1] = tess;
	pt.EdgeTess[2] = tess;
	pt.EdgeTess[3] = tess;

	pt.InsideTess[0] = tess;
	pt.InsideTess[1] = tess;

	return pt;
}

[domain("quad")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(16)]
[patchconstantfunc("ConstantHS")]
[maxtessfactor(64.0f)]
HullOut HS(InputPatch<VertexOut, 16> p,
	uint i : SV_OutputControlPointID,
	uint patchId : SV_PrimitiveID)
{
	HullOut hout;

	hout.PosL = p[i].PosL;

	return hout;
}

struct DomainOut
{
	float4 PosH : SV_POSITION;
};

// The domain shader is called for every vertex created by the tessellator.  
// It is like the vertex shader after tessellation.
[domain("quad")]
DomainOut DS(PatchTess patchTess,
	float2 uv : SV_DomainLocation,
	const OutputPatch<HullOut, 16> bezPatch)
{
	DomainOut dout;

	float4 basisU = BernsteinBasis(uv.x);
	float4 basisV = BernsteinBasis(uv.y);

	float3 p = CubicBezierSum(bezPatch, basisU, basisV);

	float4 dBasisU = dBernsteinBasis(uv.x);
	float4 dBasisV = dBernsteinBasis(uv.y);

	float3 dpdu = CubicBezierSum(bezPatch, dBasisU, basisV);
	float3 dpdv = CubicBezierSum(bezPatch, basisU, dBasisV);

	float4 posW = mul(float4(p, 1.0f), gWorld);
	dout.PosH = mul(posW, gViewProj);

	return dout;
}

float4 PS(DomainOut pin) : SV_Target
{
	return float4(1.0f, 1.0f, 1.0f, 1.0f);
}