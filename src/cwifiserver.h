#ifndef _CWIFISERVER_H_
#define _CWIFISERVER_H_

#include "csocketserver.h"
#include "cinfowifi.h"
#include "cwifi.h"

extern bool CanLostPackets;

class CWifiServer : public CSocketServer, public CWifi
{
		friend class CCTRLServer;

		TIndex MaxClientDeconnected;

		CListInfo<CInfoWifi>* InfoWifis;
		CListInfo<CInfoWifi>* InfoWifisDeconnected;

		bool RecoverInfosOfInfoWifiDeconnected(TCID cid, CCoordinate& coo, string& name);

		bool RecoverInfosOfInfoWifi(TCID cid, CCoordinate& coo, string& name);

		void DefaultValues();

	public :

		CWifiServer();

		CWifiServer(CListInfo<CInfoSocket>* infoSockets, CListInfo<CInfoWifi>* infoWifis, CListInfo<CInfoWifi>* infoWifisDeconnected);

		CWifiServer( const CWifiServer & wifiServer );

		~CWifiServer();

		CWifiServer& operator=(const CWifiServer& wifiServer);

		bool Listen(TIndex maxClientDeconnected);

		TDescriptor Accept();

		void ShowInfoWifi(TIndex index);

		void CloseClient(TIndex index);

		void CloseAllClient();

		ssize_t SendSignal(TDescriptor descriptor, VwifiRadioInfo* radio_info, const char* buffer, int sizeOfBuffer);

		ssize_t RecvSignal(TDescriptor descriptor, VwifiRadioInfo* radio_info, CDynBuffer* buffer);

		void SendAllOtherClients(TIndex index, VwifiRadioInfo* radio_info, const char* data, ssize_t sizeOfData);

		bool ClientLinkIsUp(TIndex index) const;

		// Records the address a client transmits from, so that it can later be
		// named by MAC from vwifi-ctrl.
		void LearnTransmitter(TIndex index, char* data, ssize_t sizeOfData);

		// Cuts or restores the RF link of the client transmitting from mac.
		// Returns false when no connected client has ever transmitted from it.
		bool SetLinkStateByMac(const string& mac, bool up);

		void SendAllOtherClientsWithoutLoss(TIndex index, VwifiRadioInfo* radio_info, const char* data, ssize_t sizeOfData);

		void SendAllClientsWithoutLoss(VwifiRadioInfo* radio_info, const char* data, ssize_t sizeOfData);

		CInfoWifi* GetReferenceOnInfoWifiByCID(TCID cid) const;

		CInfoWifi* GetReferenceOnInfoWifiDeconnectedByCID(TCID cid) const;

		CInfoWifi* GetReferenceOnInfoWifiByIndex(TIndex index) const;

		void AddInfoWifiDeconnected(CInfoWifi infoWifi);

};

#endif
