Texture2D gOceanHTilde0   : register(t0);
Texture2D gOceanDisplacement   : register(t1);

SamplerState gsamPointWrap        : register(s0);
SamplerState gsamPointClamp       : register(s1);
SamplerState gsamLinearWrap       : register(s2);
SamplerState gsamLinearClamp      : register(s3);
SamplerState gsamAnisotropicWrap  : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

struct VertexIn
{
	float3 PosL    : POSITION;
	float2 TexC    : TEXCOORD;
};

struct VertexOut
{
	float4 PosH    : SV_POSITION;
	float2 TexC    : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
	VertexOut vout = (VertexOut)0.0f;

	// Already in homogeneous clip space.
	vout.PosH = float4(vin.PosL, 1.0f);

	vout.TexC = vin.TexC;

	return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
	float4 sampled = gOceanHTilde0.Sample(gsamPointWrap, pin.TexC);
	sampled *= 10000.0f;
	sampled = clamp(sampled, 0.1f, 1.0f);

	return float4(sampled.rgb, 1.0f);
}

