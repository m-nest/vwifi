#include <assert.h> // assert

#include "config.h" // MAX_SIZE_NAME
#include "cinfowifi.h"

CInfoWifi::CInfoWifi(): CCoordinate()
{
	SetCid(0);
	LinkUp=true;
}

CInfoWifi::CInfoWifi(TCID cid, CCoordinate coo) : CCoordinate(coo)
{
	SetCid(cid);
	LinkUp=true;
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

void CInfoWifi::Display(ostream& os) const
{
	os << Cid << " ";
	if( HasName() )
		os << "("<<Name<<") ";
	if( ! LinkUp )
		os << "[RF down] ";
	CCoordinate::Display(os);
}

ostream& operator<<(ostream& os, const CInfoWifi& infowifi)
{
	infowifi.Display(os) ;
	return os;
}
