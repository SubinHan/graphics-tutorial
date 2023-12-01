#include "Complex.hlsl"

static const float PI = 3.1415926536;
static const float EPSILON = 0.0001;
static const float G = 9.81f;

float Dispersion(int n, int m, int res, float len)
{
	float kx = PI * (2 * n - res) / len;
	float kz = PI * (2 * m - res) / len;
	float k2 = kx * kx + kz * kz;
	float k = sqrt(k2);
	float unit = 5e-3f;
	float unit2 = unit * unit;
	//float w2 = G * k * (1 + k2 * unit2);
	float w2 = G * k;
	float w = sqrt(w2);
	float w0 = 2.0f * PI / len;
	return floor(w / w0) * w0;
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
	float damping = 0.001f;
	float damping2 = damping * damping;

	return amp * exp(-1.0f / (kLen2 * l2)) / kLen4 * kDotW2 * exp(-kLen2 * damping2);
}

float mod(float x, float y)
{
	return x - y * floor(x / y);
}

float Rand(float2 uv, float randSeed)
{
	float result = sin(mod(12345678.f, dot(uv, float2(12.9898, 78.233) * 2.0))) * (43758.5453 + randSeed);
	result = frac(result);
	return result;
}

float2 RandNegative1ToPositive1(float2 uv, float randSeed)
{
	float2 r;
	float2 isNegative = Rand(float2(uv.x, uv.y), randSeed);
	r.x = Rand(float2(uv.x + 31.2452, uv.y + 27.6354), randSeed);
	r.y = Rand(float2(uv.x + 11.67834, uv.y + 51.3214), randSeed);

	if (isNegative.x > 0.5f)
		r.x = -r.x;
	if (isNegative.y > 0.5f)
		r.y = -r.y;

	return float2(r.x, r.y);
}
float2 HTilde0(int n, int m, float amp, float2 wind, int res, float len, float randSeed)
{
	return ComplexMul(RandNegative1ToPositive1(float2(n, m), randSeed), sqrt(Phillips(n, m, amp, wind, res, len) * 0.5f));
}
