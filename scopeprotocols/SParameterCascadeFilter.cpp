/***********************************************************************************************************************
*                                                                                                                      *
* libscopeprotocols                                                                                                    *
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

/**
	@file
	@author Andrew D. Zonenberg
	@brief Implementation of SParameterCascadeFilter
 */
#include "scopeprotocols.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SParameterCascadeFilter::SParameterCascadeFilter(const string& color)
	: SParameterFilter(color, CAT_RF)
{
	//Set up output ports
	m_parameters[m_portCountName].MarkHidden();
	m_parameters[m_portCountName].SetIntVal(2);
	SetupStreams();

	//Create our input ports
	m_signalNames.clear();
	m_inputs.clear();
	for(size_t to = 0; to < 2; to++)
	{
		for(size_t from = 0; from < 2; from ++)
		{
			auto pname = string("S") + to_string(to+1) + to_string(from+1);
			CreateInput(pname + "A_mag");
			CreateInput(pname + "A_ang");
		}
	}

	for(size_t to = 0; to < 2; to++)
	{
		for(size_t from = 0; from < 2; from ++)
		{
			auto pname = string("S") + to_string(to+1) + to_string(from+1);
			CreateInput(pname + "B_mag");
			CreateInput(pname + "B_ang");
		}
	}
}

SParameterCascadeFilter::~SParameterCascadeFilter()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string SParameterCascadeFilter::GetProtocolName()
{
	return "S-Parameter Cascade";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main filter processing

void SParameterCascadeFilter::RefreshPorts()
{
	//do nothing
}

bool SParameterCascadeFilter::ValidateChannel(size_t i, StreamDescriptor stream)
{
	//All inputs are required
	if(stream.m_channel == NULL)
		return false;

	//Validate port count
	if(i >= 16 )
		return false;

	//X axis must be Hz
	if(stream.GetXAxisUnits() != Unit(Unit::UNIT_HZ))
		return false;

	//Angle: Y axis unit must be degrees
	if(i & 1)
	{
		if(stream.GetYAxisUnits() != Unit(Unit::UNIT_DEGREES))
			return false;
	}

	//Magnitude: Y axis unit must be dB
	else
	{
		if(stream.GetYAxisUnits() != Unit(Unit::UNIT_DB))
			return false;
	}

	return true;
}


void SParameterCascadeFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	//Use S11a magnitude as timebase reference for our output
	auto base = GetAnalogInputWaveform(0);

	//Get all of our inputs
	SParameterVector s11a(GetAnalogInputWaveform(0), GetAnalogInputWaveform(1));
	SParameterVector s12a(GetAnalogInputWaveform(2), GetAnalogInputWaveform(3));
	SParameterVector s21a(GetAnalogInputWaveform(4), GetAnalogInputWaveform(5));
	SParameterVector s22a(GetAnalogInputWaveform(6), GetAnalogInputWaveform(7));

	SParameterVector s11b(GetAnalogInputWaveform(8), GetAnalogInputWaveform(9));
	SParameterVector s12b(GetAnalogInputWaveform(10), GetAnalogInputWaveform(11));
	SParameterVector s21b(GetAnalogInputWaveform(12), GetAnalogInputWaveform(13));
	SParameterVector s22b(GetAnalogInputWaveform(14), GetAnalogInputWaveform(15));

	//Vectors for output
	size_t npoints = s11a.size();
	SParameterVector s11o;
	SParameterVector s12o;
	SParameterVector s21o;
	SParameterVector s22o;
	s11o.resize(npoints);
	s12o.resize(npoints);
	s21o.resize(npoints);
	s22o.resize(npoints);

	//Concatenate the S-parameters
	//(equation 2.18, page 118 of Dunsmore 2nd edition)
	for(size_t i=0; i<npoints;i++)
	{
		float freq = s11a.m_points[i].m_frequency;

		//Interpolate all inputs to our current frequency bin
		//and convert from our default mag/angle representation to real/imaginary
		auto p11a = s11a.InterpolatePoint(freq).ToComplex();
		auto p12a = s12a.InterpolatePoint(freq).ToComplex();
		auto p21a = s21a.InterpolatePoint(freq).ToComplex();
		auto p22a = s22a.InterpolatePoint(freq).ToComplex();

		auto p11b = s11b.InterpolatePoint(freq).ToComplex();
		auto p12b = s12b.InterpolatePoint(freq).ToComplex();
		auto p21b = s21b.InterpolatePoint(freq).ToComplex();
		auto p22b = s22b.InterpolatePoint(freq).ToComplex();

		//Do the actual math
		auto one = complex<float>(1, 0);
		auto p11 = p11a + (p11b * p21a * p12a) / (one - p22a*p11b);
		auto p12 = (p12a*p12b) / (one - p22a*p11b);
		auto p21 = (p21a*p21b) / (one - p22a*p11b);
		auto p22 = p22b + (p22a * p21b * p12b) / (one - p22a*p11b);

		//Convert back to mag/angle
		s11o.m_points[i] = SParameterPoint(freq, p11);
		s12o.m_points[i] = SParameterPoint(freq, p12);
		s21o.m_points[i] = SParameterPoint(freq, p21);
		s22o.m_points[i] = SParameterPoint(freq, p22);
	}

	//Waveforms for output
	auto s11o_mag = SetupEmptyOutputWaveform(base, 0);
	auto s11o_ang = SetupEmptyOutputWaveform(base, 1);
	auto s12o_mag = SetupEmptyOutputWaveform(base, 2);
	auto s12o_ang = SetupEmptyOutputWaveform(base, 3);
	auto s21o_mag = SetupEmptyOutputWaveform(base, 4);
	auto s21o_ang = SetupEmptyOutputWaveform(base, 5);
	auto s22o_mag = SetupEmptyOutputWaveform(base, 6);
	auto s22o_ang = SetupEmptyOutputWaveform(base, 7);

	//Copy the output
	s11o.ConvertToWaveforms(s11o_mag, s11o_ang);
	s12o.ConvertToWaveforms(s12o_mag, s12o_ang);
	s21o.ConvertToWaveforms(s21o_mag, s21o_ang);
	s22o.ConvertToWaveforms(s22o_mag, s22o_ang);
}
