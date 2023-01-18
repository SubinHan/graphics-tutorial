struct Vertex
{
	XMFLOAT3 Pos;
	XMFLOAT3 Normal;
	XMFLOAT2 Tex0;
};

D3D11_INPUT_ELEMENT_DESC vertexDesc[] =
{
	{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
	{"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

void VS(float3 iPosL : POSITION,
	float3 iNormalL : NORMAL,
	float2 iTex0 : TEXCOORD,
	out float4 oPosH : SV_POSITION,
	out float3 oPosW : POSITION,
	out float3 oNormalW : NORMAL,
	out float2 oTex0 : TEXCOORD0,
	out float oFog : TEXCOORD1)
{

}

void PS(float4 posH : SV_POSITION,
	float3 posW : POSITION,
	float3 normalW : NORMAL,
	float2 tex0 : TEXCOORD0,
	float fog : TEXCOORD1)
{

}

cbuffer cPerObject : register(b0)
{
	float4x4 gWorldViewProj;
};

void VS(float3 iPos : POSITION,
	float4 iColor : COLOR,
	out float4 oPos : SV_POSITION,
	out float4 oColor : COLOR)
{
	oPos = mul(float4(iPos, 1.0f), gWorldViewProj);
	oColor = iColor;
}

float4 PS(float4 posH : SV_POSITION, float4 color : COLOR) : SV_Target
{
	return color;
}

float4 main(float4 pos : POSITION) : SV_POSITION
{
	return pos;
}