float2 ComplexMul(float2 c1, float2 c2)
{
	float2 result = float2(
		c1.x * c2.x - c1.y * c2.y,
		c1.x * c2.y + c2.x * c1.y
		);

	return result;
}

float2 ComplexMul(float2 c, float scalar)
{
	float2 result;
	result.x = c.x * scalar;
	result.y = c.y * scalar;

	return result;
}

float2 ComplexAdd(float2 c1, float2 c2)
{
	float2 result;
	result.x = c1.x + c2.x;
	result.y = c1.y + c2.y;

	return result;
}

float2 ComplexConj(float2 c)
{
	float2 result;
	result.x = c.x;
	result.y = -c.y;

	return result;
}