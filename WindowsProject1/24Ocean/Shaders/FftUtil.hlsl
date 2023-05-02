#pragma once

static const float PI = 3.1415926536;
static const float EPSILON = 0.0001;
static const float G = 9.81f;

float Phillips(float n, float m, float amp, float2 wind, float res, float len)
{
	float2 k =
	{
		2.0f * PI * (n - 0.5f * res) / len,
		2.0f * PI * (m - 0.5f * res) / len
	};

	float kLen = length(k);
	float kLen2 = kLen * kLen;
	float kLen4 = kLen2 * kLen2;
	if (kLen < EPSILON)
		return 0;
	float kDotW = dot(normalize(k), normalize(wind));
	float kDotW2 = kDotW * kDotW;
	float wlen = length(wind);
	float l = wlen * wlen / G;
	float l2 = l * l;
	float damping = 0.01;
	float L2 = l2 * damping * damping;

	return amp * exp(-1 / (kLen2 * l2)) / kLen4 * kDotW2 * exp(-kLen2 * L2);
}

float2 rand_2_10(in float2 uv) {
	float noiseX = (frac(sin(dot(uv, float2(12.9898, 78.233) * 2.0)) * 43758.5453));
	float noiseY = sqrt(1 - noiseX * noiseX);
	return float2(noiseX, noiseY);
}