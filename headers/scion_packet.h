//
// Created by seyedali on 19.07.21.
//

#ifndef SCION_SIMULATOR_SCION_PACKET_H
#define SCION_SIMULATOR_SCION_PACKET_H

#include <ns3/node.h>
#include <unordered_set>

#include "ns3/nstime.h"
#include "ns3/object.h"

#include "src/SCION/headers/path_segment.h"

namespace ns3 {
typedef uint16_t HostAddr_t;
typedef uint32_t PacketId_t;

class ScionCapableNode;

enum PayloadType {
  empty = 0,
  pathReqFromHost = 1,
  regPathsFromLocalPs = 2,
  regPathsFromRemotePs = 3,
  reqForListOfAllCoreAses = 4,
  listOfAllCoreAses = 5,
  broadcastListOfAllCoreAses = 6,
  ntpReq = 7,
  ntpResp = 8
};

struct PathReqFromHost
{
  Ia_t srcIa, dstIa;
  PathSegmentType segType;
};

struct RegPathsFromLocalPs
{
  const RegPathSegsToOneAs_t *registeredPathSegments;
  Ia_t srcIa, dstIa;
  PathSegmentType segType;
};

struct ListOfAllASes
{
  std::set<Ia_t> *setOfAllAses;
};

struct NtpReqOrResp
{
  int64_t t0, t1, t2, t3;
};

union Payload {
  pathReqFromHost pathReqFromHost;
  regPathsFromLocalPs registeredPathsFromLocalPs;
  ListOfAllASes listOfAllAses;
  NtpReqOrResp ntpReqOrResp;
};

struct ScionPacket
{
public:
  Time timestamp;

  ScionCapableNode *const packetOriginator; // This field is used for memory management of packets

  std::vector<const PathSegment *> path;

  Payload payload;

  const PacketId_t id; // This field is used for memory management of packets

  Ia_t srcIa, dstIa;

  PayloadType payloadType;

  HostAddr_t srcHost, dstHost;

  uint16_t currInf, curHopf;

  uint16_t size; // size in bytes

  // The index of cross overs in each segment
  std::vector<uint8_t> shortcutHopfs;

  bool pathReversed;

  ScionPacket (ScionCapableNode *const packetOriginator, PacketId_t id)
      : packetOriginator (packetOriginator), id (id)
  {
  }
};

} // namespace ns3
#endif //SCION_SIMULATOR_SCION_PACKET_H
