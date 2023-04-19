// Defaults for number of lights.
#ifndef NUM_DIR_LIGHTS
#define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_POINT_LIGHTS
#define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
#define NUM_SPOT_LIGHTS 0
#endif

// Include structures and functions for lighting.
#include "LightingUtil.hlsl"

struct InstanceData
{
    float4x4 World;
    float4x4 TexTransform;
    uint MaterialIndex;
    uint3 instPad;
};

struct MaterialData
{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Roughness;
    float4x4 MatTransform1;
    float4x4 MatTransform2;
    uint DiffuseMapIndex;
    uint NormalMapIndex1;
    uint NormalMapIndex2;
    uint DisplacementMapIndex1;
    uint DisplacementMapIndex2;
    uint3 MatPad;
};

TextureCube gCubeMap : register(t0);
Texture2D    gTextureMaps[13] : register(t1);
StructuredBuffer<InstanceData> gInstanceData : register(t0, space1);
StructuredBuffer<MaterialData> gMaterialData : register(t1, space1);

SamplerState gsamPointWrap  : register(s0);
SamplerState gsamPointClamp  : register(s1);
SamplerState gsamLinearWrap  : register(s2);
SamplerState gsamLinearClamp  : register(s3);
SamplerState gsamAnisotropicWrap  : register(s4);
SamplerState gsamAnisotropicClamp  : register(s5);

// Constant data that varies per material.
cbuffer cbPass : register(b0)
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

    // Indices [0, NUM_DIR_LIGHTS) are directional lights;
    // indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
    // indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
    // are spot lights for a maximum of MaxLights per object.
    Light gLights[MaxLights];

    float4 gFogColor;
    float gFogStart;
    float gFogRange;
};

float3 NormalSampleToWorldSpace(
    float3 normalMapSample,
    float3 unitNormalW,
    float3 tangentW
)
{
    // [0, 1] to [-1, 1]
    float3 normalT = 2.0f * normalMapSample - 1.0f;

    float3 N = unitNormalW;
    float3 T = normalize(tangentW - dot(tangentW, N) * N);
    float3 B = cross(N, T);

    float3x3 TBN = float3x3(T, B, N);

    float3 bumpedNormalW = mul(normalT, TBN);

    return bumpedNormalW;
}