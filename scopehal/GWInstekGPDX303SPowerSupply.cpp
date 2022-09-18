#include <bitset>
#include "scopehal.h"
#include "GWInstekGPDX303SPowerSupply.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

GWInstekGPDX303SPowerSupply::GWInstekGPDX303SPowerSupply(SCPITransport* transport)
	: SCPIDevice(transport)
	, SCPIInstrument(transport)
{
	auto modelNumber = atoi(m_model.c_str() + strlen("GPD-"));
	// The GPD-3303S/D models have three channels, but only two are programmable and visible via SCPI
	if (modelNumber == 3303) {
		m_channelCount = 2;
	} else {
		m_channelCount = modelNumber/1000;
	}
}

GWInstekGPDX303SPowerSupply::~GWInstekGPDX303SPowerSupply()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device info

string GWInstekGPDX303SPowerSupply::GetDriverNameInternal()
{
	return "gwinstek_gpdx303s";
}


string GWInstekGPDX303SPowerSupply::GetName()
{
	return m_model;
}

string GWInstekGPDX303SPowerSupply::GetVendor()
{
	return m_vendor;
}

string GWInstekGPDX303SPowerSupply::GetSerial()
{
	return m_serial;
}

unsigned int GWInstekGPDX303SPowerSupply::GetInstrumentTypes()
{
	return INST_PSU;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Device capabilities

bool GWInstekGPDX303SPowerSupply::SupportsSoftStart()
{
	return false;
}

bool GWInstekGPDX303SPowerSupply::SupportsIndividualOutputSwitching()
{
	return false;
}

bool GWInstekGPDX303SPowerSupply::SupportsMasterOutputSwitching()
{
	return true;
}

bool GWInstekGPDX303SPowerSupply::SupportsOvercurrentShutdown()
{
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual hardware interfacing

bool GWInstekGPDX303SPowerSupply::IsPowerConstantCurrent(int chan)
{
	auto reg = GetStatusRegister();
	if (chan >= 2) {
		// TODO - examine a real-world output of the `STATUS?` command on a GPD-4303S, STATUS? is only documented for two channels in the user manual.
		LogError("Error: CC/CV status encoding unknown for 3/4 channel scopes.\n");
	}
	return !reg[7 - chan];
}

bitset<8> GWInstekGPDX303SPowerSupply::GetStatusRegister()
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	//Get status register
	auto ret = m_transport->SendCommandQueuedWithReply("STATUS?");
	// 8 ASCII 0/1 characters representing bits in the following format, index 0 being most-significant bit:
	// Bit    Item     Description
	// 0      CH1      0=CC mode, 1=CV mode
	// 1      CH2      0=CC mode, 1=CV mode
	// 2, 3   Tracking 01=Independent, 11=Tracking series, 10=Tracking parallel
	// 4      Beep     0=Off, 1=On
	// 5      Output   0=Off, 1=On
	// 6, 7   Baud     00=115200bps, 01=57600bps, 10=9600bps

	// Remove trailing newline from status reply and parse as bitset
	return bitset<8>(ret.substr(0, 8));
}

int GWInstekGPDX303SPowerSupply::GetPowerChannelCount()
{
	return m_channelCount;
}

string GWInstekGPDX303SPowerSupply::GetPowerChannelName(int chan)
{
	char tmp[] = "CH1";
	tmp[2] += chan;
	return string(tmp);
}

double GWInstekGPDX303SPowerSupply::GetPowerVoltageActual(int chan)
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	char tmpCmd[] = "VOUT1?";
	tmpCmd[4] += chan;
	auto ret = m_transport->SendCommandQueuedWithReply(string(tmpCmd));
	return atof(ret.c_str());
}

double GWInstekGPDX303SPowerSupply::GetPowerVoltageNominal(int chan)
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	char tmpCmd[] = "VSET1?";
	tmpCmd[4] += chan;
	auto ret = m_transport->SendCommandQueuedWithReply(string(tmpCmd));
	return atof(ret.c_str());
}

double GWInstekGPDX303SPowerSupply::GetPowerCurrentActual(int chan)
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	char tmpCmd[] = "IOUT1?";
	tmpCmd[4] += chan;
	auto ret = m_transport->SendCommandQueuedWithReply(string(tmpCmd));
	return atof(ret.c_str());
}

double GWInstekGPDX303SPowerSupply::GetPowerCurrentNominal(int chan)
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	char tmpCmd[] = "ISET1?";
	tmpCmd[4] += chan;
	auto ret = m_transport->SendCommandQueuedWithReply(string(tmpCmd));
	return atof(ret.c_str());
}

bool GWInstekGPDX303SPowerSupply::GetPowerChannelActive(int chan)
{
	(void) chan;
	return true;
}

bool GWInstekGPDX303SPowerSupply::IsSoftStartEnabled(int chan)
{
	(void) chan;
	return false;
}

void GWInstekGPDX303SPowerSupply::SetSoftStartEnabled(int chan, bool enable)
{
	(void) chan;
	(void) enable;
}

void GWInstekGPDX303SPowerSupply::SetPowerOvercurrentShutdownEnabled(int chan, bool enable)
{
	(void) chan;
	(void) enable;
}

bool GWInstekGPDX303SPowerSupply::GetPowerOvercurrentShutdownEnabled(int chan)
{
	(void) chan;
	return false;
}

bool GWInstekGPDX303SPowerSupply::GetPowerOvercurrentShutdownTripped(int chan)
{
	(void) chan;
	return false;
}

void GWInstekGPDX303SPowerSupply::SetPowerVoltage(int chan, double volts)
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	char cmd[128];
	if(!m_model.empty() && m_model.back() == 'D') {
		// The GPD-3303D only claims to support 100mV voltage granularity
		snprintf(cmd, sizeof(cmd), "VSET%u:%.1f", chan+1, volts);
	} else {
		snprintf(cmd, sizeof(cmd), "VSET%u:%.3f", chan+1, volts);
	}
	m_transport->SendCommandQueued(cmd);
}

void GWInstekGPDX303SPowerSupply::SetPowerCurrent(int chan, double amps)
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	char cmd[128];
	if(!m_model.empty() && m_model.back() == 'D') {
		// The GPD-3303D only claims to support 10mA current granularity
		snprintf(cmd, sizeof(cmd), "ISET%u:%.2f", chan+1, amps);
	} else {
		snprintf(cmd, sizeof(cmd), "ISET%u:%.3f", chan+1, amps);
	}
	m_transport->SendCommandQueued(cmd);
}

void GWInstekGPDX303SPowerSupply::SetPowerChannelActive(int chan, bool on)
{
	(void) chan;
	(void) on;
}

bool GWInstekGPDX303SPowerSupply::GetMasterPowerEnable()
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());

	auto reg = GetStatusRegister();
	return reg[7-5];
}

void GWInstekGPDX303SPowerSupply::SetMasterPowerEnable(bool enable)
{
	lock_guard<recursive_mutex> lock(m_transport->GetMutex());
	if (enable)
		m_transport->SendCommandQueued("OUT1");
	else
		m_transport->SendCommandQueued("OUT0");
}