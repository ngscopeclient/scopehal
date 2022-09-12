/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2022 Andrew D. Zonenberg and contributors                                                         *
* All rights reserved.                                                                                                 *
*                                                                                                                      *
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that the     *
* following conditions are met:                                                                                        *
*                                                                                                                      *
*    * Redistributions of source code must retain the above copyright notice, this list of conditions, and the         *
*      following disclaimer.                                                                                           *
*                                                                                                                      *
*    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the       *
*      following disclaimer in the documentation and/or other materials provided with the distribution.                *
*                                                                                                                      *
*    * Neither the name of the author nor the names of any contributors may be used to endorse or promote products     *
*      derived from this software without specific prior written permission.                                           *
*                                                                                                                      *
* THIS SOFTWARE IS PROVIDED BY THE AUTHORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED   *
* TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL *
* THE AUTHORS BE HELD LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES        *
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR       *
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE       *
* POSSIBILITY OF SUCH DAMAGE.                                                                                          *
*                                                                                                                      *
***********************************************************************************************************************/

#include "scopehal.h"
#include "FunctionGenerator.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

FunctionGenerator::FunctionGenerator()
{
}

FunctionGenerator::~FunctionGenerator()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// String helpers for enums

string FunctionGenerator::GetNameOfShape(WaveShape shape)
{
	switch(shape)
	{
		case FunctionGenerator::SHAPE_SINE:
			return "Sine";

		case FunctionGenerator::SHAPE_SQUARE:
			return "Square";

		case FunctionGenerator::SHAPE_TRIANGLE:
			return "Triangle";

		case FunctionGenerator::SHAPE_PULSE:
			return "Pulse";

		case FunctionGenerator::SHAPE_DC:
			return "DC";

		case FunctionGenerator::SHAPE_NOISE:
			return "Noise";

		case FunctionGenerator::SHAPE_SAWTOOTH_UP:
			return "Sawtooth up";

		case FunctionGenerator::SHAPE_SAWTOOTH_DOWN:
			return "Sawtooth down";

		case FunctionGenerator::SHAPE_SINC:
			return "Sinc";

		case FunctionGenerator::SHAPE_GAUSSIAN:
			return "Gaussian";

		case FunctionGenerator::SHAPE_LORENTZ:
			return "Lorentz";

		case FunctionGenerator::SHAPE_HALF_SINE:
			return "Half sine";

		case FunctionGenerator::SHAPE_PRBS_NONSTANDARD:
			return "PRBS (nonstandard polynomial)";

		case FunctionGenerator::SHAPE_EXPONENTIAL_RISE:
			return "Exponential Rise";

		case FunctionGenerator::SHAPE_EXPONENTIAL_DECAY:
			return "Exponential Decay";

		case FunctionGenerator::SHAPE_HAVERSINE:
			return "Haversine";

		case FunctionGenerator::SHAPE_CARDIAC:
			return "Cardiac";

		case FunctionGenerator::SHAPE_STAIRCASE_UP:
			return "Staircase up";

		case FunctionGenerator::SHAPE_STAIRCASE_DOWN:
			return "Staircase down";

		case FunctionGenerator::SHAPE_STAIRCASE_UP_DOWN:
			return "Staircase triangular";

		case FunctionGenerator::SHAPE_NEGATIVE_PULSE:
			return "Negative pulse";

		case FunctionGenerator::SHAPE_LOG_RISE:
			return "Logarithmic rise";

		case FunctionGenerator::SHAPE_LOG_DECAY:
			return "Logarithmic decay";

		case FunctionGenerator::SHAPE_SQUARE_ROOT:
			return "Square root";

		case FunctionGenerator::SHAPE_CUBE_ROOT:
			return "Cube root";

		case FunctionGenerator::SHAPE_QUADRATIC:
			return "Quadratic";

		case FunctionGenerator::SHAPE_CUBIC:
			return "Cubic";

		case FunctionGenerator::SHAPE_DLORENTZ:
			return "DLorentz";

		case FunctionGenerator::SHAPE_GAUSSIAN_PULSE:
			return "Gaussian pulse";

		case FunctionGenerator::SHAPE_HAMMING:
			return "Hamming";

		case FunctionGenerator::SHAPE_HANNING:
			return "Hanning";

		case FunctionGenerator::SHAPE_KAISER:
			return "Kaiser";

		case FunctionGenerator::SHAPE_BLACKMAN:
			return "Blackman";

		case FunctionGenerator::SHAPE_GAUSSIAN_WINDOW:
			return "Gaussian window";

		case FunctionGenerator::SHAPE_HARRIS:
			return "Harris";

		case FunctionGenerator::SHAPE_BARTLETT:
			return "Bartlett";

		case FunctionGenerator::SHAPE_TAN:
			return "Tan";

		case FunctionGenerator::SHAPE_COT:
			return "Cot";

		case FunctionGenerator::SHAPE_SEC:
			return "Sec";

		case FunctionGenerator::SHAPE_CSC:
			return "Csc";

		case FunctionGenerator::SHAPE_ASIN:
			return "Asin";

		case FunctionGenerator::SHAPE_ACOS:
			return "Acos";

		case FunctionGenerator::SHAPE_ATAN:
			return "Atan";

		case FunctionGenerator::SHAPE_ACOT:
			return "Acot";

		//Arbitrary is not supported yet so don't show it in the list
		//case FunctionGenerator::SHAPE_ARBITRARY:
		//	continue;

		default:
			return "Unknown";
	}
}
