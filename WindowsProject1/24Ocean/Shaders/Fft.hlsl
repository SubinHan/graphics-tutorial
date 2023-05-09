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
	for(int i = 0; i < gSize; ++i)
	{
		gOutput[int2(i, row)] = gInput[int2(i, row)];
	}

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
			int temp = gOutput[int2(i, row)];
			gOutput[int2(i, row)] = gOutput[int2(rev, row)];
			gOutput[int2(rev, row)] = temp;
		}
	}
}

void FastFourierTransform1d(int row)
{
	static const float PI = 3.141592f;

	for (int len = 2, lenHalf = 1; len <= gSize; len <<= 1, lenHalf <<= 1)
	{
		for (int i = 0; i < gSize; i += len)
		{
			for (int j = 0; j < lenHalf; ++j)
			{
				int cur = i + j;

				float theta = 2.0f * PI * j / len * (gIsInverse ? -1 : 1);
				float2 wk = { cos(theta), sin(theta) };

				float2 even = gOutput[int2(cur, row)];
				float2 odd = gOutput[int2(cur + lenHalf, row)];

				gOutput[int2(cur, row)] = 
					float4(even + ComplexMul(wk, odd), 0.0f, 0.0f);
				gOutput[int2(cur + lenHalf, row)] = 
					float4(even - ComplexMul(wk, odd), 0.0f, 0.0f);
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
	const int sizeHalf = gSize / 2;
	if(row < sizeHalf)
	{
		if(col < sizeHalf)
		{
			gOutput[uint2(col, row)] = gInput[uint2(col + sizeHalf, row + sizeHalf)];
		}
		else
		{
			gOutput[uint2(col, row)] = gInput[uint2(col - sizeHalf, row + sizeHalf)];
		}
	}
	else
	{
		if (col < sizeHalf)
		{
			gOutput[uint2(col, row)] = gInput[uint2(col + sizeHalf, row - sizeHalf)];
		}
		else
		{
			gOutput[uint2(col, row)] = gInput[uint2(col - sizeHalf, row - sizeHalf)];
		}
	}

	DeviceMemoryBarrier();
	gInput[uint2(col, row)] = gOutput[uint2(col, row)];
	DeviceMemoryBarrier();

	static const float PI = 3.141592f;

	float real = 0.0f;
	float imag = 0.0f;
	for (int m = 0; m < gSize; ++m)
	{
		float kx = 2 * PI * (m - gSize / 2.0f);
		for (int n = 0; n < gSize; ++n)
		{
			const float kz = 2 * PI * (n - gSize / 2.0f);
			const float2 k = { kx, kz };
			const float2 x = { col / gSize - 0.5f, row / gSize - 0.5f };
			const float2 hTildeC = ComplexMul(gInput[uint2(m, n)].xy, dot(k, x));
			real += hTildeC * cos(2.0f * PI *
				(float(row * m) / gSize + float(col * n) / gSize)) / (gSize * gSize);
			imag -= hTildeC * sin(2.0f * PI *
				(float(row * m) / gSize + float(col * n) / gSize)) / (gSize * gSize);
		}
	}

	gOutput[uint2(col, row)] = float4(real, imag, 0.0f, 0.0f);
}

void Shift(int row)
{
	const int sizeHalf = gSize / 2;
	if(row < sizeHalf)
	{
		for (int i = 0; i < sizeHalf; ++i)
		{
			gOutput[uint2(i, row)] = gInput[uint2(i + sizeHalf, row + sizeHalf)];
		}
		for (int i = sizeHalf; i < gSize; ++i)
		{
			gOutput[uint2(i, row)] = gInput[uint2(i - sizeHalf, row + sizeHalf)];
		}
	}
	else
	{
		for(int i = 0; i < sizeHalf; ++i)
		{
			gOutput[uint2(i, row)] = gInput[uint2(i + sizeHalf, row - sizeHalf)];
		}
		for (int i = sizeHalf; i < gSize; ++i)
		{
			gOutput[uint2(i, row)] = gInput[uint2(i - sizeHalf, row - sizeHalf)];
		}
	}
	DeviceMemoryBarrier();
	for(int i = 0; i < gSize; ++i)
	{
		gInput[uint2(i, row)] = gOutput[uint2(i, row)];
	}
}

[numthreads(1, N, 1)]
void Fft2dCS(
	int3 groupThreadID : SV_GroupThreadID,
	int3 dispatchThreadID : SV_DispatchThreadID)
{
	// 가운데가 중심이므로 0,0이 중심이 되도록 shift해야 함!
	Shift(dispatchThreadID.y);
	DeviceMemoryBarrier();

	BitReversal(dispatchThreadID.y);
	FastFourierTransform1d(dispatchThreadID.y);
	DeviceMemoryBarrier();
	Transpose(dispatchThreadID.y);
	DeviceMemoryBarrier();
	BitReversal(dispatchThreadID.y);
	FastFourierTransform1d(dispatchThreadID.y);
	DeviceMemoryBarrier();
	Transpose(dispatchThreadID.y);
	DeviceMemoryBarrier();

	for (int i = 0; i < gSize; ++i)
	{
		gOutput[uint2(i, dispatchThreadID.y)] *= 10.0f;
	}

	//DiscreteFourierTransform(dispatchThreadID.x, dispatchThreadID.y);
	//gOutput[dispatchThreadID.xy] *= 10.0f;
}
