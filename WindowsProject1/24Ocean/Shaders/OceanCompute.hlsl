#include "Complex.hlsl"

cbuffer cbFftConstants : register(b0)
{
	uint gSize;
	bool gIsInverse;
}

RWTexture2DArray<float4> gInput : register(u0);
RWTexture2DArray<float4> gOutput : register(u1);

#define N 512

void BitReversal(uint3 xyz)
{
	int rev = 0;
	for (int j = 1, target = xyz.x; j < gSize; j <<= 1, target >>= 1)
	{
		rev = (rev << 1) + (target & 1);
	}
	if (xyz.x < rev)
	{
		// swap
		float4 temp = gOutput[xyz];
		gOutput[xyz] = gOutput[uint3(rev, xyz.y, xyz.z)];
		gOutput[uint3(rev, xyz.y, xyz.z)] = temp;
	}
}

void FastFourierTransform1d(uint3 xyz)
{
	static const float PI = 3.141592f;

	for (int len = 2, lenHalf = 1; len <= gSize; len <<= 1, lenHalf <<= 1)
	{
		int j = xyz.x % len;

		if (j < lenHalf)
		{
			float theta = 2.0f * PI * j / len * (gIsInverse ? -1 : 1);
			float2 wk = { cos(theta), sin(theta) };

			float2 even = gOutput[xyz];
			float2 odd = gOutput[uint3(xyz.x + lenHalf, xyz.y, xyz.z)];

			gOutput[xyz] =
				float4(even + ComplexMul(wk, odd), 0.0f, 0.0f);
			gOutput[uint3(xyz.x + lenHalf, xyz.y, xyz.z)] =
				float4(even - ComplexMul(wk, odd), 0.0f, 0.0f);
		}
		DeviceMemoryBarrierWithGroupSync();
	}

	if (gIsInverse)
	{
		gOutput[xyz] /= gSize;
	}
}

void Transpose(uint3 xyz)
{
	gInput[xyz] = gOutput[xyz.yxz];
}

void Shift(uint3 xyz)
{
	const uint sizeHalf = gSize / 2;
	if (xyz.y < sizeHalf)
	{
		if (xyz.x < sizeHalf)
		{
			gOutput[xyz] = gInput[uint3(xyz.x + sizeHalf, xyz.y + sizeHalf, xyz.z)];
		}
		else
		{
			gOutput[xyz] = gInput[uint3(xyz.x - sizeHalf, xyz.y + sizeHalf, xyz.z)];
		}
	}
	else
	{
		if (xyz.x < sizeHalf)
		{
			gOutput[xyz] = gInput[uint3(xyz.x + sizeHalf, xyz.y - sizeHalf, xyz.z)];
		}
		else
		{
			gOutput[xyz] = gInput[uint3(xyz.x - sizeHalf, xyz.y - sizeHalf, xyz.z)];
		}
	}
}

void MakeDisplacement(int3 xyz)
{
	gOutput[uint3(xyz.xy, 0)] = float4(
		gInput[uint3(xyz.xy, 0)].x,
		gInput[uint3(xyz.xy, 1)].x,
		gInput[uint3(xyz.xy, 2)].x,
		0.0f);
}

void CalculateNormal(int3 xyz)
{
	
}

[numthreads(N, 1, 1)]
void ShiftCS(int3 dispatchThreadID : SV_DispatchThreadID)
{
	Shift(dispatchThreadID.xyz);
}

[numthreads(N, 1, 1)]
void BitReversalCS(int3 dispatchThreadID : SV_DispatchThreadID)
{
	BitReversal(dispatchThreadID.xyz);
}

[numthreads(N, 1, 1)]
void Fft1dCS(int3 dispatchThreadID : SV_DispatchThreadID)
{
	FastFourierTransform1d(dispatchThreadID.xyz);
}

[numthreads(N, 1, 1)]
void TransposeCS(int3 dispatchThreadID : SV_DispatchThreadID)
{
	Transpose(dispatchThreadID.xyz);
}


[numthreads(N, 1, 1)]
void MakeDisplacementCS(int3 dispatchThreadID : SV_DispatchThreadID)
{
	MakeDisplacement(dispatchThreadID.xyz);
}

[numthreads(N, 1, 1)]
void CalculateNormalCS(int3 dispatchThreadID : SV_DispatchThreadID)
{
	CalculateNormal(dispatchThreadID.xyz);
}
