#include <assert.h> // assert

#include "config.h" // MAX_SIZE_NAME
#include "cinfowifi.h"

CInfoWifi::CInfoWifi(): CCoordinate()
{
	SetCid(0);
	LinkUp=true;
	Household=0;
}

CInfoWifi::CInfoWifi(TCID cid, CCoordinate coo) : CCoordinate(coo)
{
	SetCid(cid);
	LinkUp=true;
	Household=0;
}

void CInfoWifi::SetCid(TCID cid)
{
	// with the empty constructor : cid=0
	assert( cid==0 || cid >=TCID_GUEST_MIN );
	Cid=cid;
}

TCID CInfoWifi::GetCid() const
{
	return Cid;
}

void CInfoWifi::SetName(string name)
{
	if( name.size() > MAX_SIZE_NAME )
		name.resize(MAX_SIZE_NAME);

	Name=name;
}

string CInfoWifi::GetName() const
{
	return Name;
}

int CInfoWifi::GetSizeName() const
{
	return Name.size();
}

bool CInfoWifi::HasName() const
{
	return ! Name.empty();
}

void CInfoWifi::LearnMac(const string& mac)
{
	Macs.insert(mac);
}

bool CInfoWifi::OwnsMac(const string& mac) const
{
	return Macs.find(mac) != Macs.end();
}

void CInfoWifi::SetLinkUp(bool up)
{
	LinkUp=up;
}

bool CInfoWifi::IsLinkUp() const
{
	return LinkUp;
}

void CInfoWifi::SetBeaconsRelayed(const string& mac, bool relayed)
{
	if( relayed )
		BeaconBlackhole.erase(mac);
	else
		BeaconBlackhole.insert(mac);
}

bool CInfoWifi::AreBeaconsRelayed(const string& mac) const
{
	// The common case by a wide margin, and the one worth not hashing for.
	if( BeaconBlackhole.empty() )
		return true;

	return BeaconBlackhole.find(mac) == BeaconBlackhole.end();
}

void CInfoWifi::SetHousehold(u32 household)
{
	Household=household;
}

u32 CInfoWifi::GetHousehold() const
{
	return Household;
}

void CInfoWifi::ReportRadio(const VwifiRadioEntry& entry)
{
	map<u32,CRadioState>::iterator it=Radios.find(entry.radio_id);
	if( it == Radios.end() )
		it=Radios.insert(make_pair(entry.radio_id,CRadioState(entry.radio_id))).first;

	// Channel and power are replaced; the airtime counters and the noise floor
	// are not. The counters have to survive because every consumer diffs them,
	// and the floor because it is set from vwifi-ctrl and the node reporting
	// its channel is not the node retracting that.
	it->second.Channel=CChannel(entry.frequency,entry.channel_width);
	it->second.TxPower=static_cast<TPower>(entry.tx_power);
}

CRadioState* CInfoWifi::GetRadio(u32 radioId)
{
	map<u32,CRadioState>::iterator it=Radios.find(radioId);
	if( it == Radios.end() )
		return NULL;

	return &(it->second);
}

const map<u32,CRadioState>& CInfoWifi::GetRadios() const
{
	return Radios;
}

u32 CInfoWifi::SetNoiseFloor(u32 radioId, int noiseFloorDbm)
{
	u32 touched=0;

	for(map<u32,CRadioState>::iterator it=Radios.begin(); it != Radios.end(); ++it)
	{
		if( radioId != RADIO_ID_ALL && it->first != radioId )
			continue;

		it->second.NoiseFloor=noiseFloorDbm;
		touched++;
	}

	return touched;
}

bool CInfoWifi::CanReceiveOn(const CChannel& channel) const
{
	// A node that has never reported stays reachable. Silently dropping frames
	// to a client whose report has not arrived yet would look exactly like a
	// medium fault, and the report is periodic : it is always about to arrive.
	if( Radios.empty() )
		return true;

	for(map<u32,CRadioState>::const_iterator it=Radios.begin(); it != Radios.end(); ++it)
		if( ChannelsCanCarry(channel,it->second.Channel) )
			return true;

	return false;
}

int CInfoWifi::NoiseFloorOn(const CChannel& channel) const
{
	for(map<u32,CRadioState>::const_iterator it=Radios.begin(); it != Radios.end(); ++it)
		if( ChannelsCanCarry(channel,it->second.Channel) )
			return it->second.NoiseFloor;

	return DEFAULT_NOISE_FLOOR_DBM;
}

void CInfoWifi::AccumulateAirtime(const CChannel& channel, u32 airtimeUs, bool sameHousehold)
{
	for(map<u32,CRadioState>::iterator it=Radios.begin(); it != Radios.end(); ++it)
	{
		double fraction=OverlapFraction(channel,it->second.Channel);
		if( fraction <= 0.0 )
			continue;

		u64 share=static_cast<u64>(static_cast<double>(airtimeUs)*fraction);

		if( sameHousehold )
			it->second.RxUs += share;
		else
			it->second.ExtUs += share;
	}
}

void CInfoWifi::AccountOwnTransmission(const VwifiRadioInfo& info, u32 airtimeUs)
{
	VwifiRadioEntry entry;
	entry.radio_id=info.radio_id;
	entry.frequency=info.frequency;
	entry.channel_width=info.channel_width;
	entry.tx_power=info.tx_power;

	// Only when the frame actually carried a channel. A frame with frequency 0
	// would otherwise retract a perfectly good report from the radio itself.
	if( info.frequency != 0 )
		ReportRadio(entry);
	else if( Radios.find(info.radio_id) == Radios.end() )
		Radios.insert(make_pair(info.radio_id,CRadioState(info.radio_id)));

	Radios[info.radio_id].TxUs += airtimeUs;
}

void CInfoWifi::Display(ostream& os) const
{
	os << Cid << " ";
	if( HasName() )
		os << "("<<Name<<") ";
	if( ! LinkUp )
		os << "[RF down] ";
	if( ! BeaconBlackhole.empty() )
		os << "[no beacons from "<<BeaconBlackhole.size()<<" BSS] ";
	if( Household != 0 )
		os << "{household "<<Household<<"} ";
	CCoordinate::Display(os);
	for(map<u32,CRadioState>::const_iterator it=Radios.begin(); it != Radios.end(); ++it)
	{
		os << " radio"<<it->first<<"=";
		if( it->second.Channel.IsKnown() )
			os << it->second.Channel.Centre<<"/"<<it->second.Channel.Width<<"MHz";
		else
			os << "?";
	}
}

ostream& operator<<(ostream& os, const CInfoWifi& infowifi)
{
	infowifi.Display(os) ;
	return os;
}
