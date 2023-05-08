#include "Complex.hlsl"

cbuffer cbFftConstants : register(b0)
{
	uint gSize;
	bool gIsInverse;
}

RWTexture2D<float4> gInput : register(u0);
RWTexture2D<float4> gOutput : register(u1);

groupshared int rev;

#define N 256

void BitReversal(int row)
{
	for (int i = 0; i < gSize; ++i)
	{
		int rev = 0;
		for (int j = 1, target = i; j < gSize; j <<= 1, target >>= 1)
		{
			rev = (rev << 1) + (target & 1);
		}
		if (i < rev)
		{
			// swap
			int temp = gInput[int2(i, row)];
			gOutput[int2(i, row)] = gInput[int2(rev, row)];
			gOutput[int2(rev, row)] = temp;
		}
	}
}

void FastFourierTransform1d(int row)
{
	static const float PI = 3.141592f;

	for (int len = 2; len <= gSize; len <<= 1)
	{
		float x = 2.0f * PI / len * (gIsInverse ? -1 : 1);
		float2 diff = { cos(x), sin(x) };
		for (int i = 0; i < gSize; i += len)
		{
			float2 e = { 1.0f, 0.0f };
			const int lenHalf = len / 2;
			for (int j = 0; j < lenHalf; ++j)
			{
				int cur = i + j;
				float2 even = gOutput[int2(cur, row)];
				float2 oddE = ComplexMul(gOutput[int2(cur + lenHalf, row)].xy, e);
				gOutput[int2(cur, row)] = float4(even + oddE, 0.0f, 0.0f);
				gOutput[int2(cur + lenHalf, row)] = float4(even - oddE, 0.0f, 0.0f);
				e = ComplexMul(e, diff);
			}
		}
	}

	if (gIsInverse)
	{
		for (int i = 0; i < gSize; ++i)
		{
			gOutput[int2(i, row)] /= gSize;
		}
	}
}

void Transpose(int row)
{
	for(int i = 0; i < gSize; ++i)
	{
		gInput[uint2(i, row)] = gOutput[uint2(row, i)];
	}
}

void DiscreteFourierTransform(int col, int row)
{
	static const float PI = 3.141592f;

	float real = 0.0f;
	float imag = 0.0f;
	for (int m = 0; m < gSize; ++m)
	{
		for (int n = 0; n < gSize; ++n)
		{
			real += gInput[uint2(m, n)] * cos(2.0f * PI *
				(float(row * m) / gSize + float(col * n) / gSize)) / (gSize * gSize);
			imag -= gInput[uint2(m, n)] * sin(2.0f * PI *
				(float(row * m) / gSize + float(col * n) / gSize)) / (gSize * gSize);
		}
	}

	gOutput[uint2(col, row)] = float4(real, imag, 0.0f, 0.0f);
}

[numthreads(1, N, 1)]
void Fft2dCS(
	int3 groupThreadID : SV_GroupThreadID,
	int3 dispatchThreadID : SV_DispatchThreadID)
{
	BitReversal(dispatchThreadID.y);
	FastFourierTransform1d(dispatchThreadID.y);
	DeviceMemoryBarrierWithGroupSync();
	Transpose(dispatchThreadID.y);
	DeviceMemoryBarrierWithGroupSync();
	BitReversal(dispatchThreadID.y);
	FastFourierTransform1d(dispatchThreadID.y);

	for (int i = 0; i < gSize; ++i)
	{
		gOutput[uint2(i, dispatchThreadID.y)] *= 50.0f;
	}


	//DiscreteFourierTransform(dispatchThreadID.x, dispatchThreadID.y);

	//gOutput[dispatchThreadID.xy] *= 50.0f;
}