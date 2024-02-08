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
#include "FunctionGenerator.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

FunctionGenerator::FunctionGenerator()
{
	m_serializers.push_back(sigc::mem_fun(*this, &FunctionGenerator::DoSerializeConfiguration));
	m_loaders.push_back(sigc::mem_fun(*this, &FunctionGenerator::DoLoadConfiguration));
	m_preloaders.push_back(sigc::mem_fun(*this, &FunctionGenerator::DoPreLoadConfiguration));
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

FunctionGenerator::WaveShape FunctionGenerator::GetShapeOfName(const string& name)
{
	if(name =="Sine")
		return FunctionGenerator::SHAPE_SINE;
	else if(name == "Square")
		return FunctionGenerator::SHAPE_SQUARE;
	else if(name == "Triangle")
		return FunctionGenerator::SHAPE_TRIANGLE;
	else if(name == "Pulse")
		return FunctionGenerator::SHAPE_PULSE;
	else if(name == "DC")
		return FunctionGenerator::SHAPE_DC;
	else if(name == "Noise")
		return FunctionGenerator::SHAPE_NOISE;
	else if(name == "Sawtooth up")
		return FunctionGenerator::SHAPE_SAWTOOTH_UP;
	else if(name == "Sawtooth down")
		return FunctionGenerator::SHAPE_SAWTOOTH_DOWN;
	else if(name == "Sinc")
		return FunctionGenerator::SHAPE_SINC;
	else if(name == "Gaussian")
		return FunctionGenerator::SHAPE_GAUSSIAN;
	else if(name == "Lorentz")
		return FunctionGenerator::SHAPE_LORENTZ;
	else if(name == "Half sine")
		return FunctionGenerator::SHAPE_HALF_SINE;
	else if(name == "PRBS (nonstandard polynomial)")
		return FunctionGenerator::SHAPE_PRBS_NONSTANDARD;
	else if(name == "Exponential Rise")
		return FunctionGenerator::SHAPE_EXPONENTIAL_RISE;
	else if(name == "Exponential Decay")
		return FunctionGenerator::SHAPE_EXPONENTIAL_DECAY;
	else if(name == "Haversine")
		return FunctionGenerator::SHAPE_HAVERSINE;
	else if(name == "Cardiac")
		return FunctionGenerator::SHAPE_CARDIAC;
	else if(name == "Stairup")
		return FunctionGenerator::SHAPE_STAIRCASE_UP;
	else if(name == "Stairdown")
		return FunctionGenerator::SHAPE_STAIRCASE_DOWN;
	else if(name == "Stairtriangular")
		return FunctionGenerator::SHAPE_STAIRCASE_UP_DOWN;
	else if(name == "Negative pulse")
		return FunctionGenerator::SHAPE_NEGATIVE_PULSE;
	else if(name == "Logarithmic rise")
		return FunctionGenerator::SHAPE_LOG_RISE;
	else if(name == "Logarithmic decay")
		return FunctionGenerator::SHAPE_LOG_DECAY;
	else if(name == "Square root")
		return FunctionGenerator::SHAPE_SQUARE_ROOT;
	else if(name == "Cube root")
		return FunctionGenerator::SHAPE_CUBE_ROOT;
	else if(name == "Quadratic")
		return FunctionGenerator::SHAPE_QUADRATIC;
	else if(name == "Cubic")
		return FunctionGenerator::SHAPE_CUBIC;
	else if(name == "DLorentz")
		return FunctionGenerator::SHAPE_DLORENTZ;
	else if(name == "Gaussian pulse")
		return FunctionGenerator::SHAPE_GAUSSIAN_PULSE;
	else if(name == "Hamming")
		return FunctionGenerator::SHAPE_HAMMING;
	else if(name == "Hanning")
		return FunctionGenerator::SHAPE_HANNING;
	else if(name == "Kaiser")
		return FunctionGenerator::SHAPE_KAISER;
	else if(name == "Blackman")
		return FunctionGenerator::SHAPE_BLACKMAN;
	else if(name == "Gaussian window")
		return FunctionGenerator::SHAPE_GAUSSIAN_WINDOW;
	else if(name == "Harris")
		return FunctionGenerator::SHAPE_HARRIS;
	else if(name == "Bartlett")
		return FunctionGenerator::SHAPE_BARTLETT;
	else if(name == "Tan")
		return FunctionGenerator::SHAPE_TAN;
	else if(name == "Cot")
		return FunctionGenerator::SHAPE_COT;
	else if(name == "Sec")
		return FunctionGenerator::SHAPE_SEC;
	else if(name == "Csc")
		return FunctionGenerator::SHAPE_CSC;
	else if(name == "Asin")
		return FunctionGenerator::SHAPE_ASIN;
	else if(name == "Acos")
		return FunctionGenerator::SHAPE_ACOS;
	else if(name == "Atan")
		return FunctionGenerator::SHAPE_ATAN;
	else if(name == "Acot")
		return FunctionGenerator::SHAPE_ACOT;

		//Arbitrary is not supported yet so don't show it in the list
		//FunctionGenerator::SHAPE_ARBITRARY;
		//	continue;

	else //default to sine
		return FunctionGenerator::SHAPE_SINE;
}

float FunctionGenerator::GetFunctionChannelRiseTime(int /*chan*/)
{
	return 0;
}

void FunctionGenerator::SetFunctionChannelRiseTime(int /*chan*/, float /*fs*/)
{
}

float FunctionGenerator::GetFunctionChannelFallTime(int /*chan*/)
{
	return 0;
}

void FunctionGenerator::SetFunctionChannelFallTime(int /*chan*/, float /*fs*/)
{
}

bool FunctionGenerator::HasFunctionDutyCycleControls(int /*chan*/)
{
	return true;
}

float FunctionGenerator::GetFunctionChannelDutyCycle(int /*chan*/)
{
	return 0.5;
}

void FunctionGenerator::SetFunctionChannelDutyCycle(int /*chan*/, float /*duty*/)
{
}

bool FunctionGenerator::HasFunctionImpedanceControls(int /*chan*/)
{
	return true;
}

FunctionGenerator::OutputImpedance FunctionGenerator::GetFunctionChannelOutputImpedance(int /*chan*/)
{
	return IMPEDANCE_50_OHM;
}

void FunctionGenerator::SetFunctionChannelOutputImpedance(int /*chan*/, OutputImpedance /*z*/)
{
}

bool FunctionGenerator::AcquireData()
{
	//no-op for now
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

string FunctionGenerator::GetNameOfImpedance(OutputImpedance imp)
{
	switch(imp)
	{
		case IMPEDANCE_HIGH_Z:
			return "Hi-Z";

		case IMPEDANCE_50_OHM:
			return "50Ω";

		default:
			return "Invalid";
	}
}

FunctionGenerator::OutputImpedance FunctionGenerator::GetImpedanceOfName(const string& name)
{
	if(name == "Hi-Z")
		return IMPEDANCE_HIGH_Z;
	else if(name == "50Ω")
		return IMPEDANCE_50_OHM;

	//invalid
	else
		return IMPEDANCE_HIGH_Z;
}

void FunctionGenerator::DoSerializeConfiguration(YAML::Node& node, IDTable& table)
{
	for(size_t i=0; i<GetChannelCount(); i++)
	{
		if(0 == (GetInstrumentTypesForChannel(i) & Instrument::INST_FUNCTION))
			continue;

		auto chan = dynamic_cast<FunctionGeneratorChannel*>(GetChannel(i));

		//Save basic info
		auto key = "ch" + to_string(i);
		auto channelNode = node["channels"][key];
		channelNode["funcgenid"] = table.emplace(chan);

		//Common config every function generator channel has
		channelNode["enabled"] = GetFunctionChannelActive(i);
		YAML::Node shapenode;
		auto shapes = GetAvailableWaveformShapes(i);
		for(auto s : shapes)
			shapenode.push_back(GetNameOfShape(s));
		channelNode["shapes"] = shapenode;
		channelNode["amplitude"] = GetFunctionChannelAmplitude(i);
		channelNode["offset"] = GetFunctionChannelOffset(i);
		channelNode["frequency"] = GetFunctionChannelFrequency(i);
		channelNode["shape"] = GetNameOfShape(GetFunctionChannelShape(i));

		//Optional configuration not all instruments have
		if(HasFunctionDutyCycleControls(i))
			channelNode["duty"] = GetFunctionChannelDutyCycle(i);

		if(HasFunctionRiseFallTimeControls(i))
		{
			channelNode["rise"] = GetFunctionChannelRiseTime(i);
			channelNode["fall"] = GetFunctionChannelFallTime(i);
		}

		if(HasFunctionImpedanceControls(i))
			channelNode["impedance"] = GetNameOfImpedance(GetFunctionChannelOutputImpedance(i));
	}
}

void FunctionGenerator::DoPreLoadConfiguration(
	int /*version*/,
	const YAML::Node& node,
	IDTable& idmap,
	ConfigWarningList& list)
{
	Unit volts(Unit::UNIT_VOLTS);

	for(size_t i=0; i<GetChannelCount(); i++)
	{
		if(0 == (GetInstrumentTypesForChannel(i) & Instrument::INST_FUNCTION))
			continue;

		auto chan = dynamic_cast<FunctionGeneratorChannel*>(GetChannel(i));

		//Save basic info
		auto key = "ch" + to_string(i);
		auto channelNode = node["channels"][key];
		idmap.emplace(channelNode["funcgenid"].as<uintptr_t>(), chan);

		//Changing impedance from high-Z to 50 ohm will double output swing
		if(HasFunctionImpedanceControls(i))
		{
			auto imp = GetImpedanceOfName(channelNode["impedance"].as<string>());
			if( (imp == IMPEDANCE_50_OHM) && (GetFunctionChannelOutputImpedance(i) == IMPEDANCE_HIGH_Z))
			{
				list.m_warnings[this].m_messages.push_back(ConfigWarningMessage(
					chan->GetDisplayName() + " output impedance",
					"Changing impedance from high-Z to 50Ω will double output amplitude",
					"Hi-Z",
					"50Ω"));
			}
		}

		//Complain about increasing amplitude
		auto aact = GetFunctionChannelAmplitude(i);
		auto anom = channelNode["amplitude"].as<float>();
		if(anom > aact)
		{
			list.m_warnings[this].m_messages.push_back(ConfigWarningMessage(
				chan->GetDisplayName() + " amplitude",
				string("Increasing amplitude by ") + volts.PrettyPrint(anom - aact),
				volts.PrettyPrint(aact),
				volts.PrettyPrint(anom)));
		}

		//Complain about increasing magnitude of, or changing sign of, offset
		auto oact = GetFunctionChannelOffset(i);
		auto onom = channelNode["offset"].as<float>();
		if(fabs(onom) > fabs(oact))
		{
			list.m_warnings[this].m_messages.push_back(ConfigWarningMessage(
				chan->GetDisplayName() + " offset",
				string("Increasing offset magnitude by ") + volts.PrettyPrint(fabs(onom - oact)),
				volts.PrettyPrint(oact),
				volts.PrettyPrint(onom)));
		}
		if( ( (onom > 0) && (oact < 0)) || ( (onom < 0) && (oact > 0)) )
		{
			list.m_warnings[this].m_messages.push_back(ConfigWarningMessage(
				chan->GetDisplayName() + " offset",
				"Changing sign of offset",
				volts.PrettyPrint(oact),
				volts.PrettyPrint(onom)));
		}
	}
}

void FunctionGenerator::DoLoadConfiguration(int /*version*/, const YAML::Node& node, IDTable& /*idmap*/)
{
	for(size_t i=0; i<GetChannelCount(); i++)
	{
		if(0 == (GetInstrumentTypesForChannel(i) & Instrument::INST_FUNCTION))
			continue;

		//auto chan = dynamic_cast<FunctionGeneratorChannel*>(GetChannel(i));

		//Load basic info
		auto key = "ch" + to_string(i);
		auto channelNode = node["channels"][key];

		SetFunctionChannelAmplitude(i, channelNode["amplitude"].as<float>());
		SetFunctionChannelOffset(i, channelNode["offset"].as<float>());
		SetFunctionChannelFrequency(i, channelNode["frequency"].as<float>());
		SetFunctionChannelShape(i, GetShapeOfName(channelNode["shape"].as<string>()));

		//Optional configuration not all instruments have
		if(HasFunctionDutyCycleControls(i) && channelNode["duty"])
			SetFunctionChannelDutyCycle(i, channelNode["duty"].as<float>());

		if(HasFunctionRiseFallTimeControls(i))
		{
			SetFunctionChannelRiseTime(i, channelNode["rise"].as<float>());
			SetFunctionChannelFallTime(i, channelNode["fall"].as<float>());
		}

		if(HasFunctionImpedanceControls(i))
			SetFunctionChannelOutputImpedance(i, GetImpedanceOfName(channelNode["impedance"].as<string>()));

		SetFunctionChannelActive(i, channelNode["enabled"].as<bool>());
	}
}
