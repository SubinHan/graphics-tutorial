
cbuffer cbSettings : register(b0)
{
	float gWaveConstant0;
	float gWaveConstant1;
	float gWaveConstant2;

	float gDisturbMagnitude;
	int2 gDisturbIndex;
}

RWTexture2D<float> gPrevSolInput : register(u0);
RWTexture2D<float> gCurrSolInput : register(u1);

#define N 16

[numthreads(N, N, 1)]
void CS(
	int3 groupThreadID : SV_GroupThreadID,
	int3 dispatchThreadID : SV_DispatchThreadID)
{
	int x = dispatchThreadID.x;
	int y = dispatchThreadID.y;

	gPrevSolInput[int2(x,y)] =
		gWaveConstant0 * gPrevSolInput[int2(x,y)].r +
		gWaveConstant1 * gCurrSolInput[int2(x,y)].r +
		gWaveConstant2 * (gCurrSolInput[int2(x - 1, y)].r +
			gCurrSolInput[int2(x + 1, y)].r +
			gCurrSolInput[int2(x, y - 1)].r +
			gCurrSolInput[int2(x, y + 1)].r);

	GroupMemoryBarrierWithGroupSync();

	float temp = gPrevSolInput[int2(x,y)];
	gPrevSolInput[int2(x,y)] = gCurrSolInput[int2(x,y)];
	gCurrSolInput[int2(x,y)] = temp;

	if (gDisturbIndex.x == dispatchThreadID.x
		&& gDisturbIndex.y == dispatchThreadID.y)
	{
		const float halfMag = 0.5f * gDisturbMagnitude;

		gCurrSolInput[int2(x, y)] += gDisturbMagnitude;
		gCurrSolInput[int2(x, y + 1)] += halfMag;
		gCurrSolInput[int2(x, y - 1)] += halfMag;
		gCurrSolInput[int2(x + 1, y)] += halfMag;
		gCurrSolInput[int2(x - 1, y)] += halfMag;
	}
}

