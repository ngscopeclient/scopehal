/*
	Push constants
		uint	samples_per_tap
		uint	size
		float	tap0
		float	tap1

	Inputs
		0		float	waveform in
 */

float __kernel__(uint x)
{
	//bounds check
	if(i >= size)
		return 0;

	float in1 = __input_0__(x);
	float in0 = __input_0__(x + samples_per_tap);

	return (in1 * __push__tap1) + (in0 * __push__tap0);
}
