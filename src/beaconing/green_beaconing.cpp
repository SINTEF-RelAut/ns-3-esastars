/**
 * @file green_beaconing.cpp
 * @authors Seyedali Tabaeiaghdaei
 * @date 2021
 * @see green_beaconing.h
 */

#include <omp.h>

#include "ns3/point-to-point-channel.h"

#include "src/SCION/headers/beaconing/green_beaconing.h"
#include "src/SCION/headers/utils.h"

namespace ns3 {

void
GreenBeaconing::DoInitializations (uint32_t numASes, rapidxml::xml_node<> *xmlNode,
                                   const YAML::Node &config)
{
  BeaconServer::DoInitializations (numASes, xmlNode, config);

  beaconsPerDstPerIngIfSortedByPollution.resize (numASes);

  for (uint32_t i = 0; i < numASes; ++i)
    {
      beaconsPerDstPerIngIfSortedByPollution.at (i) =
          std::vector<std::multimap<Ld_t, Beacon *>> ();
      beaconsPerDstPerIngIfSortedByPollution.at (i).resize (as->GetNDevices ());
      for (uint32_t j = 0; j < as->GetNDevices (); ++j)
        {
          beaconsPerDstPerIngIfSortedByPollution.at (i).at (j) =
              std::multimap<Ld_t, Beacon *> ();
        }
    }
}

const std::vector<std::vector<std::multimap<Ld_t, Beacon *>>> &
GreenBeaconing::GetBeaconsSortedByPollution () const
{
  return beaconsPerDstPerIngIfSortedByPollution;
}

void
GreenBeaconing::CreateInitialStaticInfoExtension (StaticInfoExtension_t &staticInfoExtension, uint16_t selfEgressIfNo,
    const OptimizationTarget *optimizationTarget)
{
  staticInfoExtension.insert (std::make_pair (StaticInfoType::latency, 0));
  staticInfoExtension.insert (std::make_pair (StaticInfoType::co2, 0));
}

std::tuple<bool, bool, bool, Beacon *, Ld_t>
GreenBeaconing::AlgSpecificImportPolicy (Beacon &theBeacon, uint16_t senderAs,
                                            uint16_t remoteEgressIfNo,
                                            uint16_t selfIngressIfNo, uint16_t now)
{
  uint16_t dstAs = UPPER_16_BITS (theBeacon.path.at (0));

  if (beaconsPerDstPerIngIfSortedByPollution.at (dstAs).at (selfIngressIfNo).size () <
      MAX_BEACONS_TO_STORE_PER_IFACE)
    {
      return std::tuple<bool, bool, bool, Beacon *, Ld_t> (true, false, false, NULL, 0);
    }

  Ld_t pollutionIndex = theBeacon.staticInfoExtension.at (StaticInfoType::co2);

  std::multimap<Ld_t, Beacon *>::reverse_iterator highestPreviousPollutionIterator =
      beaconsPerDstPerIngIfSortedByPollution.at (dstAs).at (selfIngressIfNo).rbegin ();
  Ld_t highestPreviousPollution = highestPreviousPollutionIterator->first;
  if (highestPreviousPollution > pollutionIndex)
    {
      Beacon *toBeRemovedBeacon = highestPreviousPollutionIterator->second;
      return std::tuple<bool, bool, bool, Beacon *, Ld_t> (true, false, false, toBeRemovedBeacon,
                                                         highestPreviousPollution);
    }
  return std::tuple<bool, bool, bool, Beacon *, Ld_t> (false, false, false, NULL, 0);
}

void
GreenBeaconing::DeleteFromAlgorithmDataStructures (Beacon *theBeacon, Ld_t replacementKey)
{
  uint16_t dstAs = UPPER_16_BITS (theBeacon->path.at (0));
  uint16_t selfIngressIf = LOWER_16_BITS (theBeacon->path.back ());
  auto &beaconContainer = beaconsPerDstPerIngIfSortedByPollution.at (dstAs).at (selfIngressIf);
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
GreenBeaconing::InsertToAlgorithmDataStructures (Beacon *theBeacon, uint16_t senderAs,
                                                     uint16_t remoteEgressIfNo,
                                                     uint16_t selfIngressIfNo)
{
  uint16_t dstAs = UPPER_16_BITS (theBeacon->path.at (0));
  uint16_t selfIngressIf = LOWER_16_BITS (theBeacon->path.back ());
  Ld_t pollutionIndex = theBeacon->staticInfoExtension.at (StaticInfoType::co2);
  beaconsPerDstPerIngIfSortedByPollution.at (dstAs)
      .at (selfIngressIf)
      .insert (std::make_pair (pollutionIndex, theBeacon));
}

void
GreenBeaconing::DisseminateBeacons (NeighbourRelation relation)
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
              selectedBeacons;
          SelectBeaconsToDisseminatePerDstPerNbr (remoteAsNo, dstAsNo, beaconsToTheDstAs,
                                                  selectedBeacons);

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

void
GreenBeaconing::SelectBeaconsToDisseminatePerDstPerNbr (
    uint16_t remoteAsNo, uint16_t dstAsNo,
    const BeaconsWithSameDstAs_t &beaconsToTheDstAs,
    std::multimap<Ld_t, std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *, StaticInfoExtension_t>>
        &pollutionIndexMapToBeaconAndMetadata)
{
  std::map<uint16_t, std::multimap<Ld_t, Beacon *>> validCandidates;

  auto const &interfaces = as->interfacesPerNeighborAs.at (remoteAsNo);
  for (auto const &selfEgressIfNo : interfaces)
    {
      validCandidates.insert (std::make_pair (selfEgressIfNo, std::multimap<Ld_t, Beacon *> ()));
    }

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
              Ld_t pollutionIndex = theBeacon->staticInfoExtension.at (StaticInfoType::co2) +
                  CalculatePollutionBetweenBorderRouters (
                      LOWER_16_BITS (theBeacon->path.back ()), selfEgressIfNo);
              validCandidates.at (selfEgressIfNo)
                  .insert (std::make_pair (pollutionIndex, theBeacon));
            }
        }
    }

  for (auto const &ifaceToPollutionBeaconPair : validCandidates)
    {
      uint16_t selfEgressIfNo = ifaceToPollutionBeaconPair.first;
      int noBeaconsPerIface = 0;
      for (auto const &pollutionBeaconPair : ifaceToPollutionBeaconPair.second)
        {
          if (noBeaconsPerIface >= MAX_BEACONS_TO_SEND_PER_IFACE)
            {
              break;
            }
          noBeaconsPerIface++;

          Ld_t pollutionIndex = pollutionBeaconPair.first;
          Beacon *theBeacon = pollutionBeaconPair.second;

          uint16_t remoteIngressIfNo = as->GetRemoteAsInfo (selfEgressIfNo).first;
          ScionAs *remoteAs = as->GetRemoteAsInfo (selfEgressIfNo).second;

          Ld_t latency =
              theBeacon->staticInfoExtension.at (StaticInfoType::latency) +
              as->latenciesBetweenInterfaces.at (LOWER_16_BITS (theBeacon->path.back ()))
                  .at (selfEgressIfNo);

          StaticInfoExtension_t staticInfoExtension;
          staticInfoExtension.insert (std::make_pair (StaticInfoType::latency, latency));
          staticInfoExtension.insert (std::make_pair (StaticInfoType::co2, pollutionIndex));

          pollutionIndexMapToBeaconAndMetadata.insert (std::make_pair (
              pollutionIndex,
              std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *, StaticInfoExtension_t> (
                  theBeacon, selfEgressIfNo, remoteIngressIfNo, remoteAs, staticInfoExtension)));
        }
    }
}

Ld_t
GreenBeaconing::CalculatePollutionBetweenBorderRouters (uint16_t ingressIf, uint16_t egressIf)
{
  Ld_t energyResourceCarbonIntensity = dirtyEnergyRatio * 700 / 3.6e6; // gr/Joule
  Ld_t pathEnergyIntensity = intraAsEnergies.at (ingressIf).at (egressIf);

  Ld_t pollution = pathEnergyIntensity * energyResourceCarbonIntensity;

  return pollution;
}

void
GreenBeaconing::UpdateAlgorithmDataStructuresPeriodic (Beacon *theBeacon, bool invalidated)
{
  if (invalidated)
    {
      DeleteFromAlgorithmDataStructures (theBeacon,
                                         theBeacon->staticInfoExtension.at (StaticInfoType::co2));
    }
}

} // namespace ns3
