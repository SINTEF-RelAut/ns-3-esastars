//
// Created by seyedali on 19.07.21.
//

#ifndef SCION_SIMULATOR_PATH_SERVER_H
#define SCION_SIMULATOR_PATH_SERVER_H

#include <map>
#include <unordered_map>
#include <vector>

#include "ns3/nstime.h"

#include "src/SCION/headers/path_segment.h"
#include "src/SCION/headers/scion_capable_node.h"
#include "src/SCION/headers/scion_packet.h"

namespace ns3 {
class PathServer : public ScionCapableNode
{
public:
  PathServer (uint32_t systemId, uint16_t isdNumber, uint16_t asNumber, HostAddr_t localAddress, double latitude, double longitude, ScionAs *as)
      : ScionCapableNode (systemId, isdNumber, asNumber, localAddress, latitude, longitude, as)
  {
    setOfAllCoreAses.insert (iaAddr);
  }

  void RegisterCorePathSegment (PathSegment &pathSegment, std::string key);
  void RegisterUpPathSegment (PathSegment &pathSegment, std::string key);
  void RegisterDownPathSegment (PathSegment &pathSegment, std::string key);

private:
  std::set<Ia_t> setOfAllCoreAses;

  RegisteredPathSegsDataset_t registeredCoreSegments;
  RegisteredPathSegsDataset_t registeredUpSegments;
  RegisteredPathSegsDataset_t registeredDownSegments;

  CachedPathSegsDataset_t cachedCoreSegments;
  CachedPathSegsDataset_t cachedUpSegments;
  CachedPathSegsDataset_t cachedDownSegments;

  void ProcessReceivedPacket (uint16_t localIf, ScionPacket *packet, Time receiveTime) override;

  void ProcessLocalHostRequestForPath (PathSegmentType pathType, Ia_t srcIa, Ia_t dstIa,
                                       HostAddr_t hostAddr);

  void SendRegisteredPathToLocalHost (HostAddr_t hostAddr, PathSegmentType pathType, Ia_t srcIa,
                                      Ia_t dstIa,
                                           const RegPathSegsToOneAs_t *);

  void ReturnListOfAllCoreAses (HostAddr_t hostAddr);
};
} // namespace ns3

#endif //SCION_SIMULATOR_PATH_SERVER_H
