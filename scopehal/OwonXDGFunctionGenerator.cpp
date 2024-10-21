/***********************************************************************************************************************
*                                                                                                                      *
* libscopehal v0.1                                                                                                     *
*                                                                                                                      *
* Copyright (c) 2012-2023 Andrew D. Zonenberg and contributors                                                         *
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
#include "OwonXDGFunctionGenerator.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

OwonXDGFunctionGenerator::OwonXDGFunctionGenerator(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
{
	//All XDG series have two channels
	m_channels.push_back(new FunctionGeneratorChannel(this, "CH1", "#ffff00", 0));
	m_channels.push_back(new FunctionGeneratorChannel(this, "CH2", "#00ffff", 1));

	for(int i=0; i<2; i++)
	{
		m_cachedFrequency[i] = 0;
		m_cachedFrequencyValid[i] = false;
	}
}

OwonXDGFunctionGenerator::~OwonXDGFunctionGenerator()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Instrument

unsigned int OwonXDGFunctionGenerator::GetInstrumentTypes() const
{
	return INST_FUNCTION;
}

uint32_t OwonXDGFunctionGenerator::GetInstrumentTypesForChannel(size_t i) const
{
	if(i < 2)
		return INST_FUNCTION;
	else
		return 0;
}

bool OwonXDGFunctionGenerator::AcquireData()
{
	return true;
}

string OwonXDGFunctionGenerator::GetDriverNameInternal()
{
	return "owon_xdg";
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FunctionGenerator

vector<FunctionGenerator::WaveShape> OwonXDGFunctionGenerator::GetAvailableWaveformShapes(int /*chan*/)
{
	vector<WaveShape> ret;
	ret.push_back(SHAPE_SINE);
	ret.push_back(SHAPE_SQUARE);
	ret.push_back(SHAPE_TRIANGLE);
	ret.push_back(SHAPE_PULSE);
	ret.push_back(SHAPE_DC);
	ret.push_back(SHAPE_NOISE);
	ret.push_back(SHAPE_SAWTOOTH_UP);
	//ret.push_back(SHAPE_SAWTOOTH_DOWN);
	ret.push_back(SHAPE_SINC);
	ret.push_back(SHAPE_GAUSSIAN);
	ret.push_back(SHAPE_LORENTZ);
	ret.push_back(SHAPE_HALF_SINE);
	//ret.push_back(SHAPE_PRBS_NONSTANDARD);
	ret.push_back(SHAPE_EXPONENTIAL_RISE);
	ret.push_back(SHAPE_EXPONENTIAL_DECAY);
	ret.push_back(SHAPE_HAVERSINE);
	ret.push_back(SHAPE_CARDIAC);

	ret.push_back(SHAPE_STAIRCASE_UP);
	ret.push_back(SHAPE_STAIRCASE_DOWN);
	ret.push_back(SHAPE_STAIRCASE_UP_DOWN);
	ret.push_back(SHAPE_NEGATIVE_PULSE);
	ret.push_back(SHAPE_LOG_RISE);
	//ret.push_back(SHAPE_LOG_DECAY);
	ret.push_back(SHAPE_SQUARE_ROOT);
	//ret.push_back(SHAPE_CUBE_ROOT);
	//ret.push_back(SHAPE_QUADRATIC);
	//ret.push_back(SHAPE_CUBIC);
	//ret.push_back(SHAPE_DLORENTZ);
	ret.push_back(SHAPE_GAUSSIAN_PULSE);
	ret.push_back(SHAPE_HAMMING);
	ret.push_back(SHAPE_HANNING);
	ret.push_back(SHAPE_KAISER);
	ret.push_back(SHAPE_BLACKMAN);
	//ret.push_back(SHAPE_GAUSSIAN_WINDOW);
	//ret.push_back(SHAPE_HARRIS);
	ret.push_back(SHAPE_BARTLETT);
	ret.push_back(SHAPE_TAN);
	ret.push_back(SHAPE_COT);
	ret.push_back(SHAPE_SEC);
	ret.push_back(SHAPE_CSC);
	ret.push_back(SHAPE_ASIN);
	ret.push_back(SHAPE_ACOS);
	ret.push_back(SHAPE_ATAN);
	ret.push_back(SHAPE_ACOT);

	ret.push_back(SHAPE_ARB);
	return ret;
}

bool OwonXDGFunctionGenerator::GetFunctionChannelActive(int chan)
{
	auto reply = Trim(m_transport->SendCommandQueuedWithReply(string("OUTP") + to_string(chan+1) + ":STAT?"));
	if(reply == "1")
		return true;
	return false;
}

void OwonXDGFunctionGenerator::SetFunctionChannelActive(int chan, bool on)
{
	if(on)
		m_transport->SendCommandQueued(string("OUTP") + to_string(chan+1) + ":STAT ON");
	else
		m_transport->SendCommandQueued(string("OUTP") + to_string(chan+1) + ":STAT OFF");
}

float OwonXDGFunctionGenerator::GetFunctionChannelAmplitude(int chan)
{
	auto reply = Trim(m_transport->SendCommandQueuedWithReply(string("SOUR") + to_string(chan+1) + ":VOLT?"));
	return stof(reply);
}

void OwonXDGFunctionGenerator::SetFunctionChannelAmplitude(int chan, float amplitude)
{
	m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":VOLT " + to_string(amplitude));
}

float OwonXDGFunctionGenerator::GetFunctionChannelOffset(int chan)
{
	auto reply = Trim(m_transport->SendCommandQueuedWithReply(string("SOUR") + to_string(chan+1) + ":VOLT:OFFS?"));
	return stof(reply);
}

void OwonXDGFunctionGenerator::SetFunctionChannelOffset(int chan, float offset)
{
	m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":VOLT:OFFS " + to_string(offset));
}

float OwonXDGFunctionGenerator::GetFunctionChannelFrequency(int chan)
{
	if(m_cachedFrequencyValid[chan])
		return m_cachedFrequency[chan];

	auto reply = Trim(m_transport->SendCommandQueuedWithReply(string("SOUR") + to_string(chan+1) + ":FREQ?"));
	m_cachedFrequency[chan] = stof(reply);
	m_cachedFrequencyValid[chan] = true;
	return m_cachedFrequency[chan];
}

void OwonXDGFunctionGenerator::SetFunctionChannelFrequency(int chan, float hz)
{
	m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FREQ " + to_string(hz));

	m_cachedFrequency[chan] = hz;
	m_cachedFrequencyValid[chan] = true;
}

FunctionGenerator::WaveShape OwonXDGFunctionGenerator::GetFunctionChannelShape(int chan)
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
	else if(reply == "PRNoise")
		return SHAPE_NOISE;
	else if(reply == "DC")
		return SHAPE_DC;
	else if(reply == "AbsSine")
		return SHAPE_HALF_SINE;
	else if(reply == "GaussPulse")
		return SHAPE_GAUSSIAN_PULSE;
	else if(reply == "NPulse")
		return SHAPE_NEGATIVE_PULSE;
	else if(reply == "StairDn")
		return SHAPE_STAIRCASE_DOWN;
	else if(reply == "StairUD")
		return SHAPE_STAIRCASE_UP_DOWN;
	else if(reply == "StairUp")
		return SHAPE_STAIRCASE_UP;
	else if(reply == "Cardiac")
		return SHAPE_CARDIAC;
	else if(reply == "CUBIC")
		return SHAPE_CUBIC;
	else if(reply == "ExpFall")
		return SHAPE_EXPONENTIAL_DECAY;
	else if(reply == "ExpRise")
		return SHAPE_EXPONENTIAL_RISE;
	else if(reply == "Gauss")
		return SHAPE_GAUSSIAN;
	else if(reply == "Lorentz")
		return SHAPE_LORENTZ;
	else if(reply == "HaverSine")
		return SHAPE_HAVERSINE;
	else if(reply == "Log")
		return SHAPE_LOG_RISE;
	else if(reply == "Cot")
		return SHAPE_COT;
	else if(reply == "SecCon")
		return SHAPE_SEC;
	else if(reply == "Csc")
		return SHAPE_CSC;
	else if(reply == "Sinc")
		return SHAPE_SINC;
	else if(reply == "Sqrt")
		return SHAPE_SQUARE_ROOT;
	else if(reply == "Tan")
		return SHAPE_TAN;
	else if(reply == "ACos")
		return SHAPE_ACOS;
	else if(reply == "ASin")
		return SHAPE_ASIN;
	else if(reply == "ATan")
		return SHAPE_ATAN;
	else if(reply == "ACot")
		return SHAPE_ACOT;
	else if(reply == "Bartlett")
		return SHAPE_BARTLETT;
	else if(reply == "Hamming")
		return SHAPE_HAMMING;
	else if(reply == "Hanning")
		return SHAPE_HANNING;
	else if(reply == "Kaiser")
		return SHAPE_KAISER;
	else if(reply == "Blackman")
		return SHAPE_BLACKMAN;
	else if(reply == "Triang")
		return SHAPE_TRIANGLE;
	else if(reply == "EMEMory")
		return SHAPE_ARB;

	return SHAPE_SINE;
}

void OwonXDGFunctionGenerator::SetFunctionChannelShape(int chan, WaveShape shape)
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
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP PRNoise");
			break;

		case SHAPE_DC:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP DC");
			break;

		case SHAPE_HALF_SINE:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP AbsSine");
			break;

		case SHAPE_GAUSSIAN_PULSE:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP GaussPulse");
			break;

		case SHAPE_NEGATIVE_PULSE:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP NPulse");
			break;

		case SHAPE_STAIRCASE_DOWN:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP StairDn");
			break;

		case SHAPE_STAIRCASE_UP_DOWN:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP StairUD");
			break;

		case SHAPE_STAIRCASE_UP:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP StairUp");
			break;

		case SHAPE_CARDIAC:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP Cardiac");
			break;

		case SHAPE_CUBIC:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP CUBIC");
			break;

		case SHAPE_EXPONENTIAL_DECAY:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP ExpFall");
			break;

		case SHAPE_EXPONENTIAL_RISE:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP ExpRise");
			break;

		case SHAPE_GAUSSIAN:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP Gauss");
			break;

		case SHAPE_LORENTZ:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP Lorentz");
			break;

		case SHAPE_HAVERSINE:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP HaverSine");
			break;

		case SHAPE_LOG_RISE:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP Log");
			break;

		case SHAPE_COT:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP Cot");
			break;

		case SHAPE_SEC:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP SecCon");
			break;

		case SHAPE_CSC:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP Csc");
			break;

		case SHAPE_SINC:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP Sinc");
			break;

		case SHAPE_SQUARE_ROOT:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP Sqrt");
			break;

		case SHAPE_TAN:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP Tan");
			break;

		case SHAPE_ACOS:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP ACos");
			break;

		case SHAPE_ASIN:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP ASin");
			break;

		case SHAPE_ATAN:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP ATan");
			break;

		case SHAPE_ACOT:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP ACot");
			break;

		case SHAPE_BARTLETT:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP Bartlett");
			break;

		case SHAPE_HAMMING:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP Hamming");
			break;

		case SHAPE_HANNING:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP Hanning");
			break;

		case SHAPE_KAISER:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP Kaiser");
			break;

		case SHAPE_BLACKMAN:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP Blackman");
			break;

		case SHAPE_TRIANGLE:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP Triang");
			break;

		case SHAPE_ARB:
			m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":FUNC:SHAP EMEMory");
			break;

		default:
			LogWarning("[OwonXDGFunctionGenerator::SetFunctionChannelShape] unrecognized shape %d", shape);
			break;
	}
}

float OwonXDGFunctionGenerator::GetFunctionChannelDutyCycle(int chan)
{
	auto reply = Trim(m_transport->SendCommandQueuedWithReply(string("SOUR") + to_string(chan+1) + ":PULS:DCYC?"));
	return stof(reply) * 1e-2;
}

void OwonXDGFunctionGenerator::SetFunctionChannelDutyCycle(int chan, float duty)
{
	int percent = round(100 * duty);
	m_transport->SendCommandQueued(string("SOUR") + to_string(chan+1) + ":PULS:DCYC " + to_string(percent));
}

bool OwonXDGFunctionGenerator::HasFunctionRiseFallTimeControls(int /*chan*/)
{
	return false;
}

FunctionGenerator::OutputImpedance OwonXDGFunctionGenerator::GetFunctionChannelOutputImpedance(int chan)
{
	auto reply = Trim(m_transport->SendCommandQueuedWithReply(string("OUTP") + to_string(chan+1) + ":IMP?"));
	if(reply == "50")
		return IMPEDANCE_50_OHM;
	return IMPEDANCE_HIGH_Z;
}

void OwonXDGFunctionGenerator::SetFunctionChannelOutputImpedance(int chan, FunctionGenerator::OutputImpedance z)
{
	if(z == IMPEDANCE_HIGH_Z)
		m_transport->SendCommandQueued(string("OUTP") + to_string(chan+1) + ":IMP INF");
	else
		m_transport->SendCommandQueued(string("OUTP") + to_string(chan+1) + ":IMP 50");
}
