/**
	Push constants
		uint	imax
		uint	upsample_factor
		uint	kernel

	Inputs
		0		float	waveform in
		1		float	kernel
 */
float __kernel__(uint x, uint y)
{
	//bounds check
	if(x >= imax)
		return 0;
	if(y >= upsample_factor)
		return;

	/*
	uint offset = i*upsample_factor;

	uint start = 0;
	uint sstart = 0;
	if(gl_GlobalInvocationID.x > 0)
	{
		sstart = 1;
		start = upsample_factor - gl_GlobalInvocationID.x;
	}

	float f = 0;
	for(uint k = start; k<kernel; k += upsample_factor, sstart ++)
		f += fkernel[k] * din[i + sstart];

	dout[offset + gl_GlobalInvocationID.x] = f;
	*/
}
