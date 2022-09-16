/**
 * @file scionlab_algo.cpp
 * @authors Seyedali Tabaeiaghdaei
 * @date 2021
 * @see scionlab_algo.h
 */

#include "src/SCION/headers/beaconing/scionlab_algo.h"
#include "ns3/point-to-point-channel.h"
#include "src/SCION/headers/utils.h"
#include <omp.h>

namespace ns3 {

void
Scionlab::DoInitializations (uint32_t numASes, rapidxml::xml_node<> *xmlNode,
                             const YAML::Node &config)
{
  BeaconServer::DoInitializations (numASes, xmlNode, config);
}

void
Scionlab::CreateInitialStaticInfoExtension (StaticInfoExtension_t &staticInfoExtension,
                                                uint16_t selfEgressIfNo,
                                                const OptimizationTarget *optimizationTarget)
{
  staticInfoExtension.insert (std::make_pair (StaticInfoType::latency, 0));
  staticInfoExtension.insert (
      std::make_pair (StaticInfoType::bw, as->interAsBwds.at (selfEgressIfNo)));
}

void
Scionlab::DisseminateBeacons (NeighbourRelation relation)
{
  uint32_t neighborsCnt = as->neighbors.size ();
  std::vector<Beacon *> validCandidates;

  for (auto const &dstAsBeaconsPair : beaconStore)
    {
      auto const &equalDstAsBeacons = dstAsBeaconsPair.second;

      std::vector<Beacon *> restOfBeacons;
      std::vector<Beacon *> selectedBeaconsPerDst;

      for (auto const &lenBeaconsPair : equalDstAsBeacons)
        { // for each length
          if (restOfBeacons.size () + selectedBeaconsPerDst.size () >= MAX_SET_SIZE)
            {
              break;
            }

          auto const &beacons = lenBeaconsPair.second;
          for (auto const &theBeacon : beacons)
            {
              if (restOfBeacons.size () + selectedBeaconsPerDst.size () >= MAX_SET_SIZE)
                {
                  break;
                }

              if (!theBeacon->isValid)
                {
                  continue;
                }

              if (selectedBeaconsPerDst.size () < MAX_BEACONS_TO_SEND - 1)
                {
                  validCandidates.push_back (theBeacon);
                  selectedBeaconsPerDst.push_back (theBeacon);
                }
              else
                {
                  restOfBeacons.push_back (theBeacon);
                }
            }
        }

      if (restOfBeacons.size () + selectedBeaconsPerDst.size () == MAX_BEACONS_TO_SEND)
        {
          validCandidates.push_back (restOfBeacons.at (0));
          continue;
        }

      if (restOfBeacons.size () + selectedBeaconsPerDst.size () < MAX_BEACONS_TO_SEND)
        {
          continue;
        }

      std::pair<Beacon *, int32_t> diversityWrToSelected =
          SelectMostDiverse (selectedBeaconsPerDst, selectedBeaconsPerDst.at (0));
      std::pair<Beacon *, int32_t> diversityWrToRest =
          SelectMostDiverse (restOfBeacons, selectedBeaconsPerDst.at (0));

      if (diversityWrToRest.second > diversityWrToSelected.second)
        {
          validCandidates.push_back (diversityWrToRest.first);
        }
      else
        {
          validCandidates.push_back (restOfBeacons.at (0));
        }
    }

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

      for (auto const &theBeacon : validCandidates)
        {
          uint16_t dstAsNo = UPPER_16_BITS (theBeacon->path.at (0));

          if (remoteAsNo == dstAsNo)
            {
              continue;
            }

          bool generatesLoop = false;

          for (auto const &linkInfo : theBeacon->path)
            { // filter AS loops
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

          // filter isd loops
          uint16_t remoteIsdNo = as->GetRemoteAsInfo (interfaces.back ()).second->isdNumber;
          if (remoteIsdNo != as->isdNumber)
            {
              for (uint16_t isdNumber : theBeacon->isdPath)
                {
                  if (isdNumber == remoteIsdNo)
                    {
                      generatesLoop = true;
                      break;
                    }
                }
            }

          if (generatesLoop)
            {
              continue;
            }

          // Iterate over all the valid interfaces of this remote AS and send the beacons
          for (auto const &egressInterfaceNo : interfaces)
            {
              std::pair<uint16_t, ScionAs *> remoteAsIfPair =
                  as->GetRemoteAsInfo (egressInterfaceNo);

              uint16_t remoteIngressIfNo = remoteAsIfPair.first;
              ScionAs *remoteAs = remoteAsIfPair.second;

              Ld_t latency =
                  theBeacon->staticInfoExtension.at (StaticInfoType::latency) +
                  as->latenciesBetweenInterfaces.at (LOWER_16_BITS (theBeacon->path.back ()))
                      .at (egressInterfaceNo);
              Ld_t bwd = theBeacon->staticInfoExtension.at (StaticInfoType::bw) >
                               (Ld_t) as->interAsBwds.at (egressInterfaceNo)
                           ? (Ld_t) as->interAsBwds.at (egressInterfaceNo)
                           : theBeacon->staticInfoExtension.at (StaticInfoType::bw);

              StaticInfoExtension_t staticInfoExtension;
              staticInfoExtension.insert (std::make_pair (StaticInfoType::latency, latency));
              staticInfoExtension.insert (std::make_pair (StaticInfoType::bw, bwd));

              GenerateBeaconAndSend (theBeacon, egressInterfaceNo, remoteIngressIfNo, remoteAs,
                                     staticInfoExtension);
            }
        }
    }
}

std::tuple<bool, bool, bool, Beacon *, Ld_t>
Scionlab::AlgSpecificImportPolicy (Beacon &theBeacon, uint16_t senderAs,
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
Scionlab::InsertToAlgorithmDataStructures (Beacon *theBeacon, uint16_t senderAs,
                                               uint16_t remoteEgressIfNo,
                                               uint16_t selfIngressIfNo)
{
}

void
Scionlab::DeleteFromAlgorithmDataStructures (Beacon *theBeacon, Ld_t replacementKey)
{
}

void
Scionlab::UpdateAlgorithmDataStructuresPeriodic (Beacon *theBeacon, bool invalidated)
{
}

std::pair<Beacon *, int32_t>
Scionlab::SelectMostDiverse (std::vector<Beacon *> &beacons, Beacon *theBeacon)
{
  if (beacons.size () == 0)
    {
      return std::make_pair (theBeacon, -1);
    }
  Beacon *diverse = NULL;
  int32_t maxDiversity = -1;
  uint32_t minLen = std::numeric_limits<uint32_t>::max ();

  for (auto const &otherBeacon : beacons)
    {
      int32_t diversity = CalcDiversity (theBeacon, otherBeacon);
      uint32_t l = otherBeacon->path.size ();

      if (diversity > maxDiversity || (diversity == maxDiversity && minLen > l))
        {
          diverse = otherBeacon;
          minLen = l;
          maxDiversity = diversity;
        }
    }
  return std::make_pair (diverse, maxDiversity);
}

int32_t
Scionlab::CalcDiversity (Beacon *beacon1, Beacon *beacon2)
{
  int32_t diff = 0;

  for (uint64_t linkInfo : beacon1->path)
    {
      bool found = false;
      for (uint64_t otherLinkInfo : beacon2->path)
        {
          if (linkInfo == otherLinkInfo)
            {
              found = true;
              break;
            }
        }
      if (!found)
        {
          diff++;
        }
    }
  return diff;
}

} // namespace ns3
