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

#include "scopehal.h"
#include "RigolFunctionGenerator.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

RigolFunctionGenerator::RigolFunctionGenerator(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
{
	//All DG4000 series have two channels
	m_channels.push_back(new FunctionGeneratorChannel(this, "CH1", "#ffff00", 0));
	m_channels.push_back(new FunctionGeneratorChannel(this, "CH2", "#00ffff", 1));

	for(int i=0; i<2; i++)
	{
		m_cachedFrequency[i] = 0;
		m_cachedFrequencyValid[i] = false;
	}
}

RigolFunctionGenerator::~RigolFunctionGenerator()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Instrument

unsigned int RigolFunctionGenerator::GetInstrumentTypes() const
{
	return INST_FUNCTION;
}

uint32_t RigolFunctionGenerator::GetInstrumentTypesForChannel(size_t i) const
{
	if(i < 2)
		return INST_FUNCTION;
	else
		return 0;
}

bool RigolFunctionGenerator::AcquireData()
{
	return true;
}

string RigolFunctionGenerator::GetDriverNameInternal()
{
	return "rigol_awg";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FunctionGenerator

vector<FunctionGenerator::WaveShape> RigolFunctionGenerator::GetAvailableWaveformShapes(int /*chan*/)
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

bool RigolFunctionGenerator::GetFunctionChannelActive(int chan)
{
	auto reply = Trim(m_transport->SendCommandQueuedWithReply(string("OUTP") + to_string(chan+1) + ":STAT?"));
	if(reply == "ON")
		return true;
	return false;
}

void RigolFunctionGenerator::SetFunctionChannelActive(int chan, bool on)
{
	if(on)
		m_transport->SendCommandQueued(string("OUTP") + to_string(chan+1) + ":STAT ON");
	else
		m_transport->SendCommandQueued(string("OUTP") + to_string(chan+1) + ":STAT OFF");
}

float RigolFunctionGenerator::GetFunctionChannelAmplitude(int chan)
{
	auto reply = Trim(m_transport->SendCommandQueuedWithReply(string("SOUR") + to_string(chan+1) + ":VOLT?"));
	return stof(reply);
}

void RigolFunctionGenerator::SetFunctionChannelAmplitude(int chan, float amplitude)
{
	m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":VOLT " + to_string(amplitude));
}

float RigolFunctionGenerator::GetFunctionChannelOffset(int chan)
{
	auto reply = Trim(m_transport->SendCommandQueuedWithReply(string("SOUR") + to_string(chan+1) + ":VOLT:OFFS?"));
	return stof(reply);
}

void RigolFunctionGenerator::SetFunctionChannelOffset(int chan, float offset)
{
	m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":VOLT:OFFS " + to_string(offset));
}

float RigolFunctionGenerator::GetFunctionChannelFrequency(int chan)
{
	if(m_cachedFrequencyValid[chan])
		return m_cachedFrequency[chan];

	auto reply = Trim(m_transport->SendCommandQueuedWithReply(string("SOUR") + to_string(chan+1) + ":FREQ?"));
	m_cachedFrequency[chan] = stof(reply);
	m_cachedFrequencyValid[chan] = true;
	return m_cachedFrequency[chan];
}

void RigolFunctionGenerator::SetFunctionChannelFrequency(int chan, float hz)
{
	m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FREQ " + to_string(hz));

	m_cachedFrequency[chan] = hz;
	m_cachedFrequencyValid[chan] = true;
}

FunctionGenerator::WaveShape RigolFunctionGenerator::GetFunctionChannelShape(int chan)
{
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

	return SHAPE_SINE;
}

void RigolFunctionGenerator::SetFunctionChannelShape(int chan, WaveShape shape)
{
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
			LogWarning("[RigolFunctionGenerator::SetFunctionChannelShape] unrecognized shape %d", shape);
			break;
	}
}

float RigolFunctionGenerator::GetFunctionChannelDutyCycle(int chan)
{
	auto reply = Trim(m_transport->SendCommandQueuedWithReply(string("SOUR") + to_string(chan+1) + ":FUNC:SQU:DCYC?"));
	return stof(reply) * 1e-2;
}

void RigolFunctionGenerator::SetFunctionChannelDutyCycle(int chan, float duty)
{
	//TODO: implement caps on duty cycle per manual
	//20-80% from DC to 10 MHz
	//40-60% from 10-40 MHz
	//fixed 50% past 40 MHz
	int percent = round(100 * duty);
	m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SQU:DCYC " + to_string(percent));
}

bool RigolFunctionGenerator::HasFunctionRiseFallTimeControls(int /*chan*/)
{
	return false;
}

FunctionGenerator::OutputImpedance RigolFunctionGenerator::GetFunctionChannelOutputImpedance(int chan)
{
	auto reply = Trim(m_transport->SendCommandQueuedWithReply(string("OUTP") + to_string(chan+1) + ":IMP?"));
	if(reply == "50")
		return IMPEDANCE_50_OHM;
	return IMPEDANCE_HIGH_Z;
}

void RigolFunctionGenerator::SetFunctionChannelOutputImpedance(int chan, FunctionGenerator::OutputImpedance z)
{
	if(z == IMPEDANCE_HIGH_Z)
		m_transport->SendCommandQueued(string("OUTP") + to_string(chan+1) + ":IMP INF");
	else
		m_transport->SendCommandQueued(string("OUTP") + to_string(chan+1) + ":IMP 50");
}
