//
// Created by Seyedali Tabaeiaghdaei on 20.04.22.
//

#ifndef SCION_SIMULATOR_SCION_AS_H
#define SCION_SIMULATOR_SCION_AS_H
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ns3/map-scheduler.h"
#include "ns3/network-module.h"
#include "ns3/node.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-net-device.h"

#include "src/SCION/headers/beaconing/beacon.h"
#include "src/SCION/headers/border_router.h"
#include "src/SCION/headers/path_server.h"
#include "src/SCION/headers/scion_host.h"
#include "src/SCION/headers/scion_packet.h"
#include "src/SCION/headers/user_defined_events.h"
#include "src/SCION/headers/utils.h"

namespace ns3 {

enum NeighbourRelation { core = 0, peer = 1, customer = 2, provider = 3 };

class BeaconServer;

class ScionAs : public Node
{
public:
  ScionAs (uint32_t systemId, bool parallelScheduler, uint16_t asNumber,
            rapidxml::xml_node<> *xmlNode, const YAML::Node &config, bool maliciousBorderRouters,
            Time localTime)
      : Node (systemId)
  {
    PropertyContainer p = ParseProperties (xmlNode);

    if (p.HasProperty ("isd"))
      {
        isdNumber = std::stoi (p.GetProperty ("isd"));
      }
    else
      {
        isdNumber = 0;
      }

    this->asNumber = asNumber;
    iaAddr = (((uint32_t) isdNumber) << 16) | ((uint32_t) asNumber);

    this->localTime = localTime;

    this->maliciousBorderRouters = maliciousBorderRouters;

    if (maliciousBorderRouters)
      {
        borderRoutersMaliciousAction =
            config["border_router"]["malicious_action"].as<std::string> ();
      }
    else
      {
        borderRoutersMaliciousAction = "no";
      }

    InstantiateBeaconServer (parallelScheduler, xmlNode, config);
  }

  virtual ~ScionAs ()
  {
  }

  uint16_t isdNumber;
  uint16_t asNumber;
  Ia_t iaAddr;

  Time localTime;
  int32_t asMaxBwd;

  std::vector<Time> latenciesBetweenHostsAndPathServer;
  std::vector<Time> latenciesBetweenInterfacesAndBeaconServer;
  Time latencyBetweenPathServerAndBeaconServer;

  std::vector<std::pair<uint16_t, NeighbourRelation>> neighbors;
  std::unordered_map<uint16_t, std::vector<uint16_t>> interfacesPerNeighborAs;
  std::unordered_map<uint16_t, uint16_t> interfaceToNeighborMap;
  std::vector<std::pair<Ld_t, Ld_t>> interfacesCoordinates;
  std::multimap<std::pair<Ld_t, Ld_t>, uint16_t> coordinatesToInterfaces;
  std::vector<std::vector<Ld_t>> latenciesBetweenInterfaces;
  std::vector<int32_t> interAsBwds;

  void DoInitializations (uint32_t numASes, rapidxml::xml_node<> *xmlNode,
                          const YAML::Node &config, bool onlyPropagationDelay);

  void DoInitializations (uint32_t numASes, rapidxml::xml_node<> *xmlNode,
                          const YAML::Node &config);

  std::pair<uint16_t, ScionAs *> GetRemoteAsInfo (uint16_t egressInterfaceNo);

  void ReceiveBeacon (Beacon &theBeacon, uint16_t senderAs, uint16_t remoteIf,
                      uint16_t localIf);

  void SetBeaconServer (BeaconServer *theBeaconServer);

  void SetPathServer (PathServer *thePathServer);

  BeaconServer *GetBeaconServer ();

  PathServer *GetPathServer ();

  ScionCapableNode *GetHost (HostAddr_t hostAddr);

  uint32_t GetNHosts ();

  void AdvanceTime (ns3::Time advance);

  void AddHost (ScionHost *host);

  BorderRouter *AddBr (double latitude, double longitude, Time processingDelay,
                       Time processingThroughputDelay);

  void AddToRemoteAsInfo (uint16_t remoteIf, ScionAs *remoteAs);

  friend class UserDefinedEvents;

protected:
  bool maliciousBorderRouters;
  std::string borderRoutersMaliciousAction;

  BeaconServer *beaconServer;
  PathServer *pathServer = NULL;
  std::vector<ScionHost *> hosts;
  std::vector<BorderRouter *> borderRouters;

  std::vector<std::pair<uint16_t, ScionAs *>> remoteAsInfo;

  void ConnectInternalNodes (bool onlyPropagationDelay);
  void InitializeLatencies (bool onlyPropagationDelay);

  void InstantiateBeaconServer (bool parallelScheduler, rapidxml::xml_node<> *xmlNode,
                                  const YAML::Node &config);
};
} // namespace ns3
#endif //SCION_SIMULATOR_SCION_AS_H
