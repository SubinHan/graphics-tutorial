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
	const float textureDepthY =		1.0 / 9.0;
	const float textureDepthZ = 	2.0 / 9.0;
	const float textureSlopeXX = 	3.0 / 9.0;
	const float textureSlopeXY = 	4.0 / 9.0;
	const float textureSlopeXZ = 	5.0 / 9.0;
	const float textureSlopeZX = 	6.0 / 9.0;
	const float textureSlopeZY = 	7.0 / 9.0;
	const float textureSlopeZZ = 	8.0 / 9.0;

	float4 displacementX = gOceanMap.SampleLevel(gsamAnisotropicClamp, float3(texC, textureDepthX), 0);
	float4 displacementY = gOceanMap.SampleLevel(gsamAnisotropicClamp, float3(texC, textureDepthY), 0);
	float4 displacementZ = gOceanMap.SampleLevel(gsamAnisotropicClamp, float3(texC, textureDepthZ), 0);

	float3 displacement = float3(
		0.0f,
		displacementX.x,
		0.0f);

	pos += displacement;

	float4 slopeXX = gOceanMap.SampleLevel(gsamAnisotropicClamp, float3(texC, textureSlopeXX), 0);
	//float4 slopeXY = gOceanMap.SampleLevel(gsamAnisotropicClamp, float3(texC, textureSlopeXY), 0);
	//float4 slopeXZ = gOceanMap.SampleLevel(gsamAnisotropicClamp, float3(texC, textureSlopeXZ), 0);

	float4 slopeZX = gOceanMap.SampleLevel(gsamAnisotropicClamp, float3(texC, textureSlopeZX), 0);
	//float4 slopeZY = gOceanMap.SampleLevel(gsamAnisotropicClamp, float3(texC, textureSlopeZY), 0);
	//float4 slopeZZ = gOceanMap.SampleLevel(gsamAnisotropicClamp, float3(texC, textureSlopeZZ), 0);
	
	float slopeScale = 0.5f;

	float3 slopeX = { 1.0f, slopeXX.x * slopeScale, 0.0f };
	float3 slopeZ = { 0.0f, slopeZX.x * slopeScale, 1.0f };
	float3 normalL = normalize(cross(slopeZ, slopeX));
	//normalL = normalize(float3(slopeXX.x, 1.0f, slopeZX.x));

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
	dout.NormalW = mul(normalL, (float3x3)gWorld);
	dout.TangentW = mul(tangentU, (float3x3)world);
	dout.PosH = mul(posW, gViewProj);

	return dout;
}

float4 PS(DomainOut pin) : SV_Target
{
	// Fetch the material data.
	MaterialData matData = gMaterialData[gMaterialIndex];
	float4 diffuseAlbedo = matData.DiffuseAlbedo * 1.0;
	uint diffuseMapIndex = matData.DiffuseMapIndex;

	pin.NormalW = normalize(pin.NormalW);

	// Vector from point being lit to eye. 
	float3 toEyeW = gEyePosW - pin.PosW;
	float distToEye = length(toEyeW);
	toEyeW /= distToEye;

	float nSnell = 1.34f;

	float reflectivity;
	float3 nI = normalize(toEyeW);
	float3 nN = pin.NormalW;
	float costhetai = dot(nI, nN);
	float thetai = acos(costhetai);
	float sinthetat = sin(thetai) / nSnell;
	float thetat = asin(sinthetat);

	if (thetai == 0.0f)
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

	float3 r = reflect(-toEyeW, pin.NormalW);

	//reflectivity = (exp(reflectivity) - 1) / exp(1);
	float3 lightDir = normalize(float3(1.0f, -1.0f, 1.0f));
	float sunColor = pow(max(0.0, dot(r, lightDir)), 720.0) * 210.0;

	float4 reflectionColor = gCubeMap.Sample(gsamAnisotropicClamp, r);

	float3 Ci = ((reflectivity * reflectionColor.xyz + (1 - reflectivity) * diffuseAlbedo.xyz));
	//Ci = float3(reflectivity, reflectivity, reflectivity);


	//float power = 8.0;
	//float distortion = 0.2;
	//float scatterStrength = 0.5;
	//float3 scatterColor = float3(0.05, 0.8, 0.7);

	//float3 sunLightColor = float3(1.0f, 1.0f, 1.0f);
	//float a = (nSnell - 1) / (nSnell + 1);
	//a = a * a;
	//float3 b = float3(a, a, a);
	//float3 sunLightReflectivity = SchlickFresnel(float3(0.05, 0.05, 0.05), pin.NormalW, r);
	//float3 sunLight = sunLightReflectivity * sunLightColor;

	//Ci += sunLight;

	//float3 h = normalize(-lightDir + pin.NormalW * distortion);
	//float vDotH = pow(saturate(dot(-toEyeW, -h)), power) * 0.4f;

	//Ci += scatterStrength * pow((1.0 - distToEye / 400), 4.0) * 2.0f * vDotH * scatterColor;

	//Ci = dist * ((sunLightReflectivity * reflectionColor.xyz + (1 - sunLightReflectivity) * diffuseAlbedo.xyz)) + (1 - dist) * float3(0.1f, 0.1f, 0.1f);

	//float fresnel = (0.04 + (1.0 - 0.04) * (pow(1.0 - max(0.0, dot(pin.NormalW, toEyeW)), 5.0)));

	//float3 reflection = reflectionColor.xyz + float3(sunColor.xxx);

	//Ci = fresnel * reflection + (1.0f - fresnel) * diffuseAlbedo.xyz;

	//return float4(Ci, 1.0f);
	return float4(reflectivity.xxx, 1.0f);
	//return float4(pin.NormalW.x, pin.NormalW.y * 1.0f, pin.NormalW.z, 1.0f);
}