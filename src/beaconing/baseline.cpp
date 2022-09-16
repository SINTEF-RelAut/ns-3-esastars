/**
 * @file baseline.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see baseline.h
 */
#include <omp.h>

#include "ns3/point-to-point-channel.h"

#include "src/SCION/headers/beaconing/baseline.h"
#include "src/SCION/headers/utils.h"

namespace ns3 {

void
Baseline::DoInitializations (uint32_t numASes, rapidxml::xml_node<> *xmlNode,
                             const YAML::Node &config)
{
  BeaconServer::DoInitializations (numASes, xmlNode, config);
}

void
Baseline::CreateInitialStaticInfoExtension (StaticInfoExtension_t &staticInfoExtension,
                                                uint16_t selfEgressIfNo,
                                                const OptimizationTarget *optimizationTarget)
{
  staticInfoExtension.insert (std::make_pair (StaticInfoType::latency, 0));
  staticInfoExtension.insert (
      std::make_pair (StaticInfoType::bw, as->interAsBwds.at (selfEgressIfNo)));
}

void
Baseline::DisseminateBeacons (NeighbourRelation relation)
{
  uint32_t neighborsCnt = as->neighbors.size ();
  omp_set_num_threads (g_numCore);
#pragma omp parallel for
  for (uint32_t i = 0; i < neighborsCnt; ++i)
    {
      if (as->neighbors.at (i).second != relation)
        {
          continue;
        }

      uint16_t &remoteAsNo = as->neighbors.at (i).first;
      const std::vector<uint16_t> &interfaces = as->interfacesPerNeighborAs.at (remoteAsNo);

      for (auto const &dstAsBeaconsPair : beaconStore)
        {
          const uint16_t &dstAsNo = dstAsBeaconsPair.first;
          auto const &equalDstAsBeacons = dstAsBeaconsPair.second;

          uint32_t sentCount = 0;

          if (remoteAsNo == dstAsNo)
            {
              continue;
            }

          for (auto const &lenBeaconsPair : equalDstAsBeacons)
            { // for each length
              if (sentCount >= MAX_BEACONS_TO_SEND)
                {
                  break;
                }

              auto const &beacons = lenBeaconsPair.second;

              for (auto const &theBeacon : beacons)
                {
                  if (sentCount >= MAX_BEACONS_TO_SEND)
                    {
                      break;
                    }

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

                  sentCount++;

                  // Iterate over all the valid interfaces of this remote AS and send the beacons
                  for (auto const &egressInterfaceNo : interfaces)
                    {
                      std::pair<uint16_t, ScionAs *> remoteAsIfPair =
                          as->GetRemoteAsInfo (egressInterfaceNo);

                      uint16_t remoteIngressIfNo = remoteAsIfPair.first;
                      ScionAs *remoteAs = remoteAsIfPair.second;

                      Ld_t latency =
                          theBeacon->staticInfoExtension.at (StaticInfoType::latency) +
                                   as->latenciesBetweenInterfaces
                                       .at (LOWER_16_BITS (theBeacon->path.back ()))
                              .at (egressInterfaceNo);
                      Ld_t bwd = theBeacon->staticInfoExtension.at (StaticInfoType::bw) >
                                       (Ld_t) as->interAsBwds.at (egressInterfaceNo)
                                   ? (Ld_t) as->interAsBwds.at (egressInterfaceNo)
                                   : theBeacon->staticInfoExtension.at (StaticInfoType::bw);

                      StaticInfoExtension_t staticInfoExtension;
                      staticInfoExtension.insert (
                          std::make_pair (StaticInfoType::latency, latency));
                      staticInfoExtension.insert (std::make_pair (StaticInfoType::bw, bwd));

                      GenerateBeaconAndSend (theBeacon, egressInterfaceNo, remoteIngressIfNo,
                                             remoteAs, staticInfoExtension);
                    }
                }
            }
        }
    }
}

std::tuple<bool, bool, bool, Beacon *, Ld_t>
Baseline::AlgSpecificImportPolicy (Beacon &theBeacon, uint16_t senderAs,
                                      uint16_t remoteEgressIfNo, uint16_t selfIngressIfNo,
                                      uint16_t now)
{
  uint16_t dstAs = UPPER_16_BITS (theBeacon.path.at (0));

  if (nextRoundValidBeaconsCountPerDstAs.find (dstAs) == nextRoundValidBeaconsCountPerDstAs.end ())
    {
      return std::tuple<bool, bool, bool, Beacon *, Ld_t> (true, false, false, NULL, 0);
    }

  if (this->nextRoundValidBeaconsCountPerDstAs.at (dstAs) < MAX_BEACONS_TO_STORE)
    {
      return std::tuple<bool, bool, bool, Beacon *, Ld_t> (true, false, false, NULL, 0);
    }

  return std::tuple<bool, bool, bool, Beacon *, Ld_t> (false, false, false, NULL, 0);
}

void
Baseline::InsertToAlgorithmDataStructures (Beacon *theBeacon, uint16_t senderAs,
                                               uint16_t remoteEgressIfNo,
                                               uint16_t selfIngressIfNo)
{
}

void
Baseline::DeleteFromAlgorithmDataStructures (Beacon *theBeacon, Ld_t replacementKey)
{
}

void
Baseline::UpdateAlgorithmDataStructuresPeriodic (Beacon *theBeacon, bool invalidated)
{
}

} // namespace ns3