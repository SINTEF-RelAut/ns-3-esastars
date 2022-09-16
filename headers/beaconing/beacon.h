/**
 * @file beacon.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 */

#ifndef SCION_SIMULATOR_BEACON_H
#define SCION_SIMULATOR_BEACON_H

#include <set>
#include <string>
#include <utility>
#include <vector>
#include <unordered_set>

#include "src/SCION/headers/path_segment.h"

namespace ns3 {

#define BEACON_HEADER_SIZE 86
#define BEACON_HOP_SIZE 132
#define ORIGINATOR(beacon) (UPPER_16_BITS (beacon.path.front ()))
#define ORIGINATOR_PTR(beacon) (UPPER_16_BITS (beacon->path.front ()))
#define DST_AS(beacon)                                       \
  (beacon.beaconDirection == BeaconDirection::pullBased ? beacon.optimizationTarget->targetAs                \
       : UPPER_16_BITS (beacon.path.front ()))

#define DST_AS_PTR(beacon)                                    \
  (beacon->beaconDirection == BeaconDirection::pullBased ? beacon->optimizationTarget->targetAs                \
       : UPPER_16_BITS (beacon->path.front ()))

typedef long double Ld_t;

typedef uint64_t LinkInformation_t; // sender_as   eg_if   receiver_as   ing_if
    // <-16bit-> <-16bit-> <--16bit-->  <-16bit->

typedef std::vector<LinkInformation_t> Path_t;
typedef std::vector<uint16_t> IsdPath_t;

enum StaticInfoType { latency = 0, bw = 1, co2 = 2, forbiddenEdges = 3 };
typedef std::map<StaticInfoType, float> StaticInfoExtension_t;

typedef std::map<StaticInfoType, float> OptimizationCriteria_t;
enum OptimizationDirection { forward = 0, backward = 1, symmetric = 2 };
typedef uint16_t TargetAs_t;
typedef uint16_t TargetId_t;
typedef uint16_t TargetIfGroup_t;

struct OptimizationTarget
{
  const TargetId_t targetId;
  const OptimizationCriteria_t criteria;
  const OptimizationDirection direction;
  const TargetAs_t targetAs;
  const TargetIfGroup_t targetIfGroup;
  const uint16_t noBeaconsPerOptimizationTarget;
  const std::unordered_map<uint16_t, std::unordered_set<uint16_t> *> *const setOfForbiddenEdges;

  OptimizationTarget (
      TargetId_t targetId, OptimizationCriteria_t criteria, OptimizationDirection direction,
      TargetAs_t targetAs, TargetIfGroup_t targetIfGroup,
      uint16_t noBeaconsPerOptimizationTarget,
      const std::unordered_map<uint16_t, std::unordered_set<uint16_t> *> *setOfForbiddenEdges)
      : targetId (targetId),
        criteria (std::move (criteria)),
        direction (direction),
        targetAs (targetAs),
        targetIfGroup (targetIfGroup),
        noBeaconsPerOptimizationTarget (noBeaconsPerOptimizationTarget),
        setOfForbiddenEdges (setOfForbiddenEdges)
  {
  }
};

enum BeaconDirection { pushBased = 0, pullBased = 1 };

struct Beacon
{
  StaticInfoExtension_t staticInfoExtension;

  const OptimizationTarget *optimizationTarget;

  BeaconDirection beaconDirection;

  uint16_t initiationTime;
  uint16_t expirationTime;

  uint16_t nextInitiationTime;
  uint16_t nextExpirationTime;

  bool isNew;
  bool isValid;

  Path_t path;

  std::string key;

  IsdPath_t isdPath;

  Beacon (StaticInfoExtension_t &staticInfoExtension,
          const OptimizationTarget *optimizationTarget,
          BeaconDirection beaconDirection,
          uint16_t i, uint16_t e, uint16_t nxtI, uint16_t nxtE, bool n, bool v, Path_t &p,
          std::string &k, IsdPath_t &isdp)
      : staticInfoExtension (staticInfoExtension),
        optimizationTarget (optimizationTarget),
        beaconDirection (beaconDirection),
        initiationTime (i),
        expirationTime (e),
        nextInitiationTime (nxtI),
        nextExpirationTime (nxtE),
        isNew (n),
        isValid (v),
        path (p),
        key (k),
        isdPath (isdp)

  {
  }

  void ExtractPathSegmentFromPushBasedBeacon (PathSegment &pathSegment) const;

  void ExtractPathSegmentFromPullBasedBeacon (PathSegment &pathSegment) const;
};
} // namespace ns3
#endif //SCION_SIMULATOR_BEACON_H
