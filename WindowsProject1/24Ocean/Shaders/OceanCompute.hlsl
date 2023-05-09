#include "Complex.hlsl"

cbuffer cbFftConstants : register(b0)
{
	uint gSize;
	bool gIsInverse;
}

RWTexture2D<float4> gInput : register(u0);
RWTexture2D<float4> gOutput : register(u1);

#define N 256

void BitReversal(uint2 xy)
{
	int rev = 0;
	for (int j = 1, target = xy.x; j < gSize; j <<= 1, target >>= 1)
	{
		rev = (rev << 1) + (target & 1);
	}
	if (xy.x < rev)
	{
		// swap
		float4 temp = gOutput[xy];
		gOutput[xy] = gOutput[uint2(rev, xy.y)];
		gOutput[uint2(rev, xy.y)] = temp;
	}
}

void FastFourierTransform1d(uint2 xy)
{
	static const float PI = 3.141592f;

	for (int len = 2, lenHalf = 1; len <= gSize; len <<= 1, lenHalf <<= 1)
	{
		int j = xy.x % len;

		if (j < lenHalf)
		{
			float theta = 2.0f * PI * j / len * (gIsInverse ? -1 : 1);
			float2 wk = { cos(theta), sin(theta) };

			float2 even = gOutput[xy];
			float2 odd = gOutput[uint2(xy.x + lenHalf, xy.y)];

			gOutput[xy] =
				float4(even + ComplexMul(wk, odd), 0.0f, 0.0f);
			gOutput[uint2(xy.x + lenHalf, xy.y)] =
				float4(even - ComplexMul(wk, odd), 0.0f, 0.0f);
		}
		AllMemoryBarrierWithGroupSync();
	}

	if (gIsInverse)
	{
		gOutput[xy] /= gSize;
	}
}

void Transpose(uint2 xy)
{
	gInput[xy] = gOutput[xy.yx];
}

void Shift(uint2 xy)
{
	const uint sizeHalf = gSize / 2;
	if (xy.y < sizeHalf)
	{
		if (xy.x < sizeHalf)
		{
			gOutput[xy] = gInput[uint2(xy.x + sizeHalf, xy.y + sizeHalf)];
		}
		else
		{
			gOutput[xy] = gInput[uint2(xy.x - sizeHalf, xy.y + sizeHalf)];
		}
	}
	else
	{
		if (xy.x < sizeHalf)
		{
			gOutput[xy] = gInput[uint2(xy.x + sizeHalf, xy.y - sizeHalf)];
		}
		else
		{
			gOutput[xy] = gInput[uint2(xy.x - sizeHalf, xy.y - sizeHalf)];
		}
	}
}

[numthreads(N, 1, 1)]
void ShiftCS(int3 dispatchThreadID : SV_DispatchThreadID)
{
	Shift(dispatchThreadID.xy);
}

[numthreads(N, 1, 1)]
void BitReversalCS(int3 dispatchThreadID : SV_DispatchThreadID)
{
	BitReversal(dispatchThreadID.xy);
}

[numthreads(N, 1, 1)]
void Fft1dCS(int3 dispatchThreadID : SV_DispatchThreadID)
{
	FastFourierTransform1d(dispatchThreadID.xy);
}

[numthreads(N, 1, 1)]
void TransposeCS(int3 dispatchThreadID : SV_DispatchThreadID)
{
	Transpose(dispatchThreadID.xy);
}