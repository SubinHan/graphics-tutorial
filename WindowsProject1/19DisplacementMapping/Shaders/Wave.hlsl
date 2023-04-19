//***************************************************************************************
// Tessellation.hlsl by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#include "Common.hlsl"

struct VertexIn
{
	float3 PosL    : POSITION;
	float3 NormalL : NORMAL;
	float2 TexC    : TEXCOORD;
	float3 TangentU : TANGENT;
};

struct VertexOut
{
	float3 PosL    : POSITION;
	float3 NormalL : NORMAL;
	float3 TangentU : TANGENT;
	float2 TexC1    : TEXCOORD1;
	float2 TexC2    : TEXCOORD2;
	nointerpolation uint MatIndex : MATINDEX;
	nointerpolation uint InstanceIndex : INSTANCEINDEX;
};

struct HullOut
{
	float3 PosL    : POSITION;
	float3 NormalL : NORMAL;
	float3 TangentU : TANGENT;
	float2 TexC1    : TEXCOORD1;
	float2 TexC2    : TEXCOORD2;
	nointerpolation uint MatIndex : MATINDEX;
	nointerpolation uint InstanceIndex : INSTANCEINDEX;
};

struct DomainOut
{
	float4 PosH						: SV_POSITION;
	float3 PosW						: POSITION;
	float3 NormalW					: NORMAL;
	float3 TangentW					: TANGENT;
	float2 TexC1						: TEXCOORD1;
	float2 TexC2						: TEXCOORD2;
	nointerpolation uint MatIndex	: MATINDEX;
};

struct PatchTess
{
	float EdgeTess[3]	 : SV_TessFactor;
	float InsideTess : SV_InsideTessFactor;
};


VertexOut VS(VertexIn vin, uint instanceID : SV_InstanceID)
{
	VertexOut vout;

	InstanceData instData = gInstanceData[instanceID];
	uint matIndex = instData.MaterialIndex;
	float4x4 texTransform = instData.TexTransform;

	vout.PosL = vin.PosL;
	vout.NormalL = vin.NormalL;
	vout.TangentU = vin.TangentU;
	float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), texTransform);
	vout.TexC1 = mul(texC, gMaterialData[matIndex].MatTransform1).xy;
	vout.TexC2 = mul(texC, gMaterialData[matIndex].MatTransform2).xy;
	vout.MatIndex = matIndex;
	vout.InstanceIndex = instanceID;

	return vout;
}

PatchTess ConstantHS(InputPatch<VertexOut, 3> patch, uint patchID : SV_PrimitiveID)
{
	PatchTess pt;

	float4x4 world = gInstanceData[patch[0].InstanceIndex].World;

	float3 centerL = 0.333f * (patch[0].PosL + patch[1].PosL + patch[2].PosL);
	float3 centerW = mul(float4(centerL, 1.0f), world).xyz;

	float d = distance(centerW, gEyePosW);

	const float d0 = 0.0f;
	const float d1 = 50.0f;

	float tess = 1.0f + 7.0f * saturate((d1 - d) / (d1 - d0));

	pt.EdgeTess[0] = tess;
	pt.EdgeTess[1] = tess;
	pt.EdgeTess[2] = tess;

	pt.InsideTess = tess;

	return pt;
}

[domain("tri")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("ConstantHS")]
[maxtessfactor(64.0f)]
HullOut HS(InputPatch<VertexOut, 3> p,
	uint i : SV_OutputControlPointID,
	uint patchId : SV_PrimitiveID)
{
	HullOut hout;

	hout.PosL = p[i].PosL;
	hout.NormalL = p[i].NormalL;
	hout.TangentU = p[i].TangentU;
	hout.TexC1 = p[i].TexC1;
	hout.TexC2 = p[i].TexC2;
	hout.MatIndex = p[i].MatIndex;
	hout.InstanceIndex = p[i].InstanceIndex;

	return hout;
}

// The domain shader is called for every vertex created by the tessellator.  
// It is like the vertex shader after tessellation.
[domain("tri")]
DomainOut DS(PatchTess patchTess,
	float3 uvw : SV_DomainLocation,
	const OutputPatch<HullOut, 3> triPatch)
{
	DomainOut dout;

	float3 pos =
		triPatch[0].PosL * uvw[0] + triPatch[1].PosL * uvw[1] + triPatch[2].PosL * uvw[2];

	MaterialData matData0 = gMaterialData[triPatch[0].MatIndex];
	//MaterialData matData1 = gMaterialData[triPatch[1].MatIndex];
	//MaterialData matData2 = gMaterialData[triPatch[2].MatIndex];

	uint displacementMapIndex1 = matData0.DisplacementMapIndex1;
	uint displacementMapIndex2 = matData0.DisplacementMapIndex2;

	float2 texC10 = triPatch[0].TexC1;
	float2 texC11 = triPatch[1].TexC1;
	float2 texC12 = triPatch[2].TexC1;

	float2 texC1 = texC10 * uvw[0] + texC11 * uvw[1] + texC12 * uvw[2];

	float2 texC20 = triPatch[0].TexC2;
	float2 texC21 = triPatch[1].TexC2;
	float2 texC22 = triPatch[2].TexC2;

	float2 texC2 = texC20 * uvw[0] + texC21 * uvw[1] + texC22 * uvw[2];

	float4 displacementMapSample1 = gTextureMaps[displacementMapIndex1].SampleLevel(
		gsamAnisotropicWrap, texC1, 0);

	float4 displacementMapSample2 = gTextureMaps[displacementMapIndex2].SampleLevel(
		gsamAnisotropicWrap, texC2, 0);

	float displacementMapSample = displacementMapSample1.x * 0.8f + displacementMapSample2.x * 0.4f;

	float3 normalL = normalize(
		triPatch[0].NormalL * uvw[0] +
		triPatch[1].NormalL * uvw[1] +
		triPatch[2].NormalL * uvw[2]
	);

	float3 tangentU = normalize(
		triPatch[0].TangentU * uvw[0] +
		triPatch[1].TangentU * uvw[1] +
		triPatch[2].TangentU * uvw[2]
	);

	pos += normalL * displacementMapSample;

	InstanceData instData = gInstanceData[triPatch[0].InstanceIndex];
	float4x4 world = instData.World;

	float4 posW = mul(float4(pos, 1.0f), world);

	dout.PosW = posW.xyz;
	dout.MatIndex = triPatch[0].MatIndex;
	dout.TexC1 = texC1;
	dout.TexC2 = texC2;
	dout.NormalW = mul(normalL, (float3x3)world);
	dout.TangentW = mul(tangentU, (float3x3)world);
	dout.PosH = mul(posW, gViewProj);

	return dout;
}

float4 PS(DomainOut pin) : SV_Target
{
	MaterialData matData = gMaterialData[pin.MatIndex];
	float4 diffuseAlbedo = matData.DiffuseAlbedo;
	float3 fresnelR0 = matData.FresnelR0;
	float roughness = matData.Roughness;
	uint diffuseMapIndex = matData.DiffuseMapIndex;
	uint normalMapIndex1 = matData.NormalMapIndex1;
	uint normalMapIndex2 = matData.NormalMapIndex2;

	diffuseAlbedo *= gTextureMaps[diffuseMapIndex].Sample(gsamAnisotropicWrap, pin.TexC1);

#ifdef ALPHA_TEST
	clip(diffuseAlbedo.a - 0.1f);
#endif

	// Interpolating normal can unnormalize it, so renormalize it.
	pin.NormalW = normalize(pin.NormalW);

	float4 normalMapSample1 = gTextureMaps[normalMapIndex1].Sample(
		gsamAnisotropicWrap, pin.TexC1);
	float3 bumpedNormalW1 = NormalSampleToWorldSpace(
		normalMapSample1.rgb, pin.NormalW, pin.TangentW);

	float4 normalMapSample2 = gTextureMaps[normalMapIndex2].Sample(
		gsamAnisotropicWrap, pin.TexC2);
	float3 bumpedNormalW2 = NormalSampleToWorldSpace(
		normalMapSample2.rgb, pin.NormalW, pin.TangentW);

	float3 bumpedNormalW = normalize(bumpedNormalW1 * 0.8f + bumpedNormalW2 * 0.4f);

	// Vector from point being lit to eye. 
	float3 toEyeW = gEyePosW - pin.PosW;
	float distToEye = length(toEyeW);
	toEyeW /= distToEye;

	// Light terms.
	float4 ambient = gAmbientLight * diffuseAlbedo;

	const float shininess = (1.0f - roughness) * normalMapSample1.a;
	Material mat = { diffuseAlbedo, fresnelR0, shininess };
	float3 shadowFactor = 1.0f;
	float4 directLight = ComputeLighting(gLights, mat, pin.PosW,
		bumpedNormalW, toEyeW, shadowFactor);

	float4 litColor = ambient + directLight;

#ifdef FOG
	float fogAmount = saturate((distToEye - gFogStart) / gFogRange);
	litColor = lerp(litColor, gFogColor, fogAmount);
#endif

	float3 r = reflect(-toEyeW, bumpedNormalW);
	float4 reflectionColor = gCubeMap.Sample(gsamLinearWrap, r);
	float3 fresnelFactor = SchlickFresnel(fresnelR0, bumpedNormalW, r);
	litColor.rgb += shininess * fresnelFactor * reflectionColor.rgb;

	// Common convention to take alpha from diffuse material.
	litColor.a = diffuseAlbedo.a;


	return litColor;
}
