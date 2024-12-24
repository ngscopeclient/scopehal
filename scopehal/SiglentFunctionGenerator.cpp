/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal                                                                                                          *
*                                                                                                                      *
* Copyright (c) 2012-2024 Andrew D. Zonenberg and contributors                                                         *
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
	@brief Implementation of SiglentFunctionGenerator
	@ingroup funcdrivers
 */

#include "scopehal.h"
#include "SiglentFunctionGenerator.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

SiglentFunctionGenerator::SiglentFunctionGenerator(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
{
	//All SDG series have two channels
	m_channels.push_back(new FunctionGeneratorChannel(this, "C1", "#008000", 0));
	m_channels.push_back(new FunctionGeneratorChannel(this, "C2", "#ffff00", 1));

	FlushConfigCache();

	//Echoing causes problems for us, but most models don't allow us to turn it off!
	//Turn it on for everything so behavior is at least consistent.
	m_transport->SendCommandQueued("CHDR ON");
}

SiglentFunctionGenerator::~SiglentFunctionGenerator()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Instrument

unsigned int SiglentFunctionGenerator::GetInstrumentTypes() const
{
	return INST_FUNCTION;
}

uint32_t SiglentFunctionGenerator::GetInstrumentTypesForChannel(size_t i) const
{
	if(i < 2)
		return INST_FUNCTION;
	else
		return 0;
}

bool SiglentFunctionGenerator::AcquireData()
{
	return true;
}

string SiglentFunctionGenerator::GetDriverNameInternal()
{
	return "siglent_sdg";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FunctionGenerator

void SiglentFunctionGenerator::FlushConfigCache()
{
	FunctionGenerator::FlushConfigCache();

	for(size_t i=0; i<m_channels.size(); i++)
	{
		m_cachedEnableStateValid[i] = 0;
		m_cachedOutputEnable[i] = 0;

		m_cachedFrequency[i] = 0;
		m_cachedFrequencyValid[i] = false;

		m_cachedAmplitude[i] = 0;
		m_cachedAmplitudeValid[i] = false;

		m_cachedOffset[i] = 0;
		m_cachedOffsetValid[i] = false;
	}
}

string SiglentFunctionGenerator::RemoveHeader(const string& str)
{
	auto pos = str.find(' ');
	return Trim(str.substr(pos + 1));
}

/**
	@brief Parse the response to an OUTP? query
 */
void SiglentFunctionGenerator::ParseOutputState(const string& str, size_t i)
{
	auto fields = explode(str, ',');

	//Output enable
	if(fields[0] == "ON")
		m_cachedOutputEnable[i] = true;
	else
		m_cachedOutputEnable[i] = false;
	m_cachedEnableStateValid[i] = true;

	//TODO: output invert

	//TODO: output impedance
}

/**
	@brief Parse the response to a BSWV? query
 */
void SiglentFunctionGenerator::ParseBasicWaveform(const string& str, size_t i)
{
	auto fields = explode(str, ',');
	LogDebug("ParseBasicWaveform\n");
	LogIndenter li;

	//Fields are paired as name,value consecutively
	map<string, string> fieldmap;
	for(size_t j=0; j<fields.size(); j += 2)
	{
		if(j+1 >= fields.size())
			break;
		fieldmap[fields[j]] = fields[j+1];
	}

	//Default all waveform cache stuff to invalid
	m_cachedAmplitudeValid[i] = false;
	m_cachedOffsetValid[i] = false;
	m_cachedFrequencyValid[i] = false;

	Unit volts(Unit::UNIT_VOLTS);
	Unit hz(Unit::UNIT_HZ);
	for(auto it : fieldmap)
	{
		if(it.first == "AMP")
		{
			m_cachedAmplitude[i] = volts.ParseString(it.second);
			m_cachedAmplitudeValid[i] = true;
		}

		if(it.first == "OFST")
		{
			m_cachedOffset[i] = volts.ParseString(it.second);
			m_cachedOffsetValid[i] = true;
		}

		if(it.first == "FRQ")
		{
			m_cachedFrequency[i] = hz.ParseString(it.second);
			m_cachedFrequencyValid[i] = true;
		}

		//LogDebug("%10s -> %10s\n", it.first.c_str(), it.second.c_str());
	}
}

vector<FunctionGenerator::WaveShape> SiglentFunctionGenerator::GetAvailableWaveformShapes(int /*chan*/)
{
	vector<WaveShape> ret;
	ret.push_back(SHAPE_SINE);
	ret.push_back(SHAPE_SQUARE);
	ret.push_back(SHAPE_SAWTOOTH_UP);
	ret.push_back(SHAPE_PULSE);
	ret.push_back(SHAPE_NOISE);
	ret.push_back(SHAPE_DC);
	ret.push_back(SHAPE_HALF_SINE);
	ret.push_back(SHAPE_GAUSSIAN_PULSE);
	ret.push_back(SHAPE_SAWTOOTH_DOWN);
	ret.push_back(SHAPE_NEGATIVE_PULSE);
	ret.push_back(SHAPE_STAIRCASE_DOWN);
	ret.push_back(SHAPE_STAIRCASE_UP_DOWN);
	ret.push_back(SHAPE_STAIRCASE_UP);
	ret.push_back(SHAPE_CARDIAC);
	ret.push_back(SHAPE_CUBIC);
	ret.push_back(SHAPE_EXPONENTIAL_DECAY);
	ret.push_back(SHAPE_EXPONENTIAL_RISE);
	ret.push_back(SHAPE_GAUSSIAN);
	ret.push_back(SHAPE_HAVERSINE);
	ret.push_back(SHAPE_LOG_RISE);
	ret.push_back(SHAPE_COT);
	ret.push_back(SHAPE_SINC);
	ret.push_back(SHAPE_SQUARE_ROOT);
	ret.push_back(SHAPE_TAN);
	ret.push_back(SHAPE_ACOS);
	ret.push_back(SHAPE_ASIN);
	ret.push_back(SHAPE_ATAN);
	ret.push_back(SHAPE_BARTLETT);
	ret.push_back(SHAPE_HAMMING);
	ret.push_back(SHAPE_HANNING);
	ret.push_back(SHAPE_TRIANGLE);
	return ret;
}

bool SiglentFunctionGenerator::GetFunctionChannelActive(int chan)
{
	if(m_cachedEnableStateValid[chan])
		return m_cachedOutputEnable[chan];

	auto reply = RemoveHeader(m_transport->SendCommandQueuedWithReply(m_channels[chan]->GetHwname() + ":OUTP?"));
	ParseOutputState(reply, chan);

	return m_cachedOutputEnable[chan];
}

void SiglentFunctionGenerator::SetFunctionChannelActive(int chan, bool on)
{
	if(on)
		m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":OUTP ON");
	else
		m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":OUTP OFF");

	m_cachedOutputEnable[chan] = on;
	m_cachedEnableStateValid[chan] = true;
}

float SiglentFunctionGenerator::GetFunctionChannelAmplitude(int chan)
{
	if(m_cachedAmplitudeValid[chan])
		return m_cachedAmplitude[chan];

	auto reply = RemoveHeader(m_transport->SendCommandQueuedWithReply(m_channels[chan]->GetHwname() + ":BSWV?"));
	ParseBasicWaveform(reply, chan);

	return m_cachedAmplitude[chan];
}

void SiglentFunctionGenerator::SetFunctionChannelAmplitude(int chan, float amplitude)
{
	m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":BSWV AMP," + to_string(amplitude));

	m_cachedAmplitude[chan] = amplitude;
	m_cachedAmplitudeValid[chan] = true;
}

float SiglentFunctionGenerator::GetFunctionChannelOffset(int chan)
{
	if(m_cachedOffsetValid[chan])
		return m_cachedOffset[chan];

	auto reply = RemoveHeader(m_transport->SendCommandQueuedWithReply(m_channels[chan]->GetHwname() + ":BSWV?"));
	ParseBasicWaveform(reply, chan);

	return m_cachedOffset[chan];
}

void SiglentFunctionGenerator::SetFunctionChannelOffset(int chan, float offset)
{
	m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":BSWV OFST," + to_string(offset));

	m_cachedOffset[chan] = offset;
	m_cachedOffsetValid[chan] = true;
}

float SiglentFunctionGenerator::GetFunctionChannelFrequency(int chan)
{
	if(m_cachedFrequencyValid[chan])
		return m_cachedFrequency[chan];

	auto reply = RemoveHeader(m_transport->SendCommandQueuedWithReply(m_channels[chan]->GetHwname() + ":BSWV?"));
	ParseBasicWaveform(reply, chan);

	return m_cachedFrequency[chan];
}

void SiglentFunctionGenerator::SetFunctionChannelFrequency(int chan, float hz)
{
	m_transport->SendCommandQueued(m_channels[chan]->GetHwname() + ":BSWV FRQ," + to_string(hz));

	m_cachedFrequency[chan] = hz;
	m_cachedFrequencyValid[chan] = true;
}

FunctionGenerator::WaveShape SiglentFunctionGenerator::GetFunctionChannelShape(int chan)
{
	/*
	auto reply = Trim(m_transport->SendCommandQueuedWithReply(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP?"));
	if(reply == "SIN")
		return SHAPE_SINE;
	else if(reply == "SQU")
		return SHAPE_SQUARE;
	else if(reply == "RAMP")
		return SHAPE_SAWTOOTH_UP;
	else if(reply == "PULS")
		return SHAPE_PULSE;
	else if(reply == "NOIS")
		return SHAPE_NOISE;
	//USER
	//HARMonic
	//CUSTom
	else if(reply == "DC")
		return SHAPE_DC;
	else if(reply == "ABSSINE")
		return SHAPE_HALF_SINE;
	//ABSSINEHALF
	//AMPALT
	//ATTALT
	else if(reply == "GAUSSPULSE")
		return SHAPE_GAUSSIAN_PULSE;
	else if(reply == "NEGRAMP")
		return SHAPE_SAWTOOTH_DOWN;
	else if(reply == "NPULSE")
		return SHAPE_NEGATIVE_PULSE;
	//PPULSE
	//SINETRA
	//SINEVER
	else if(reply == "STAIRDN")
		return SHAPE_STAIRCASE_DOWN;
	else if(reply == "STAIRUD")
		return SHAPE_STAIRCASE_UP_DOWN;
	else if(reply == "STAIRUP")
		return SHAPE_STAIRCASE_UP;
	//TRAPEZIA
	//BANDLIMITED
	//BUTTERWORTH
	//CHEBYSHEV1
	//CHEBYSHEV2
	//COMBIN
	//CPULSE
	//CWPULSE
	//DAMPEDOSC
	//DUALTONE
	//GAMA
	//GATEVIBR
	//LFMPULSE
	//MCNOSIE
	//NIMHDISCHARGE
	//PAHCUR
	//QUAKE
	//RADAR
	//RIPPLE
	//ROUDHALF
	//ROUNDPM
	//STEPRESP
	//SWINGOSC
	//TV
	//VOICE
	//THREEAM
	//THREEFM
	//THREEPM
	//THREEPWM
	//THREEPFM
	else if(reply == "CARDIAC")
		return SHAPE_CARDIAC;
	//EOG
	//EEG
	//EMG
	//PULSILOGRAM
	//RESSPEED
	//LFPULSE
	//TENS1
	//TENS2
	//TENS3
	//IGNITION
	//ISO167502SP|ISO167502VR|ISO76372TP1|ISO76372TP2A|ISO76372TP2B|ISO76372TP3A|ISO76372TP3B|ISO76372TP4|ISO76372TP5A|ISO76372TP5B|
	//SCR
	//SURGE
	//AIRY
	//BESSELJ
	//BESSELY
	//CAUCHY
	else if(reply == "CUBIC")
		return SHAPE_CUBIC;
	//DIRICHLET|ERF|ERFC|ERFCINV|ERFINV|
	else if(reply == "EXPFALL")
		return SHAPE_EXPONENTIAL_DECAY;
	else if(reply == "EXPRISE")
		return SHAPE_EXPONENTIAL_RISE;
	else if(reply == "GAUSS")
		return SHAPE_GAUSSIAN;
	else if(reply == "HAVERSINE")
		return SHAPE_HAVERSINE;
	//LAGUERRE|LAPLACE|LEGEND|
	else if(reply == "LOG")
		return SHAPE_LOG_RISE;
	//LOGNORMAL
	//LORENTZ|MAXWELL|RAYLEIGH|VERSIERA|WEIBULL|X2DATA|COSH|COSINT
	else if(reply == "COT")
		return SHAPE_COT;
	//COTHCON|COTHPRO|CSCCON|CSCPRO|CSCHCON|CSCHPRO|RECIPCON|RECIPPRO|SECCON|SECPRO|SECH|
	else if(reply == "SINC")
		return SHAPE_SINC;
	//SINH|SININT
	else if(reply == "SQRT")
		return SHAPE_SQUARE_ROOT;
	else if(reply == "TAN")
		return SHAPE_TAN;
	//TANH
	else if(reply == "ACOS")
		return SHAPE_ACOS;
	//COSH|ACOTCON|ACOTPRO|ACOTHCON|ACOTHPRO|ACSCCON|ACSCPRO|ACSCHCON|ACSCHPRO|ASECCON|ASECPRO|ASECH|
	else if(reply == "ASIN")
		return SHAPE_ASIN;
	//ASINH
	else if(reply == "ATAN")
		return SHAPE_ATAN;
	//ATANH
	else if(reply == "BARTLETT")
		return SHAPE_BARTLETT;
	//BARTHANN|BLACKMAN|BLACKMANH|BOHMANWIN|BOXCAR|CHEBWIN|FLATTOPWIN|
	else if(reply == "HAMMING")
		return SHAPE_HAMMING;
	else if(reply == "HANNING")
		return SHAPE_HANNING;
	//KAISER|NUTTALLWIN|PARZENWIN|TAYLORWIN|TUKEYWIN
	else if(reply == "TRIANG")
		return SHAPE_TRIANGLE;
	*/
	return SHAPE_SINE;
}

void SiglentFunctionGenerator::SetFunctionChannelShape(int chan, WaveShape shape)
{
	/*
	switch(shape)
	{
		case SHAPE_SINE:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP SIN");
			break;

		case SHAPE_SQUARE:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP SQU");
			break;

		case SHAPE_SAWTOOTH_UP:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP RAMP");
			break;

		case SHAPE_PULSE:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP PULS");
			break;

		case SHAPE_NOISE:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP NOIS");
			break;

		case SHAPE_DC:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP DC");
			break;

		case SHAPE_HALF_SINE:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP ABSSINE");
			break;

		case SHAPE_GAUSSIAN_PULSE:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP GAUSSPULSE");
			break;

		case SHAPE_SAWTOOTH_DOWN:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP NEGRAMP");
			break;

		case SHAPE_NEGATIVE_PULSE:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP NPULSE");
			break;

		case SHAPE_STAIRCASE_DOWN:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP STAIRDN");
			break;

		case SHAPE_STAIRCASE_UP_DOWN:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP STAIRUD");
			break;

		case SHAPE_STAIRCASE_UP:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP STAIRUP");
			break;

		case SHAPE_CARDIAC:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP CARDIAC");
			break;

		case SHAPE_CUBIC:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP CUBIC");
			break;

		case SHAPE_EXPONENTIAL_DECAY:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP EXPFALL");
			break;

		case SHAPE_EXPONENTIAL_RISE:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP EXPRISE");
			break;

		case SHAPE_GAUSSIAN:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP GAUSS");
			break;

		case SHAPE_HAVERSINE:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP HAVERSINE");
			break;

		case SHAPE_LOG_RISE:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP LOG");
			break;

		case SHAPE_COT:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP COT");
			break;

		case SHAPE_SINC:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP SINC");
			break;

		case SHAPE_SQUARE_ROOT:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP SQRT");
			break;

		case SHAPE_TAN:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP TAN");
			break;

		case SHAPE_ACOS:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP ACOS");
			break;

		case SHAPE_ASIN:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP ASIN");
			break;

		case SHAPE_ATAN:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP ATAN");
			break;

		case SHAPE_BARTLETT:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP BARTLETT");
			break;

		case SHAPE_HAMMING:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP HAMMING");
			break;

		case SHAPE_HANNING:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP HANNING");
			break;

		case SHAPE_TRIANGLE:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP TRIANG");
			break;

		default:
			LogWarning("[SiglentFunctionGenerator::SetFunctionChannelShape] unrecognized shape %d", shape);
			break;
	}
	*/
}

float SiglentFunctionGenerator::GetFunctionChannelDutyCycle(int chan)
{
	/*
	auto reply = Trim(m_transport->SendCommandQueuedWithReply(string("SOUR") + to_string(chan+1) + ":FUNC:SQU:DCYC?"));
	return stof(reply) * 1e-2;
	*/
	return 0;
}

void SiglentFunctionGenerator::SetFunctionChannelDutyCycle(int chan, float duty)
{
	/*
	//TODO: implement caps on duty cycle per manual
	//20-80% from DC to 10 MHz
	//40-60% from 10-40 MHz
	//fixed 50% past 40 MHz
	int percent = round(100 * duty);
	m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SQU:DCYC " + to_string(percent));
	*/
}

bool SiglentFunctionGenerator::HasFunctionRiseFallTimeControls(int /*chan*/)
{
	return false;
}

FunctionGenerator::OutputImpedance SiglentFunctionGenerator::GetFunctionChannelOutputImpedance(int chan)
{
	/*
	auto reply = Trim(m_transport->SendCommandQueuedWithReply(string("OUTP") + to_string(chan+1) + ":IMP?"));
	if(reply == "50")
		return IMPEDANCE_50_OHM;
	*/
	return IMPEDANCE_HIGH_Z;
}

void SiglentFunctionGenerator::SetFunctionChannelOutputImpedance(int chan, FunctionGenerator::OutputImpedance z)
{
	/*
	if(z == IMPEDANCE_HIGH_Z)
		m_transport->SendCommandQueued(string("OUTP") + to_string(chan+1) + ":IMP INF");
	else
		m_transport->SendCommandQueued(string("OUTP") + to_string(chan+1) + ":IMP 50");
		*/
}
