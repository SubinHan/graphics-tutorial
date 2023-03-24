cbuffer cbSettings : register(b0)
{
	int gBlurRadius;

	float w0;
	float w1;
	float w2;
	float w3;
	float w4;
	float w5;
	float w6;
	float w7;
	float w8;
	float w9;
	float w10;
}

static const int gMaxBlurRadius = 5;

Texture2D gInput : register(t0);
RWTexture2D<float4> gOutput : register(u0);

#define N 256
#define CacheSize (N + 2 * gMaxBlurRadius)
groupshared float4 gCache[CacheSize];

float4 Gaussian(float4 x);

[numthreads(N, 1, 1)]
void HorzBlurCS( 
	int3 groupThreadID : SV_GroupThreadID,
	int3 dispatchThreadID: SV_DispatchThreadID )
{
	float weights[11] = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10 };

	if(groupThreadID.x < gBlurRadius)
	{
		int x = max(dispatchThreadID.x - gBlurRadius, 0);
		gCache[groupThreadID.x] = gInput[int2(x, dispatchThreadID.y)];
	}

	if(groupThreadID.x >= N-gBlurRadius)
	{
		int x = min(dispatchThreadID.x + gBlurRadius, gInput.Length.x - 1);
		gCache[groupThreadID.x + 2 * gBlurRadius] = gInput[int2(x, dispatchThreadID.y)];
	}

	gCache[groupThreadID.x + gBlurRadius] =
		gInput[min(dispatchThreadID.xy, gInput.Length.xy - 1)];

	GroupMemoryBarrierWithGroupSync();

	float4 blurColor = float4(0, 0, 0, 0);
	float4 normalizeFactor = float4(0, 0, 0, 0);

	for(int i = -gBlurRadius; i <= gBlurRadius; ++i)
	{
		int k = groupThreadID.x + gBlurRadius + i;

		const float4 center = gCache[groupThreadID.x + gBlurRadius];
		const float4 weight = Gaussian(abs(gCache[k] - center))
			* Gaussian(abs(i));

		normalizeFactor += weight;

		blurColor += gCache[k] * weight;
	}

	blurColor /= normalizeFactor;
	
	gOutput[dispatchThreadID.xy] = blurColor;
}

[numthreads(1, N, 1)]
void VertBlurCS(
	int3 groupThreadID : SV_GroupThreadID,
	int3 dispatchThreadID : SV_DispatchThreadID)
{
	float weights[11] = { w0, w1, w2, w3, w4, w5, w6, w7, w8, w9, w10 };

	if (groupThreadID.y < gBlurRadius)
	{
		int y = max(dispatchThreadID.y - gBlurRadius, 0);
		gCache[groupThreadID.y] = gInput[int2( dispatchThreadID.x, y)];
	}

	if (groupThreadID.y >= N - gBlurRadius)
	{
		int y = min(dispatchThreadID.y + gBlurRadius, gInput.Length.y - 1);
		gCache[groupThreadID.y + 2*gBlurRadius] = gInput[int2(dispatchThreadID.x, y)];
	}

	gCache[groupThreadID.y + gBlurRadius] =
		gInput[min(dispatchThreadID.xy, gInput.Length.xy - 1)];

	GroupMemoryBarrierWithGroupSync();

	float4 blurColor = float4(0, 0, 0, 0);
	float4 normalizeFactor = float4(0, 0, 0, 0);

	for (int i = -gBlurRadius; i <= gBlurRadius; ++i)
	{
		int k = groupThreadID.y + gBlurRadius + i;

		const float4 center = gCache[groupThreadID.y + gBlurRadius];
		const float4 weight = Gaussian(abs(gCache[k] - center))
			* Gaussian(abs(i));

		normalizeFactor += weight;

		blurColor += gCache[k] * weight;
	}

	blurColor /= normalizeFactor;

	gOutput[dispatchThreadID.xy] = blurColor;
}

float4 Gaussian(float4 x)
{
	return exp(-pow(x, 2));
}