//
// Created by seyedali on 19.07.21.
//

#ifndef SCION_SIMULATOR_SCION_HOST_H
#define SCION_SIMULATOR_SCION_HOST_H

#include <unordered_map>

#include "ns3/nstime.h"

#include "src/SCION/headers/path_segment.h"
#include "src/SCION/headers/scion_capable_node.h"
#include "src/SCION/headers/scion_packet.h"

namespace ns3 {
class ScionHost : public ScionCapableNode
{
public:
  ScionHost (uint32_t systemId, uint16_t isdNumber, uint16_t asNumber, HostAddr_t localAddress,
             double latitude, double longitude, ScionAs *as)
      : ScionCapableNode (systemId, isdNumber, asNumber, localAddress, latitude, longitude, as)
  {
  }

  void SendArbitraryPacket (Ia_t dstIa, HostAddr_t dstHost);

protected:
  CachedPathSegsDataset_t cachedUpPathSegments;
  CachedPathSegsDataset_t cachedCorePathSegments;
  CachedPathSegsDataset_t cachedDownPathSegments;

  virtual void ProcessReceivedPacket (uint16_t localIf, ScionPacket *packet,
                                        Time receiveTime) override;
  virtual void ModifyPktUponSend (ScionPacket *packet) override;
  void RemoveExpiredSegments ();
  void SearchInCachedSegments (Ia_t dstIa, std::vector<const PathSegment *> &path,
                                  std::vector<uint8_t> &shortcuts);
  void RequestForPathSegments (Ia_t dstIa);
  void SendRequestForPathSegments (PathSegmentType segType, Ia_t srcIa, Ia_t dstIa);

  void ReceiveRegisteredPathSegments (PathSegmentType segType, Ia_t srcIa, Ia_t dstIa,
                                         const RegPathSegsToOneAs_t *pathSegments);
  void ReceiveCachedPathSegments (PathSegmentType segType, Ia_t srcIa, Ia_t dstIa,
                                  CachedPathSegsPerDst_t *pathSeg);

  void CachePathSegment (PathSegmentType segType, Ia_t srcIa, Ia_t dstIa,
                           PathSegment *pathSeg);
};
} // namespace ns3

#endif //SCION_SIMULATOR_SCION_HOST_H
