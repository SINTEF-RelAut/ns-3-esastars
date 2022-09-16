//
// Created by Seyedali Tabaeiaghdaei on 06.04.22.
//

#include "src/SCION/headers/beaconing/on_demand_optimization.h"
#include "src/SCION/headers/utils.h"
#include <omp.h>

namespace ns3 {

void
OnDemandOptimization::DoInitializations (uint32_t numASes, rapidxml::xml_node<> *xmlNode,
                                         const YAML::Node &config)
{
  BeaconServer::DoInitializations (numASes, xmlNode, config);
}

void
OnDemandOptimization::PerLinkInitializations (rapidxml::xml_node<> *xmlNode,
                                              const YAML::Node &config)
{
  uint32_t to = std::stoi (xmlNode->first_node ("to")->value ());
  uint32_t from = std::stoi (xmlNode->first_node ("from")->value ());

  NS_ASSERT (g_realToAliasAsNo.at (to) == as->asNumber ||
             g_realToAliasAsNo.at (from) == as->asNumber);

  std::string targetElementStr = "_target";
  uint16_t interfaceId = 0;
  uint16_t neighborAs = 0;

  PropertyContainer p = ParseProperties (xmlNode);
  if (g_realToAliasAsNo.at (to) == as->asNumber)
    {
      targetElementStr = "to" + targetElementStr;
      interfaceId = std::stoi (p.GetProperty ("to_if_id"));
      neighborAs = g_realToAliasAsNo.at (from);
    }
  else
    {
      targetElementStr = "from" + targetElementStr;
      interfaceId = std::stoi (p.GetProperty ("from_if_id"));
      neighborAs = g_realToAliasAsNo.at (to);
    }

  Ld_t latitude = std::stod (p.GetProperty ("latitude"));
  Ld_t longitude = std::stod (p.GetProperty ("longitude"));
  std::pair<Ld_t, Ld_t> coordinates = std::make_pair (latitude, longitude);

  NS_ASSERT (coordinates == as->interfacesCoordinates.at (interfaceId));

  rapidxml::xml_node<> *curXmlTarget = xmlNode->first_node (targetElementStr.c_str ());
  while (curXmlTarget)
    {
      uint16_t targetId = std::stoi (curXmlTarget->value ());
      const OptimizationTarget *optimizationTarget =
          &setOfOptimizationTargetsOriginatedFromThisAs.at (targetId);
      uint16_t interfaceGroup = optimizationTarget->targetIfGroup;

      ifToPushBasedOptimizationTargetsMap.insert (
          std::make_pair (interfaceId, optimizationTarget));
      ifToIfGroup.insert (std::make_pair (interfaceId, interfaceGroup));

      if (interfaceGroupsConnectedPerNeighbor.find (neighborAs) ==
          interfaceGroupsConnectedPerNeighbor.end ())
        {
          interfaceGroupsConnectedPerNeighbor.insert (
              std::make_pair (neighborAs, std::unordered_map<uint16_t, std::vector<uint16_t>> ()));
        }
      if (interfaceGroupsConnectedPerNeighbor.at (neighborAs).find (interfaceGroup) ==
          interfaceGroupsConnectedPerNeighbor.at (neighborAs).end ())
        {
          interfaceGroupsConnectedPerNeighbor.at (neighborAs)
              .insert (std::make_pair (interfaceGroup, std::vector<uint16_t> ()));
        }
      if (std::find (
              std::begin (
                  interfaceGroupsConnectedPerNeighbor.at (neighborAs).at (interfaceGroup)),
              std::end (interfaceGroupsConnectedPerNeighbor.at (neighborAs).at (interfaceGroup)),
              interfaceId) ==
          std::end (interfaceGroupsConnectedPerNeighbor.at (neighborAs).at (interfaceGroup)))
        {
          interfaceGroupsConnectedPerNeighbor.at (neighborAs)
              .at (interfaceGroup)
              .push_back (interfaceId);
        }

      curXmlTarget = curXmlTarget->next_sibling (targetElementStr.c_str ());
    }
#if NS3_ASSERT_ENABLE
  for (uint32_t i = 0; i < AS->interfaces_coordinates.size (); ++i)
    {
      NS_ASSERT (if_to_if_group.find (i) != if_to_if_group.end ());
      NS_ASSERT (if_to_push_based_optimization_targets_map.find (i) !=
                 if_to_push_based_optimization_targets_map.end ());
    }
#endif
}

void
OnDemandOptimization::InitiateBeaconsPerInterface (uint16_t selfEgressIfNo,
                                                      ScionAs *remoteAs,
                                                      uint16_t remoteIngressIfNo)
{
  // push_based
  if (now < firstPullBasedInterval)
    {
      for (auto it = ifToPushBasedOptimizationTargetsMap.lower_bound (selfEgressIfNo);
           it != ifToPushBasedOptimizationTargetsMap.upper_bound (selfEgressIfNo); ++it)
        {
          StaticInfoExtension_t staticInfoExtension;
          CreateInitialStaticInfoExtension (staticInfoExtension, selfEgressIfNo, it->second);
          GenerateBeaconAndSend (NULL, selfEgressIfNo, remoteIngressIfNo, remoteAs,
                                 staticInfoExtension, it->second, BeaconDirection::pushBased);
        }
    }

  // pull-based
  if (now >= firstPullBasedInterval &&
      (now / beaconingPeriod.ToInteger (Time::MIN)) % pullBasedDisseminationToInitiationFrequency ==
          0)
    {
      for (auto it = ifToPullBasedOptimizationTargetsMap.lower_bound (selfEgressIfNo);
           it != ifToPullBasedOptimizationTargetsMap.upper_bound (selfEgressIfNo); ++it)
        {
          NS_ASSERT (it->second->setOfForbiddenEdges->find (as->asNumber) !=
                     it->second->setOfForbiddenEdges->end ());
          if (it->second->setOfForbiddenEdges->at (as->asNumber)->find (selfEgressIfNo) !=
              it->second->setOfForbiddenEdges->at (as->asNumber)->end ())
            {
              continue;
            }
          StaticInfoExtension_t staticInfoExtension;
          CreateInitialStaticInfoExtension (staticInfoExtension, selfEgressIfNo, it->second);
          GenerateBeaconAndSend (NULL, selfEgressIfNo, remoteIngressIfNo, remoteAs,
                                 staticInfoExtension, it->second, BeaconDirection::pullBased);
        }
    }
}

void
OnDemandOptimization::CreateInitialStaticInfoExtension (
    StaticInfoExtension_t &staticInfoExtension, uint16_t selfEgressIfNo,
    const OptimizationTarget *optimizationTarget)
{
  for (auto const &criteria : optimizationTarget->criteria)
    {
      if (criteria.first == latency)
        {
          staticInfoExtension.insert (std::make_pair (StaticInfoType::latency, 0));
        }
      else if (criteria.first == bw)
        {
          staticInfoExtension.insert (
              std::make_pair (StaticInfoType::bw, as->interAsBwds.at (selfEgressIfNo)));
        }
      else if (criteria.first == co2)
        {
          staticInfoExtension.insert (std::make_pair (StaticInfoType::co2, 0));
        }
    }
}

void
OnDemandOptimization::ExtendStaticInfoExtension (
    const Beacon *theBeacon, uint16_t beaconIngressIfNo, uint16_t candidateEgressIfNo,
                                                 StaticInfoExtension_t &propagationStaticInfo)
{
  for (auto const &criteria : theBeacon->optimizationTarget->criteria)
    {
      if (criteria.first == latency)
        {
          Ld_t latency = theBeacon->staticInfoExtension.at (StaticInfoType::latency) +
                       as->latenciesBetweenInterfaces.at (beaconIngressIfNo)
                           .at (candidateEgressIfNo);
          propagationStaticInfo.insert (std::make_pair (StaticInfoType::latency, latency));
        }
      else if (criteria.first == bw)
        {
          Ld_t bw = theBeacon->staticInfoExtension.at (StaticInfoType::bw) >
                          (Ld_t) as->interAsBwds.at (candidateEgressIfNo)
                      ? (Ld_t) as->interAsBwds.at (candidateEgressIfNo)
                      : theBeacon->staticInfoExtension.at (StaticInfoType::bw);
          propagationStaticInfo.insert (std::make_pair (StaticInfoType::bw, bw));
        }
      else if (criteria.first == co2)
        {
          propagationStaticInfo.insert (std::make_pair (StaticInfoType::co2, 0));
        }
    }
}

void
OnDemandOptimization::DisseminateBeacons (NeighbourRelation relation)
{
  NS_ASSERT (now == (uint16_t) Simulator::Now ().ToInteger (Time::MIN));
  bool pullBasedDissemination = now >= firstPullBasedInterval;
  bool pushBasedDissemination = now < firstPullBasedInterval;

  auto &beaconsGroupedByOptimizationTargetsAndIngressIf =
      (pushBasedDissemination)
          ? pushBasedBeaconsGroupedByOptimizationTargetsAndIngressIfGroup
          : pullBasedBeaconsGroupedByOptimizationTargetsAndIngressIfGroup.at (
                pullBasedRead);

  uint32_t neighborsCnt = as->neighbors.size ();
  omp_set_num_threads (g_numCore);
#pragma omp parallel for schedule(dynamic)

  for (uint32_t i = 0; i < neighborsCnt; ++i)
    { // Per neighbor AS
      if (as->neighbors.at (i).second != relation)
        {
          continue;
        }

      uint16_t remoteAsNo = as->neighbors.at (i).first;

      for (auto const &[optimizationTarget, beaconsWithTheSameOptTarget] :
           beaconsGroupedByOptimizationTargetsAndIngressIf)
        {
          if (optimizationTarget->targetAs == as->asNumber)
            { // pull-based request to this AS
              continue;
            }

          std::unordered_map<
              uint16_t,
              std::multimap<Ld_t, std::tuple<Beacon *, uint16_t, uint16_t,
                                                                ScionAs *, StaticInfoExtension_t>,
                  std::greater<Ld_t>>>
              selected_beacons;

          SelectBeaconsToDisseminatePerTargetPerNbr (remoteAsNo, beaconsWithTheSameOptTarget,
                                                     optimizationTarget, selected_beacons);
          SendSelectedBeaconsPerTargetPerNbr (selected_beacons);
        }
    }

  if (pullBasedDissemination)
    {
      // returning pull-based beacons
      for (auto const &[optimizationTarget, beaconsWithTheSameOptTarget] :
           pullBasedBeaconsGroupedByOptimizationTargetsAndIngressIfGroup.at (
               pullBasedRead))
        {
          if (optimizationTarget->targetAs != as->asNumber)
            {
              continue;
            }

          for (auto const &[beaconIngressIfGroup, scoreBeaconsMap] :
               beaconsWithTheSameOptTarget)
            {
              for (auto &[score, theBeacon] : scoreBeaconsMap)
                {
                  if (theBeacon->expirationTime > now)
                    {
                      uint64_t firstHop = theBeacon->path.front ();
                      ScionAs *requestingAs = dynamic_cast<ScionAs *> (
                          PeekPointer (g_nodes.Get (UPPER_16_BITS (firstHop))));
                      requestingAs->ReceiveBeacon (*theBeacon, SECOND_LOWER_16_BITS (firstHop),
                                                    LOWER_16_BITS (firstHop),
                                                    SECOND_UPPER_16_BITS (firstHop));
                    }
                }
            }
        }
      pullBasedBeaconsGroupedByOptimizationTargetsAndIngressIfGroup.at (pullBasedRead)
          .clear ();
      nonRequestedPullBasedBeaconContainer.at (pullBasedRead).clear ();
    }
}

void
OnDemandOptimization::SelectBeaconsToDisseminatePerTargetPerNbr (
    uint16_t remoteAsNo,
    const BeaconsWithTheSameOptTarget_t &beaconsWithTheSameOptTarget,
    const OptimizationTarget *optimizationTarget,
    std::unordered_map<
        uint16_t,
        std::multimap<Ld_t,
                      std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *, StaticInfoExtension_t>,
                      std::greater<Ld_t>>> &selectedBeacons)
{
  for (auto const &[beaconIngressIfGroup, scoreBeaconsMap] : beaconsWithTheSameOptTarget)
    {
      for (auto const &[score, theBeacon] : scoreBeaconsMap)
        {
          if ((theBeacon->beaconDirection == BeaconDirection::pushBased &&
               !theBeacon->isValid) ||
              (theBeacon->beaconDirection == BeaconDirection::pullBased &&
               theBeacon->expirationTime < now))
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

          NS_ASSERT (!interfaceGroupsConnectedPerNeighbor.at (remoteAsNo).empty ());
          for (auto const &[group, ifaces] : interfaceGroupsConnectedPerNeighbor.at (remoteAsNo))
            {
              NS_ASSERT (!ifaces.empty ());
              for (auto const &candidateEgressIfNo : ifaces)
                {
                  if (theBeacon->optimizationTarget->setOfForbiddenEdges != NULL)
                    {
                      if (theBeacon->optimizationTarget->setOfForbiddenEdges->find (
                              as->asNumber) !=
                              theBeacon->optimizationTarget->setOfForbiddenEdges->end () &&
                          theBeacon->optimizationTarget->setOfForbiddenEdges->at (as->asNumber)
                                  ->find (candidateEgressIfNo) !=
                              theBeacon->optimizationTarget->setOfForbiddenEdges
                                  ->at (as->asNumber)
                                  ->end ())
                        {
                          continue;
                        }
                    }
                  if (selectedBeacons.find (group) == selectedBeacons.end ())
                    {
                      selectedBeacons.insert (std::make_pair (
                          group, std::multimap<Ld_t,
                                               std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *,
                                                          StaticInfoExtension_t>,
                                               std::greater<Ld_t>> ()));
                    }

                  StaticInfoExtension_t propagationStaticInfo;
                  ExtendStaticInfoExtension (theBeacon,
                                             LOWER_16_BITS (theBeacon->path.back ()),
                                             candidateEgressIfNo, propagationStaticInfo);

                  Ld_t disseminationScore =
                      CalculateScore (theBeacon->optimizationTarget, propagationStaticInfo);

                  auto [remoteIngressIfNo, remoteAs] =
                      as->GetRemoteAsInfo (candidateEgressIfNo);

                  selectedBeacons.at (group).insert (std::make_pair (
                      disseminationScore,
                      std::make_tuple (theBeacon, candidateEgressIfNo, remoteIngressIfNo,
                                       remoteAs,
                                       propagationStaticInfo)));
                }
            }
        }
    }

  for (auto &subgroupSelectedBeaconsPerSubgroupPair : selectedBeacons)
    {
      auto &selectedBeaconsPerSubgroup = subgroupSelectedBeaconsPerSubgroupPair.second;
      if (selectedBeaconsPerSubgroup.size () >
          optimizationTarget->noBeaconsPerOptimizationTarget)
        {
          auto it = selectedBeaconsPerSubgroup.begin ();
          std::advance (it, optimizationTarget->noBeaconsPerOptimizationTarget);
          selectedBeaconsPerSubgroup.erase (it, selectedBeaconsPerSubgroup.end ());
        }
      NS_ASSERT (selectedBeaconsPerSubgroup.size () ==
                 optimizationTarget->noBeaconsPerOptimizationTarget);
    }
}

void
OnDemandOptimization::SendSelectedBeaconsPerTargetPerNbr (
    const std::unordered_map<
        uint16_t,
        std::multimap<Ld_t,
                      std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *, StaticInfoExtension_t>,
                      std::greater<Ld_t>>> &selectedBeacons)
{
  for (auto const &groupSelectedBeaconsPair : selectedBeacons)
    {
      for (auto const &scoreSelectedBeaconsPair : groupSelectedBeaconsPair.second)
        {
          Beacon *theBeacon;
          uint16_t remoteIngressIfNo;
          uint16_t selfEgressIfNo;
          ScionAs *remoteAs;
          StaticInfoExtension_t staticInfoExtension;

          std::tie (theBeacon, selfEgressIfNo, remoteIngressIfNo, remoteAs,
                    staticInfoExtension) = scoreSelectedBeaconsPair.second;

          GenerateBeaconAndSend (theBeacon, selfEgressIfNo, remoteIngressIfNo, remoteAs,
                                 staticInfoExtension, theBeacon->optimizationTarget,
                                 theBeacon->beaconDirection);
        }
    }
}

std::tuple<bool, bool, bool, Beacon *, Ld_t>
OnDemandOptimization::AlgSpecificImportPolicy (Beacon &theBeacon, uint16_t senderAs,
                                                  uint16_t remoteEgressIfNo,
                                                  uint16_t selfIngressIfNo, uint16_t now)
{
  if (theBeacon.beaconDirection == BeaconDirection::pullBased)
    {
      if (ORIGINATOR (theBeacon) == as->asNumber)
        {
          if (now - theBeacon.nextInitiationTime >=
              pullBasedDisseminationToInitiationFrequency *
                  beaconingPeriod.ToInteger (Time::MIN))
            {
              return std::tuple<bool, bool, bool, Beacon *, Ld_t> (false, false, false, NULL, 0);
            }
          return std::tuple<bool, bool, bool, Beacon *, Ld_t> (true, false, false, NULL, 0);
        }
      else
        {
          if (visitedPullBasedSrcDstPair.find (
                  std::make_pair (ORIGINATOR (theBeacon), DST_AS (theBeacon))) != visitedPullBasedSrcDstPair.end ())
            {
              return std::tuple<bool, bool, bool, Beacon *, Ld_t> (false, false, false, NULL, 0);
            }
        }
    }

  const auto &groupedBeacons =
      (theBeacon.beaconDirection == BeaconDirection::pushBased)
          ? pushBasedBeaconsGroupedByOptimizationTargetsAndIngressIfGroup
          : pullBasedBeaconsGroupedByOptimizationTargetsAndIngressIfGroup.at (
                pullBasedWrite);

  uint16_t accessIndex = ifToIfGroup.at (selfIngressIfNo);

  if (groupedBeacons.find (theBeacon.optimizationTarget) == groupedBeacons.end ())
    {
      return std::tuple<bool, bool, bool, Beacon *, Ld_t> (true, false, false, NULL, 0);
    }

  if (groupedBeacons.at (theBeacon.optimizationTarget).find (accessIndex) ==
      groupedBeacons.at (theBeacon.optimizationTarget).end ())
    {
      return std::tuple<bool, bool, bool, Beacon *, Ld_t> (true, false, false, NULL, 0);
    }

  if (groupedBeacons.at (theBeacon.optimizationTarget).at (accessIndex).size () <
      theBeacon.optimizationTarget->noBeaconsPerOptimizationTarget)
    {
      return std::tuple<bool, bool, bool, Beacon *, Ld_t> (true, false, false, NULL, 0);
    }

  std::multimap<Ld_t, Beacon *>::const_reverse_iterator worstBeaconScore =
      groupedBeacons.at (theBeacon.optimizationTarget).at (accessIndex).rbegin ();
  Ld_t incomingBeaconScore =
      CalculateScore (theBeacon.optimizationTarget, theBeacon.staticInfoExtension);
  Ld_t lowestPreviousScore = worstBeaconScore->first;
  if (lowestPreviousScore < incomingBeaconScore)
    {
      Beacon *toBeRemovedBeacon = worstBeaconScore->second;
      NS_ASSERT (toBeRemovedBeacon->beaconDirection == theBeacon.beaconDirection);
      NS_ASSERT (DST_AS_PTR (toBeRemovedBeacon) == DST_AS (theBeacon));
      return std::tuple<bool, bool, bool, Beacon *, Ld_t> (true, false, false, toBeRemovedBeacon,
                                                         lowestPreviousScore);
    }

  NS_ASSERT (theBeacon.beaconDirection != BeaconDirection::pushBased ||
             nextRoundValidBeaconsCountPerDstAs.find (DST_AS (theBeacon)) !=
                 nextRoundValidBeaconsCountPerDstAs.end ());
  return std::tuple<bool, bool, bool, Beacon *, Ld_t> (false, false, false, NULL, 0);
}

void
OnDemandOptimization::InsertToAlgorithmDataStructures (Beacon *theBeacon, uint16_t senderAs,
                                                           uint16_t remoteEgressIfNo,
                                                           uint16_t selfIngressIfNo)
{
  if (theBeacon->beaconDirection == BeaconDirection::pullBased)
    {
      if (ORIGINATOR_PTR (theBeacon) == as->asNumber)
        {
          InsertToForbiddenEdges (theBeacon);
          return;
        }
      else
        {
          visitedPullBasedSrcDstPair.insert (
              std::make_pair (ORIGINATOR_PTR (theBeacon), DST_AS_PTR (theBeacon)));
        }
    }

  auto &groupedBeacons =
      (theBeacon->beaconDirection == BeaconDirection::pushBased)
          ? pushBasedBeaconsGroupedByOptimizationTargetsAndIngressIfGroup
          : pullBasedBeaconsGroupedByOptimizationTargetsAndIngressIfGroup.at (
                pullBasedWrite);

  uint16_t accessIndex = ifToIfGroup.at (selfIngressIfNo);

  if (groupedBeacons.find (theBeacon->optimizationTarget) == groupedBeacons.end ())
    {
      groupedBeacons.insert (
          std::make_pair (theBeacon->optimizationTarget, BeaconsWithTheSameOptTarget_t ()));
    }

  if (groupedBeacons.at (theBeacon->optimizationTarget).find (accessIndex) ==
      groupedBeacons.at (theBeacon->optimizationTarget).end ())
    {
      groupedBeacons.at (theBeacon->optimizationTarget)
          .insert (std::make_pair (accessIndex, BeaconsWithTheSameOptTargetAndIngressIfGroup_t ()));
    }

  Ld_t incomingBeaconScore =
      CalculateScore (theBeacon->optimizationTarget, theBeacon->staticInfoExtension);

  groupedBeacons.at (theBeacon->optimizationTarget)
      .at (accessIndex)
      .insert (std::make_pair (incomingBeaconScore, theBeacon));

  if (theBeacon->beaconDirection == BeaconDirection::pushBased)
    {
      InsertToForbiddenEdges (theBeacon);
    }
}

void
OnDemandOptimization::DeleteFromAlgorithmDataStructures (Beacon *theBeacon, Ld_t replacementKey)
{
  if (theBeacon->beaconDirection == BeaconDirection::pullBased &&
      ORIGINATOR_PTR (theBeacon) == as->asNumber)
    {
      DeleteFromForbiddenEdges (theBeacon);
      return;
    }

  uint16_t selfIngressIf = (theBeacon->beaconDirection == BeaconDirection::pushBased)
                                 ? LOWER_16_BITS (theBeacon->path.back ())
                                 : SECOND_UPPER_16_BITS (theBeacon->path.front ());

  auto &groupedBeacons =
      (theBeacon->beaconDirection == BeaconDirection::pushBased)
          ? pushBasedBeaconsGroupedByOptimizationTargetsAndIngressIfGroup
                .at (theBeacon->optimizationTarget)
                .at (ifToIfGroup.at (selfIngressIf))
          : pullBasedBeaconsGroupedByOptimizationTargetsAndIngressIfGroup.at (pullBasedWrite)
                .at (theBeacon->optimizationTarget)
                .at (ifToIfGroup.at (selfIngressIf));

  for (auto it = groupedBeacons.lower_bound (replacementKey);
       it != groupedBeacons.upper_bound (replacementKey); ++it)
    {
      if (it->second == theBeacon)
        {
          groupedBeacons.erase (it--);
          break;
        }
    }

  if (theBeacon->beaconDirection == BeaconDirection::pushBased)
    {
      DeleteFromForbiddenEdges (theBeacon);
    }
}

Ld_t
OnDemandOptimization::CalculateScore (const OptimizationTarget *optimizationTarget,
                                       const StaticInfoExtension_t &staticInfoExtension)
{
  Ld_t score = 0;
  Ld_t weightsSum = 0;
  for (auto const &criteria : optimizationTarget->criteria)
    {
      weightsSum += criteria.second;
      if (criteria.first == StaticInfoType::latency)
        { // in ms, assuming max latency is 1000 ms
          score += (1 - staticInfoExtension.at (StaticInfoType::latency) / 1000.0) *
                   criteria.second;
        }
      else if (criteria.first == StaticInfoType::bw)
        { // in Gbps, assuming max BW is 400 Gbps
          score += staticInfoExtension.at (StaticInfoType::bw) / 400.0 * criteria.second;
        }
      else if (criteria.first == StaticInfoType::co2)
        { // in g/Gbps, assuming max is 10 g/Gbps
          score +=
              (1 - staticInfoExtension.at (StaticInfoType::co2) / 10.0) * criteria.second;
        }
      else if (criteria.first == StaticInfoType::forbiddenEdges)
        {
        }
    }

  if (weightsSum == 0)
    {
      return score;
    }

  return (score / weightsSum);
}

void
OnDemandOptimization::UpdateAlgorithmDataStructuresPeriodic (Beacon *theBeacon,
                                                                 bool invalidated)
{
  if (invalidated)
    {
      DeleteFromForbiddenEdges (theBeacon);
    }
}

void
OnDemandOptimization::UpdateStateBeforeBeaconing ()
{
  BeaconServer::UpdateStateBeforeBeaconing ();
  if (now == firstPullBasedInterval)
    {
      if (fileToReadBeacons != "none")
        {
          NS_ASSERT (!beaconStore.empty ());
          for (auto const &dstBeacons : beaconStore)
            {
              for (auto const &lenBeacons : dstBeacons.second)
                {
                  if (lenBeacons.first == 1)
                    {
                      break;
                    }

                  for (auto const &theBeacon : lenBeacons.second)
                    {
                      InsertToForbiddenEdges (theBeacon);
                    }
                }
            }
        }

      CheckMaxTolerableLinkFailures ();
    }

  NS_ASSERT (now == Simulator::Now ().ToInteger (Time::MIN));
  if (now >= firstPullBasedInterval &&
      (now / beaconingPeriod.ToInteger (Time::MIN)) % pullBasedDisseminationToInitiationFrequency ==
          0)
    {
      if (!newRequestedPullBasedBeacons.empty ())
        {
          for (auto const &theBeacon : newRequestedPullBasedBeacons)
            {
              NS_ASSERT (now - theBeacon->nextInitiationTime >=
                         pullBasedDisseminationToInitiationFrequency *
                             beaconingPeriod.ToInteger (Time::MIN));
              InsertToForbiddenEdges (theBeacon);
            }
          newRequestedPullBasedBeacons.clear ();
        }
      visitedPullBasedSrcDstPair.clear ();
      if (now > firstPullBasedInterval)
        {
          CheckMaxTolerableLinkFailures ();
        }
    }
}

void
OnDemandOptimization::DeleteFromForbiddenEdges (Beacon *theBeacon)
{
  if (theBeacon->beaconDirection == BeaconDirection::pullBased &&
      ORIGINATOR_PTR (theBeacon) == as->asNumber &&
      now - theBeacon->nextInitiationTime <
          pullBasedDisseminationToInitiationFrequency * beaconingPeriod.ToInteger (Time::MIN))
    {
      newRequestedPullBasedBeacons.erase (theBeacon);
      return;
    }

  uint16_t dstAs = DST_AS_PTR (theBeacon);

  NS_ASSERT (theBeacon->beaconDirection == BeaconDirection::pushBased ||
             ORIGINATOR_PTR (theBeacon) == as->asNumber);
  NS_ASSERT (repetitionOfEdges.find (dstAs) != repetitionOfEdges.end ());

  std::vector<LinkInformation_t>::reverse_iterator hop = theBeacon->path.rbegin ();
  for (; hop != theBeacon->path.rend (); ++hop)
    {
      uint32_t observedEdge;
      uint16_t observedAs;
      uint16_t observedIface;

      if (theBeacon->beaconDirection == BeaconDirection::pullBased)
        {
          observedEdge = UPPER_32_BITS (*hop);
          observedAs = UPPER_16_BITS (*hop);
          observedIface = SECOND_UPPER_16_BITS (*hop);
        }
      else
        {
          observedEdge = LOWER_32_BITS (*hop);
          observedAs = SECOND_LOWER_16_BITS (*hop);
          observedIface = LOWER_16_BITS (*hop);
        }
      NS_ASSERT (repetitionOfEdges.at (dstAs)->find (observedEdge) !=
                 repetitionOfEdges.at (dstAs)->end ());
      repetitionOfEdges.at (dstAs)->at (observedEdge)--;
      edgeToBeacon.at (dstAs)->at (observedEdge).erase (theBeacon);

      if (repetitionOfEdges.at (dstAs)->at (observedEdge) == 0)
        {
          repetitionOfEdges.at (dstAs)->erase (observedEdge);
          edgeToBeacon.at (dstAs)->erase (observedEdge);
          NS_ASSERT (setOfForbiddenEdgesPerDestinationAs.at (dstAs)->find (observedAs) !=
                     setOfForbiddenEdgesPerDestinationAs.at (dstAs)->end ());
          NS_ASSERT (setOfForbiddenEdgesPerDestinationAs.at (dstAs)
                  ->at (observedAs)
                  ->find (observedIface) !=
                     setOfForbiddenEdgesPerDestinationAs.at (dstAs)->at (observedAs)->end ());
          setOfForbiddenEdgesPerDestinationAs.at (dstAs)
              ->at (observedAs)
              ->erase (observedIface);
          // We do not remove the observed AS as it is a set object in the heap
        }
    }
}

void
OnDemandOptimization::InsertToForbiddenEdges (Beacon *theBeacon)
{
  if (theBeacon->beaconDirection == BeaconDirection::pullBased &&
      ORIGINATOR_PTR (theBeacon) == as->asNumber &&
      now - theBeacon->nextInitiationTime <
          pullBasedDisseminationToInitiationFrequency * beaconingPeriod.ToInteger (Time::MIN))
    {
      newRequestedPullBasedBeacons.insert (theBeacon);
      return;
    }

  uint16_t dstAs = DST_AS_PTR (theBeacon);
  NS_ASSERT (theBeacon->beaconDirection == BeaconDirection::pushBased ||
             ORIGINATOR_PTR (theBeacon) == as->asNumber);

  if (repetitionOfEdges.find (dstAs) == repetitionOfEdges.end ())
    {
      repetitionOfEdges.insert (
          std::make_pair (dstAs, new std::unordered_map<uint32_t, uint16_t> ()));
      edgeToBeacon.insert (std::make_pair (
          dstAs, new std::unordered_map<uint32_t, std::unordered_set<const Beacon *>> ()));
      NS_ASSERT (setOfForbiddenEdgesPerDestinationAs.find (dstAs) ==
                 setOfForbiddenEdgesPerDestinationAs.end ());
      setOfForbiddenEdgesPerDestinationAs.insert (std::make_pair (
          dstAs, new std::unordered_map<uint16_t, std::unordered_set<uint16_t> *> ()));
    }

  std::vector<LinkInformation_t>::const_reverse_iterator hop = theBeacon->path.rbegin ();
  for (; hop != theBeacon->path.rend (); ++hop)
    {
      uint32_t observedEdge;
      uint16_t observedAs;
      uint16_t observedIface;

      if (theBeacon->beaconDirection == BeaconDirection::pullBased)
        {
          observedEdge = UPPER_32_BITS (*hop);
          observedAs = UPPER_16_BITS (*hop);
          observedIface = SECOND_UPPER_16_BITS (*hop);
        }
      else
        {
          observedEdge = LOWER_32_BITS (*hop);
          observedAs = SECOND_LOWER_16_BITS (*hop);
          observedIface = LOWER_16_BITS (*hop);
        }

      NS_ASSERT (repetitionOfEdges.find (dstAs) != repetitionOfEdges.end ());
      NS_ASSERT (repetitionOfEdges.at (dstAs) != NULL);
      if (repetitionOfEdges.at (dstAs)->find (observedEdge) ==
          repetitionOfEdges.at (dstAs)->end ())
        {
          repetitionOfEdges.at (dstAs)->insert (std::make_pair (observedEdge, 0));
          edgeToBeacon.at (dstAs)->insert (
              std::make_pair (observedEdge, std::unordered_set<const Beacon *> ()));
          NS_ASSERT (setOfForbiddenEdgesPerDestinationAs.at (dstAs)->find (observedAs) ==
                         setOfForbiddenEdgesPerDestinationAs.at (dstAs)->end () ||
                     setOfForbiddenEdgesPerDestinationAs.at (dstAs)
                      ->at (observedAs)
                      ->find (observedIface) ==
                         setOfForbiddenEdgesPerDestinationAs.at (dstAs)->at (observedAs)->end ());
        }

      edgeToBeacon.at (dstAs)->at (observedEdge).insert (theBeacon);
      repetitionOfEdges.at (dstAs)->at (observedEdge)++;
      if (setOfForbiddenEdgesPerDestinationAs.at (dstAs)->find (observedAs) ==
          setOfForbiddenEdgesPerDestinationAs.at (dstAs)->end ())
        {
          setOfForbiddenEdgesPerDestinationAs.at (dstAs)->insert (
              std::make_pair (observedAs, new std::unordered_set<uint16_t> ()));
        }
      if (setOfForbiddenEdgesPerDestinationAs.at (dstAs)
              ->at (observedAs)
              ->find (observedIface) ==
          setOfForbiddenEdgesPerDestinationAs.at (dstAs)->at (observedAs)->end ())
        {
          setOfForbiddenEdgesPerDestinationAs.at (dstAs)
              ->at (observedAs)
              ->insert (observedIface);
        }
    }
}

void
OnDemandOptimization::CheckMaxTolerableLinkFailures ()
{
  for (auto const &[dstAs, perDstEdgeToBeacon] : edgeToBeacon)
    {
      auto perDstEdgeToBeaconCopy = *perDstEdgeToBeacon;

      uint16_t maxTolerableLinkFailure =
          CheckMaxTolerableLinkFailuresPerDst (perDstEdgeToBeaconCopy, 0);

      if (maxTolerableLinkFailure < desiredMaxTolerableLinkFailures)
        {
          if (now == firstPullBasedInterval)
            {
              CreateOptimizationTargetsForForbiddenEdges (dstAs);
            }
        }
      else
        {
          RemoveOptimizationTargetsForForbiddenEdges (dstAs);
        }
    }
}

uint16_t
OnDemandOptimization::CheckMaxTolerableLinkFailuresPerDst (
    std::unordered_map<uint32_t, std::unordered_set<const Beacon *>> &perDstEdgeToBeacon,
    uint16_t maxTolerableLinkFailure)
{
  if (perDstEdgeToBeacon.empty ())
    {
      return maxTolerableLinkFailure;
    }

  uint32_t maxEdge = 0;
  uint16_t maxRepetition = 0;
  for (auto const &[edge, beacons] : perDstEdgeToBeacon)
    {
      if (beacons.size () > maxRepetition)
        {
          maxEdge = edge;
          maxRepetition = beacons.size ();
        }
    }

  for (auto const &theBeacon : perDstEdgeToBeacon.at (maxEdge))
    {
      std::vector<LinkInformation_t>::const_reverse_iterator hop = theBeacon->path.rbegin ();
      for (; hop != theBeacon->path.rend (); ++hop)
        {
          uint32_t observedEdge;

          if (theBeacon->beaconDirection == BeaconDirection::pullBased)
            {
              observedEdge = UPPER_32_BITS (*hop);
            }
          else
            {
              observedEdge = LOWER_32_BITS (*hop);
            }

          if (observedEdge == maxEdge)
            {
              continue;
            }

          perDstEdgeToBeacon.at (observedEdge).erase (theBeacon);
        }
    }

  perDstEdgeToBeacon.erase (maxEdge);

  for (auto it = perDstEdgeToBeacon.begin (); it != perDstEdgeToBeacon.end ();)
    {
      if (it->second.empty ())
        {
          it = perDstEdgeToBeacon.erase (it);
        }
      else
        {
          ++it;
        }
    }

  return CheckMaxTolerableLinkFailuresPerDst (perDstEdgeToBeacon, maxTolerableLinkFailure + 1);
}

void
OnDemandOptimization::CreateOptimizationTargetsForForbiddenEdges (uint16_t dstAs)
{
  OptimizationTarget *optimizationTarget =
      new OptimizationTarget (0xFFFF - as->asNumber, {{StaticInfoType::forbiddenEdges, 1}},
                              OptimizationDirection::symmetric, dstAs, 0xFFFF, 1,
                               setOfForbiddenEdgesPerDestinationAs.at (dstAs));

  for (uint16_t iface = 0; iface < as->GetNDevices (); ++iface)
    {
      ifToPullBasedOptimizationTargetsMap.insert (
          std::make_pair (iface, optimizationTarget));
    }
}

void
OnDemandOptimization::RemoveOptimizationTargetsForForbiddenEdges (uint16_t dstAs)
{
  for (auto it = ifToPullBasedOptimizationTargetsMap.begin ();
       it != ifToPullBasedOptimizationTargetsMap.end ();)
    {
      if (it->second->targetAs == dstAs)
        {
          it = ifToPullBasedOptimizationTargetsMap.erase (it);
        }
      else
        {
          ++it;
        }
    }
}

} // namespace ns3