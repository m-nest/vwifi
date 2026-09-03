#ifndef _CINFOWIFI_H_
#define _CINFOWIFI_H_

#include <iostream> // ostream
#include <set>
#include <string>

#include "ccoordinate.h"
#include "types.h" // TCID

const TCID TCID_GUEST_MIN=3;

using namespace std;

class CInfoWifi : public CCoordinate
{
		TCID Cid;
		string Name;

		// A frame carries its transmitter's hwsim address and nothing else
		// identifying the client, so this is how a client becomes addressable
		// by MAC. One client can own several radios, hence a set.
		set<string> Macs;

		// false : the client is still connected and still transmitting, but
		// nothing it sends is relayed and nothing reaches it
		bool LinkUp;

	public :

		CInfoWifi();
		CInfoWifi(TCID cid, CCoordinate coo);

		void SetCid(TCID cid);

		TCID GetCid() const;

		void SetName(string name);
		string GetName() const;
		int GetSizeName() const;
		bool HasName() const;

		void LearnMac(const string& mac);
		bool OwnsMac(const string& mac) const;

		void SetLinkUp(bool up);
		bool IsLinkUp() const;

		void Display(ostream& os) const;

		friend ostream& operator<<(ostream& os, const CInfoWifi& infowifi);

};

#endif
