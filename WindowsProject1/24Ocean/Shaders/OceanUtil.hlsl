#include "Complex.hlsl"

static const float PI = 3.1415926536;
static const float EPSILON = 0.0001;
static const float G = 9.81f;

float Dispersion(int n, int m, int res, float len)
{
	float w = 2.0f * PI / len;
	float kx = PI * (2 * n - res) / len;
	float kz = PI * (2 * m - res) / len;
	return floor(sqrt(G * sqrt(kx * kx + kz * kz)) / w) * w;
}

// 0 <= n, m < res
float Phillips(int n, int m, float amp, float2 wind, int res, float len)
{
	float2 k =
	{
		2.0f * PI * (n - 0.5f * res) / len,
		2.0f * PI * (m - 0.5f * res) / len
	};
	// k = 2pi * n0 / L, (-res/2 <= n0 <= res/2)

	float kLen = length(k);
	if (kLen < EPSILON)
		return 0;

	float kLen2 = kLen * kLen;
	float kLen4 = kLen2 * kLen2;

	float kDotW = dot(normalize(k), normalize(wind));
	float kDotW2 = kDotW * kDotW;

	float wlen = length(wind);
	float l = wlen * wlen / G;
	float l2 = l * l;
	float damping = 0.01;
	float L2 = l2 * damping * damping;

	return amp * exp(-1 / (kLen2 * l2)) / kLen4 * kDotW2 * exp(-kLen2 * L2);
}

float Rand(float2 uv)
{
	float result = dot(float3(uv, 5.20948), float3(12.9898, 78.233, 37.719));
	result = frac(result);
	return result;
}

float2 HTilde0(int n, int m, float amp, float2 wind, int res, float len)
{
	float2 r;
	r.x = Rand(float2(n, m));
	r.y = Rand(float2(n, m));

	return ComplexMul(r, sqrt(Phillips(n, m, amp, wind, res, len) / 2.0f));
}
