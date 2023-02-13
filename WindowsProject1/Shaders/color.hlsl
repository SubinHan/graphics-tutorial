
cbuffer cbPerObject : register(b0)
{
	float4x4 gWorldViewProj;
};

struct VPosData
{
	float3 Pos : POSITION;
};

struct VColorData
{
	float4 Color : COLOR;
};


struct VertexOut
{
	float4 PosH  : SV_POSITION;
	float4 Color : COLOR;
};

VertexOut VS(VPosData vpos, VColorData vcolor)
{
	VertexOut vout;

	// Transform to homogeneous clip space.
	vout.PosH = mul(float4(vpos.Pos, 1.0f), gWorldViewProj);

	// Just pass vertex color into the pixel shader.
	vout.Color = vcolor.Color;

	return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	return pin.Color;
}