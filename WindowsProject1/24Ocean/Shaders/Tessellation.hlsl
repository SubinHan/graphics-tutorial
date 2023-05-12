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
	float2 TexC    : TEXCOORD;
};

struct HullOut
{
	float3 PosL    : POSITION;
	float3 NormalL : NORMAL;
	float3 TangentU : TANGENT;
	float2 TexC    : TEXCOORD;
};

struct DomainOut
{
	float4 PosH						: SV_POSITION;
	float4 ShadowPosH : POSITION0;
	float4 SsaoPosH   : POSITION1;
	float3 PosW    : POSITION2;
	float3 NormalW					: NORMAL;
	float3 TangentW					: TANGENT;
	float2 TexC						: TEXCOORD;
};

struct PatchTess
{
	float EdgeTess[3]	 : SV_TessFactor;
	float InsideTess : SV_InsideTessFactor;
};


VertexOut VS(VertexIn vin)
{
	VertexOut vout;
	
	vout.PosL = vin.PosL;
	vout.NormalL = vin.NormalL;
	vout.TangentU = vin.TangentU;
	vout.TexC = vin.TexC;

	return vout;
}

PatchTess ConstantHS(InputPatch<VertexOut, 3> patch, uint patchID : SV_PrimitiveID)
{
	PatchTess pt;

	float4x4 world = gWorld;

	float3 centerL = 0.333f * (patch[0].PosL + patch[1].PosL + patch[2].PosL);
	float3 centerW = mul(float4(centerL, 1.0f), world).xyz;

	float d = distance(centerW, gEyePosW);

	const float d0 = 0.0f;
	const float d1 = 70.0f;

	float tess = 2.0f + 6.0f * saturate((d1 - d) / (d1 - d0));
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
	hout.TexC = p[i].TexC;

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

	MaterialData matData = gMaterialData[gMaterialIndex];

	float2 texC0 = triPatch[0].TexC;
	float2 texC1 = triPatch[1].TexC;
	float2 texC2 = triPatch[2].TexC;

	float2 texC = texC0 * uvw[0] + texC1 * uvw[1] + texC2 * uvw[2];

	const float textureDepthX = 0.0f;
	const float textureDepthY = 0.1f;
	const float textureDepthZ = 0.2f;
	const float textureSlopeXX = 0.333f;
	const float textureSlopeXY = 0.444f;
	const float textureSlopeXZ = 0.555f;
	const float textureSlopeZX = 0.666f;
	const float textureSlopeZY = 0.777f;
	const float textureSlopeZZ = 0.888f;

	float4 displacementX = gOceanMap.SampleLevel(gsamPointWrap, float3(texC, textureDepthX), 0);
	float4 displacementY = gOceanMap.SampleLevel(gsamPointWrap, float3(texC, textureDepthY), 0);
	float4 displacementZ = gOceanMap.SampleLevel(gsamPointWrap, float3(texC, textureDepthZ), 0);

	float3 displacement = float3(
		displacementX.x,
		displacementY.x * 400.f,
		displacementZ.x * 400.f);

	pos += displacement * 1500.f;

	float4 slopeXX = gOceanMap.SampleLevel(gsamPointWrap, float3(texC, textureSlopeXX), 0);
	float4 slopeXY = gOceanMap.SampleLevel(gsamPointWrap, float3(texC, textureSlopeXY), 0);
	float4 slopeXZ = gOceanMap.SampleLevel(gsamPointWrap, float3(texC, textureSlopeXZ), 0);

	float4 slopeZX = gOceanMap.SampleLevel(gsamPointWrap, float3(texC, textureSlopeZX), 0);
	float4 slopeZY = gOceanMap.SampleLevel(gsamPointWrap, float3(texC, textureSlopeZY), 0);
	float4 slopeZZ = gOceanMap.SampleLevel(gsamPointWrap, float3(texC, textureSlopeZZ), 0);
	
	float3 slopeX = { 0.00000015f, slopeXY.x, slopeXZ.x };
	float3 slopeZ = { slopeZX.x, slopeZY.x, 0.00000015f };
	float3 normalL = cross(normalize(slopeX), normalize(slopeZ));
	normalL.y = abs(normalL.y);

	float3 tangentU = normalize(
		triPatch[0].TangentU * uvw[0] +
		triPatch[1].TangentU * uvw[1] +
		triPatch[2].TangentU * uvw[2]
	);
	
	float4x4 world = gWorld;

	float4 posW = mul(float4(pos, 1.0f), world);

	dout.PosW = posW.xyz;
	dout.TexC = mul(texC, matData.MatTransform).xy;
	dout.SsaoPosH = mul(posW, gViewProjTex);
	dout.ShadowPosH = mul(posW, gShadowTransform);
	dout.NormalW = mul(normalL, (float3x3)world);
	dout.TangentW = mul(tangentU, (float3x3)world);
	dout.PosH = mul(posW, gViewProj);

	return dout;
}

float4 PS(DomainOut pin) : SV_Target
{
	// Fetch the material data.
	MaterialData matData = gMaterialData[gMaterialIndex];
	float4 diffuseAlbedo = matData.DiffuseAlbedo;
	uint diffuseMapIndex = matData.DiffuseMapIndex;

	pin.NormalW = normalize(pin.NormalW);
	
	// Vector from point being lit to eye. 
	float3 toEyeW = gEyePosW - pin.PosW;
	float distToEye = length(toEyeW);
	toEyeW /= distToEye;
	
	float nSnell = 1.34f;

	float reflectivity;
	float3 nI = normalize(toEyeW);
	float3 nN = normalize(pin.NormalW);
	float costhetai = dot(nI, nN) ;
	float thetai = acos(costhetai);
	float sinthetat = sin(thetai) / nSnell;
	float thetat = asin(sinthetat);

	if(thetai == 0.0f)
	{
		reflectivity = (nSnell - 1) / (nSnell + 1);
		reflectivity *= reflectivity;
	}
	else
	{
		float fs = sin(thetat - thetai) / sin(thetat + thetai);
		float ts = tan(thetat - thetai) / tan(thetat + thetai);
		reflectivity = 0.5f * (fs * fs + ts * ts);
	}
	reflectivity = clamp(reflectivity, 0.0f, 1.0f);

	diffuseAlbedo *= gTextureMaps[diffuseMapIndex].Sample(gsamAnisotropicWrap, pin.TexC);
	float3 r = reflect(-toEyeW, pin.NormalW);


	float4 reflectionColor = gCubeMap.Sample(gsamLinearWrap, r);

	//reflectionColor.xyz = float3(0.69f, 0.84f, 1.0f);
	float3 Ci = (reflectivity * reflectionColor + (1 - reflectivity) * diffuseAlbedo);
	
	return float4(Ci, 1.0f);
}