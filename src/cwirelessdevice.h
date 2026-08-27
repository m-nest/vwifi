#ifndef _WIRELESSDEVICE_H
#define _WIRELESSDEVICE_H

#include <net/ethernet.h>

#include <string>
#include <iostream>

class WirelessDevice {

public:
    static constexpr uint32_t INVALID_WIPHY = UINT32_MAX;

	std::string _name;
	int _index;
	int _iftype;
	uint32_t _wiphy_id = INVALID_WIPHY;
	//unsigned char _macaddr[ETH_ALEN];
	int _txpower ;
	struct ether_addr _macaddr ;
	struct ether_addr _machwsim = {0x00,0X00,0x00,0X00,0x00,0X00};

	public:

	WirelessDevice();
	~WirelessDevice();

	/**
	 * \fn WirelessDevice(std::string n,int i,int t,const struct ether_addr & m,const struct ether_addr & h,int p,uint32_t w);
	 * \biref Constructor
	 * \param n -  interface name
	 * 	  i -  index
	 * 	  t -  type
	 * 	  m -  wireless net device mac address
	 * 	  h -  wireless net device mac address in hwsim driver
	 * 	  p -  transmission power
	 * 	  w -  wiphy ID
	 */
	WirelessDevice(const std::string &,int,int,const struct ether_addr &,const struct ether_addr &,int,uint32_t);

	/**
	 * \fn WirelessDevice(std::string n,int i,int t,const struct ether_addr & m);
	 * \biref Constructor
	 * \param n -  interface name
	 * 	  i -  index
	 * 	  t -  type
	 * 	  m -  wireless net device mac address
	 * 	  p -  transmission power
	 * 	  w -  wiphy ID
	 */
	WirelessDevice(const std::string &,int,int,const struct ether_addr &,int,uint32_t);

	friend std::ostream &   operator<< ( std::ostream & , const WirelessDevice &);

	struct ether_addr getMacaddr() const  ;
	struct ether_addr getMachwsim() const  ;

	void setMachwsim(const struct  ether_addr &);

	std::string getName() const ;

	bool checkif_wireless_device();
	int getIndex() const ;
	uint32_t getWiphyId() const;
	int getTxPower() const ;

};

#endif
