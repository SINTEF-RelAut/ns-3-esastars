/**
 * @file latency_optimized_beaconing.cpp
 * @authors Seyedali Tabaeiaghdaei
 * @date 2021
 * @see latency_optimized_beaconing.h
 */

#include "src/SCION/headers/beaconing/latency_optimized_beaconing.h"
#include "src/SCION/headers/utils.h"
#include <cassert>
#include <omp.h>

namespace ns3 {

void
LatencyOptimized::DoInitializations (uint32_t numASes, rapidxml::xml_node<> *xmlNode,
                                     const YAML::Node &config)
{
  BeaconServer::DoInitializations (numASes, xmlNode, config);

  beaconsPerDstPerIngIfSortedByLatency.resize (numASes);

  for (uint32_t i = 0; i < numASes; ++i)
    {
      beaconsPerDstPerIngIfSortedByLatency.at (i) =
          std::vector<std::multimap<Ld_t, Beacon *>> ();
      beaconsPerDstPerIngIfSortedByLatency.at (i).resize (as->GetNDevices ());
      for (uint32_t j = 0; j < as->GetNDevices (); ++j)
        {
          beaconsPerDstPerIngIfSortedByLatency.at (i).at (j) =
              std::multimap<Ld_t, Beacon *> ();
        }
    }
}

void
LatencyOptimized::CreateInitialStaticInfoExtension (StaticInfoExtension_t &staticInfoExtension, uint16_t selfEgressIfNo,
    const OptimizationTarget *optimizationTarget)
{
  staticInfoExtension.insert (std::make_pair (StaticInfoType::latency, 0));
}

std::tuple<bool, bool, bool, Beacon *, Ld_t>
LatencyOptimized::AlgSpecificImportPolicy (Beacon &theBeacon, uint16_t senderAs,
                                              uint16_t remoteEgressIfNo,
                                              uint16_t selfIngressIfNo, uint16_t now)
{
  uint16_t dstAs = UPPER_16_BITS (theBeacon.path.at (0));

  if (beaconsPerDstPerIngIfSortedByLatency.at (dstAs).at (selfIngressIfNo).size () <
      MAX_BEACONS_TO_STORE_PER_IFACE)
    {
      return std::tuple<bool, bool, bool, Beacon *, Ld_t> (true, false, false, NULL, 0);
    }

  Ld_t latency = theBeacon.staticInfoExtension.at (StaticInfoType::latency);

  std::multimap<Ld_t, Beacon *>::reverse_iterator highestPreviousLatencyIterator =
      beaconsPerDstPerIngIfSortedByLatency.at (dstAs).at (selfIngressIfNo).rbegin ();
  Ld_t highestPreviousLatency = highestPreviousLatencyIterator->first;
  if (highestPreviousLatency > latency)
    {
      Beacon *toBeRemovedBeacon = highestPreviousLatencyIterator->second;
      return std::tuple<bool, bool, bool, Beacon *, Ld_t> (true, false, false, toBeRemovedBeacon,
                                                         highestPreviousLatency);
    }
  return std::tuple<bool, bool, bool, Beacon *, Ld_t> (false, false, false, NULL, 0);
}

void
LatencyOptimized::DeleteFromAlgorithmDataStructures (Beacon *theBeacon, Ld_t replacementKey)
{
  uint16_t dstAs = UPPER_16_BITS (theBeacon->path.at (0));
  uint16_t selfIngressIf = LOWER_16_BITS (theBeacon->path.back ());
  auto &beaconContainer = beaconsPerDstPerIngIfSortedByLatency.at (dstAs).at (selfIngressIf);
  for (auto it = beaconContainer.lower_bound (replacementKey);
       it != beaconContainer.upper_bound (replacementKey); ++it)
    {
      if (it->second == theBeacon)
        {
          beaconContainer.erase (it--);
          break;
        }
    }
}

void
LatencyOptimized::InsertToAlgorithmDataStructures (Beacon *theBeacon, uint16_t senderAs,
                                                       uint16_t remoteEgressIfNo,
                                                       uint16_t selfIngressIfNo)
{
  uint16_t dstAs = UPPER_16_BITS (theBeacon->path.at (0));
  uint16_t selfIngressIf = LOWER_16_BITS (theBeacon->path.back ());
  Ld_t latency = theBeacon->staticInfoExtension.at (StaticInfoType::latency);
  beaconsPerDstPerIngIfSortedByLatency.at (dstAs)
      .at (selfIngressIf)
      .insert (std::make_pair (latency, theBeacon));
}

void
LatencyOptimized::DisseminateBeacons (NeighbourRelation relation)
{
  uint32_t neighborsCnt = as->neighbors.size ();
  omp_set_num_threads (g_numCore);
#pragma omp parallel for
  for (uint32_t i = 0; i < neighborsCnt; ++i)
    { // Per neighbor AS

      if (as->neighbors.at (i).second != relation)
        {
          continue;
        }

      uint16_t remoteAsNo = as->neighbors.at (i).first;
      for (auto const &dstAsBeaconsPair : beaconStore)
        { // Per destination AS
          uint16_t dstAsNo = dstAsBeaconsPair.first;
          const BeaconsWithSameDstAs_t &beaconsToTheDstAs = dstAsBeaconsPair.second;

          if (remoteAsNo == dstAsNo)
            {
              continue;
            }

          std::multimap<Ld_t, std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *, StaticInfoExtension_t>>
              selectedBeacons = SelectBeaconsToDisseminatePerDstPerNbr (remoteAsNo, dstAsNo, beaconsToTheDstAs);

          for (auto const &theTuplePair : selectedBeacons)
            {
              Beacon *theBeacon;
              uint16_t remoteIngressIfNo;
              uint16_t selfEgressIfNo;
              ScionAs *remoteAs;
              StaticInfoExtension_t staticInfoExtension;

              std::tie (theBeacon, selfEgressIfNo, remoteIngressIfNo, remoteAs,
                        staticInfoExtension) = theTuplePair.second;

              GenerateBeaconAndSend (theBeacon, selfEgressIfNo, remoteIngressIfNo, remoteAs,
                                     staticInfoExtension);
            }
        }
    }
}

std::multimap<Ld_t, std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *, StaticInfoExtension_t>>
LatencyOptimized::SelectBeaconsToDisseminatePerDstPerNbr (
    uint16_t remoteAsNo, uint16_t dstAsNo,
    const BeaconsWithSameDstAs_t &beaconsToTheDstAs)
{
  std::multimap<Ld_t, std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *, StaticInfoExtension_t>>
      latencyMapToBeaconAndMetadata;
  std::map<uint16_t, std::multimap<Ld_t, Beacon *>> validCandidates;

  int beaconCnt = 0;
  for (auto const &lenBeaconsPair : beaconsToTheDstAs)
    {
      auto const &beacons = lenBeaconsPair.second;
      for (auto const &theBeacon : beacons)
        {
          if (!theBeacon->isValid)
            {
              continue;
            }

          bool generatesLoop = false;
          for (auto const &linkInfo : theBeacon->path)
            { // remove loops
              if (UPPER_16_BITS (linkInfo) == remoteAsNo)
                {
                  generatesLoop = true;
                  break;
                }
            }

          if (generatesLoop)
            {
              continue;
            }

          auto const &interfaces = as->interfacesPerNeighborAs.at (remoteAsNo);
          for (auto const &selfEgressIfNo : interfaces)
            {
              Ld_t latency =
                  theBeacon->staticInfoExtension.at (StaticInfoType::latency) +
                  as->latenciesBetweenInterfaces.at (LOWER_16_BITS (theBeacon->path.back ()))
                      .at (selfEgressIfNo);

              if (beaconCnt == 0)
                {
                  validCandidates.insert (
                      std::make_pair (selfEgressIfNo, std::multimap<Ld_t, Beacon *> ()));
                }

              validCandidates.at (selfEgressIfNo).insert (std::make_pair (latency, theBeacon));
            }
          beaconCnt++;
        }
    }

  for (auto const &ifaceToLatencyBeaconPair : validCandidates)
    {
      uint16_t selfEgressIfNo = ifaceToLatencyBeaconPair.first;

      int noBeaconsPerIface = 0;
      for (auto const &latencyBeaconPair : ifaceToLatencyBeaconPair.second)
        {
          if (noBeaconsPerIface >= MAX_BEACONS_TO_SEND_PER_IFACE)
            {
              break;
            }
          noBeaconsPerIface++;

          Ld_t latency = latencyBeaconPair.first;
          Beacon *theBeacon = latencyBeaconPair.second;

          uint16_t remoteIngressIfNo = as->GetRemoteAsInfo (selfEgressIfNo).first;
          ScionAs *remoteAs = as->GetRemoteAsInfo (selfEgressIfNo).second;

          StaticInfoExtension_t staticInfoExtension;
          staticInfoExtension.insert (std::make_pair (StaticInfoType::latency, latency));

          latencyMapToBeaconAndMetadata.insert (std::make_pair (
              latency,
              std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *, StaticInfoExtension_t> (
                           theBeacon, selfEgressIfNo, remoteIngressIfNo, remoteAs, staticInfoExtension)));
        }
    }
  return latencyMapToBeaconAndMetadata;
}

void
LatencyOptimized::UpdateAlgorithmDataStructuresPeriodic (Beacon *theBeacon, bool invalidated)
{
  if (invalidated)
    {
      DeleteFromAlgorithmDataStructures (
          theBeacon, theBeacon->staticInfoExtension.at (StaticInfoType::latency));
    }
}

} // namespace ns3