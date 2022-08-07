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
	@brief Implementation of SParameterDeEmbedFilter
 */
#include "scopeprotocols.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SParameterDeEmbedFilter::SParameterDeEmbedFilter(const string& color)
	: SParameterFilter(color, CAT_RF)
	, m_knownSide("Known Side")
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
			CreateInput(pname + "Combined_mag");
			CreateInput(pname + "Combined_ang");
		}
	}

	for(size_t to = 0; to < 2; to++)
	{
		for(size_t from = 0; from < 2; from ++)
		{
			auto pname = string("S") + to_string(to+1) + to_string(from+1);
			CreateInput(pname + "Known_mag");
			CreateInput(pname + "Known_ang");
		}
	}

	m_parameters[m_knownSide] = FilterParameter(FilterParameter::TYPE_ENUM, Unit(Unit::UNIT_COUNTS));
	m_parameters[m_knownSide].AddEnumValue("Left (Port 1)", SIDE_LEFT);
	m_parameters[m_knownSide].AddEnumValue("Right (Port 2)", SIDE_RIGHT);
	m_parameters[m_knownSide].SetIntVal(SIDE_LEFT);
}

SParameterDeEmbedFilter::~SParameterDeEmbedFilter()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string SParameterDeEmbedFilter::GetProtocolName()
{
	return "S-Parameter De-Embed";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main filter processing

void SParameterDeEmbedFilter::RefreshPorts()
{
	//do nothing
}

bool SParameterDeEmbedFilter::ValidateChannel(size_t i, StreamDescriptor stream)
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


void SParameterDeEmbedFilter::Refresh()
{
	//Make sure we've got valid inputs
	if(!VerifyAllInputsOKAndAnalog())
	{
		SetData(NULL, 0);
		return;
	}

	//Use S11a magnitude as timebase reference for our output
	auto base = GetAnalogInputWaveform(0);

	//Combined network
	SParameterVector s11c(GetAnalogInputWaveform(0), GetAnalogInputWaveform(1));
	SParameterVector s12c(GetAnalogInputWaveform(2), GetAnalogInputWaveform(3));
	SParameterVector s21c(GetAnalogInputWaveform(4), GetAnalogInputWaveform(5));
	SParameterVector s22c(GetAnalogInputWaveform(6), GetAnalogInputWaveform(7));

	//Known network
	SParameterVector s11k(GetAnalogInputWaveform(8), GetAnalogInputWaveform(9));
	SParameterVector s12k(GetAnalogInputWaveform(10), GetAnalogInputWaveform(11));
	SParameterVector s21k(GetAnalogInputWaveform(12), GetAnalogInputWaveform(13));
	SParameterVector s22k(GetAnalogInputWaveform(14), GetAnalogInputWaveform(15));

	//Vectors for output
	size_t npoints = s11c.size();
	SParameterVector s11o;
	SParameterVector s12o;
	SParameterVector s21o;
	SParameterVector s22o;
	s11o.resize(npoints);
	s12o.resize(npoints);
	s21o.resize(npoints);
	s22o.resize(npoints);

	//Figure out which network is known
	bool knownIsA = (m_parameters[m_knownSide].GetIntVal() == SIDE_LEFT);

	//Do the actual de-embed
	for(size_t i=0; i<npoints;i++)
	{
		float freq = s11c.m_points[i].m_frequency;

		//Interpolate all inputs to our current frequency bin
		//and convert from our default mag/angle representation to real/imaginary
		auto p11c = s11c.InterpolatePoint(freq).ToComplex();
		auto p12c = s12c.InterpolatePoint(freq).ToComplex();
		auto p21c = s21c.InterpolatePoint(freq).ToComplex();
		auto p22c = s22c.InterpolatePoint(freq).ToComplex();

		auto p11k = s11k.InterpolatePoint(freq).ToComplex();
		auto p12k = s12k.InterpolatePoint(freq).ToComplex();
		auto p21k = s21k.InterpolatePoint(freq).ToComplex();
		auto p22k = s22k.InterpolatePoint(freq).ToComplex();

		complex<float> p11;
		complex<float> p12;
		complex<float> p21;
		complex<float> p22;
		complex<float> one = complex<float>(1, 0);

		//Solve for B network given A
		if(knownIsA)
		{
			p11 = (p11c - p11k) / (p21k*p12k + p22k*p11c - p22k*p11k);
			p12 = (p12c - p12c*p11*p22k) / p12k;
			p21 = (p21c - p21c*p22k*p11) / p21k;
			p22 = p22c - (p22k * p21 * p12) / (one - p22k*p11);
		}

		//Solve for A network given B
		else
		{
			p22 = (p22c - p22k) / (p21k*p12k + p22c*p11k - p22k*p11k);
			p12 = (p12c - p12c*p11k*p22) / p12k;
			p21 = (p21c - p21c*p22*p11k) / p21k;
			p11 = p11c - (p11k * p21 * p12) / (one - p22*p11k);
		}

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
