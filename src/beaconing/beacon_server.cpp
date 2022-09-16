/**
 * @file beacon_server.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see beacon_server.h
 */

#include <omp.h>

#include "ns3/point-to-point-net-device.h"

#include "src/SCION/headers/beaconing/beacon_server.h"
#include "src/SCION/headers/path_server.h"
#include "src/SCION/headers/run_parallel_events.h"
#include "src/SCION/headers/scion_core_as.h"
#include "src/SCION/headers/utils.h"

namespace ns3 {

void
BeaconServer::DoInitializations (uint32_t numAses, rapidxml::xml_node<> *xmlNode,
                                 const YAML::Node &config)
{
  beaconsSentPerInterface.resize (as->GetNDevices ());
  pullBasedBeaconsSentPerOptPerInterface.resize (as->GetNDevices ());
  pushBasedBeaconsSentPerOptPerInterface.resize (as->GetNDevices ());
  beaconsSentPerDstPerInterface.resize (as->GetNDevices ());
}
void BeaconServer::PerLinkInitializations (rapidxml::xml_node<> *xmlNode,
                                           const YAML::Node &config){};

void
BeaconServer::SetAs (ScionAs *as)
{
  this->as = as;
}

void
BeaconServer::ScheduleBeaconing (Time lastBeaconingEventTime)
{
  if (parallelScheduler)
    {
      Simulator::Schedule (Seconds (0), &RunParallelEvents<void (BeaconServer::*) ()>,
                           &BeaconServer::ReadBeacons);
      Simulator::Schedule (lastBeaconingEventTime + TimeStep (2),
                           &RunParallelEvents<void (BeaconServer::*) ()>,
                           &BeaconServer::WriteBeacons);
    }
  for (Time t = Seconds (0); t <= lastBeaconingEventTime; t += beaconingPeriod)
    {
      if (parallelScheduler)
        {
          if (as->GetPathServer () != NULL)
            {
              Simulator::Schedule (t + as->latencyBetweenPathServerAndBeaconServer,
                                   &RunParallelEvents<void (BeaconServer::*) ()>,
                                   &BeaconServer::RegisterToLocalPathServer);
            }
          Simulator::Schedule (t, &RunParallelEvents<void (BeaconServer::*) ()>,
                               &BeaconServer::UpdateStateBeforeBeaconing);

          Simulator::Schedule (t + TimeStep (1), &RunParallelEvents<void (BeaconServer::*) ()>,
                               &BeaconServer::UpdateStatePeriodic);
        }

      if (dynamic_cast<ScionCoreAs *> (as) != NULL)
        {
          Simulator::Schedule (t, &BeaconServer::DisseminateBeacons, this, NeighbourRelation::core);

          Simulator::Schedule (t, &BeaconServer::InitiateBeacons, this, NeighbourRelation::core);
          Simulator::Schedule (t, &BeaconServer::InitiateBeacons, this,
                               NeighbourRelation::customer);
        }
      else
        {
          Simulator::Schedule (t, &BeaconServer::DisseminateBeacons, this,
                               NeighbourRelation::customer);
        }
    }
}

void
BeaconServer::InsertPulledBeaconsToBeaconStore ()
{
  for (auto &keyBeaconPair : requestedPullBasedBeaconContainer)
    {
      Beacon *toInsertBeacon = &keyBeaconPair.second;
      std::vector<uint64_t> &thePath = toInsertBeacon->path;
      uint16_t dstAs = toInsertBeacon->optimizationTarget->targetAs;

      std::reverse (thePath.begin (), thePath.end ());
      for (uint32_t i = 0; i < thePath.size (); ++i)
        {
          thePath.at (i) = (thePath.at (i) >> 32) | (thePath.at (i) << 32);
        }

      NS_ASSERT (dstAs == UPPER_16_BITS (thePath.front ()));

      uint16_t pathLen = (uint16_t) toInsertBeacon->path.size ();

      if (beaconStore.find (dstAs) != beaconStore.end () &&
          beaconStore.at (dstAs).find (pathLen) != beaconStore.at (dstAs).end ())
        {
          beaconStore.at (dstAs).at (pathLen).insert (toInsertBeacon);
        }
      else if (beaconStore.find (dstAs) != beaconStore.end () &&
               beaconStore.at (dstAs).find (pathLen) == beaconStore.at (dstAs).end ())
        {
          beaconStore.at (dstAs).insert (std::make_pair (pathLen, BeaconsWithEqualLength_t ()));
          beaconStore.at (dstAs).at (pathLen).insert (toInsertBeacon);
        }
      else
        {
          beaconStore.insert (std::make_pair (dstAs, BeaconsWithSameDstAs_t ()));
          beaconStore.at (dstAs).insert (std::make_pair (pathLen, BeaconsWithEqualLength_t ()));
          beaconStore.at (dstAs).at (pathLen).insert (toInsertBeacon);
        }
    }
}

void
BeaconServer::UpdateBeaconState (Beacon *theBeacon)
{
  uint16_t dstAs = DST_AS_PTR (theBeacon);
  NS_ASSERT (theBeacon->beaconDirection == BeaconDirection::pullBased ||
             theBeacon->optimizationTarget == NULL ||
             ORIGINATOR_PTR (theBeacon) == theBeacon->optimizationTarget->targetAs);
  if (theBeacon->isNew)
    {
      theBeacon->isNew = false;
      if (theBeacon->nextExpirationTime > now)
        {
          if (!theBeacon->isValid)
            {
              theBeacon->isValid = true;
              IncrementValidBeaconsCount (dstAs);
            }
          theBeacon->initiationTime = theBeacon->nextInitiationTime;
          theBeacon->expirationTime = theBeacon->nextExpirationTime;
        }
    }

  if (theBeacon->expirationTime <= nextPeriod && theBeacon->isValid)
    {
      theBeacon->isValid = false;
      DecrementValidBeaconsCount (dstAs);
      DecrementNextRoundValidBeaconsCount (dstAs);
    }
}

void
BeaconServer::CreateInitialStaticInfoExtension (StaticInfoExtension_t &staticInfoExtension, uint16_t selfEgressIfNo,
    const OptimizationTarget *optimizationTarget)
{
}

void
BeaconServer::InitiateBeacons (NeighbourRelation relation)
{
  NS_ASSERT (now == (uint16_t) Simulator::Now ().ToInteger (Time::MIN));
  uint32_t neighborsCnt = as->neighbors.size ();
  omp_set_num_threads (g_numCore);

#pragma omp parallel for
  for (uint32_t i = 0; i < neighborsCnt; ++i)
    {
      if (as->neighbors.at (i).second != relation)
        {
          continue;
        }

      uint16_t remoteAsNo = as->neighbors.at (i).first;
      const auto &interfaces = as->interfacesPerNeighborAs.at (remoteAsNo);
      for (auto const &selfEgressIfNo : interfaces)
        {
          std::pair<uint16_t, ScionAs *> remoteAsIfPair = as->GetRemoteAsInfo (selfEgressIfNo);

          uint16_t remoteIngressIfNo = remoteAsIfPair.first;
          ScionAs *remoteAs = remoteAsIfPair.second;

          InitiateBeaconsPerInterface (selfEgressIfNo, remoteAs, remoteIngressIfNo);
        }
    }
}

void
BeaconServer::InitiateBeaconsPerInterface (uint16_t selfEgressIfNo, ScionAs *remoteAs,
                                              uint16_t remoteIngressIfNo)
{
  StaticInfoExtension_t staticInfoExtension;
  CreateInitialStaticInfoExtension (staticInfoExtension, selfEgressIfNo, NULL);

  GenerateBeaconAndSend (NULL, selfEgressIfNo, remoteIngressIfNo, remoteAs, staticInfoExtension);
}

void
BeaconServer::GenerateBeaconAndSend (Beacon *selectedBeacon, uint16_t selfEgressIfNo,
                                        uint16_t remoteIngressIfNo, ScionAs *remoteAs,
                                     StaticInfoExtension_t &staticInfoExtension,
                                        const OptimizationTarget *optimizationTarget,
                                     BeaconDirection beaconDirection)
{
  std::string key;
  uint16_t remoteAsNo = remoteAs->asNumber;

  Path_t newPath;
  IsdPath_t newIsdPath;
  uint16_t nextInitiationTime;
  uint16_t nextExpirationTime;

  uint64_t linkInfo;
  linkInfo = (((uint64_t) as->asNumber) << 48) | (((uint64_t) selfEgressIfNo) << 32) |
              (((uint64_t) remoteAsNo) << 16) | ((uint64_t) remoteIngressIfNo);

  if (selectedBeacon == NULL)
    {
      nextInitiationTime = now;
      nextExpirationTime = now + expirationPeriod;
      if (optimizationTarget != NULL)
        {
          key = std::string ((char *) &optimizationTarget->targetId, 2);
          if (beaconDirection == BeaconDirection::pullBased)
            {
              NS_ASSERT (optimizationTarget->targetAs != remoteAsNo);
              key = key + std::string ((char *) &optimizationTarget->targetAs, 2);
            }
        }
    }
  else
    {
      nextInitiationTime = selectedBeacon->initiationTime;
      nextExpirationTime = selectedBeacon->expirationTime;
      newPath = selectedBeacon->path;
      key = selectedBeacon->key;
      newIsdPath = selectedBeacon->isdPath;
    }

  key =
      key + std::string ((char *) &as->asNumber, 2) + std::string ((char *) &selfEgressIfNo, 2);
  newPath.push_back (linkInfo);

  if (newIsdPath.size () == 0 || newIsdPath.back () != as->isdNumber)
    {
      newIsdPath.push_back (as->isdNumber);
    }

  uint16_t initiationTime =
      (beaconDirection == BeaconDirection::pullBased) ? nextInitiationTime : 0;
  uint16_t expirationTime =
      (beaconDirection == BeaconDirection::pullBased) ? nextExpirationTime : 0;

  Beacon toDisseminateBeacon (staticInfoExtension, optimizationTarget, beaconDirection,
                                initiationTime, expirationTime, nextInitiationTime,
                                nextExpirationTime, true, false, newPath, key, newIsdPath);

  IncrementControlPlaneBytesSent (toDisseminateBeacon, selfEgressIfNo);
  remoteAs->ReceiveBeacon (toDisseminateBeacon, as->asNumber, selfEgressIfNo,
                            remoteIngressIfNo);
}

void
BeaconServer::UpdateStatePeriodic ()
{
  pullBasedRead = (pullBasedRead + 1) % 2;
  pullBasedWrite = (pullBasedWrite + 1) % 2;
  std::map<int32_t, std::unordered_map<std::string, Beacon> &> beaconContainers = {
      {1, pushBasedBeaconContainer}, {2, requestedPullBasedBeaconContainer}};

  for (auto const &beaconContainer : beaconContainers)
    {
      for (auto &theBeaconPair : beaconContainer.second)
        {
          Beacon *theBeacon = &theBeaconPair.second;

          bool wasValid = theBeacon->isValid;
          UpdateBeaconState (theBeacon);
          bool isValid = theBeacon->isValid;
          bool invalidated = wasValid && (!isValid);

          UpdateAlgorithmDataStructuresPeriodic (theBeacon, invalidated);
        }
    }
}

void
BeaconServer::InsertBeacon (Beacon &theBeacon, uint16_t dstAs, uint16_t senderAs,
                             uint16_t remoteEgressIf, uint16_t localIngressIf, bool pathExists,
                             bool existingPathValid, Beacon *beaconToReplace)
{
  if (theBeacon.beaconDirection == BeaconDirection::pullBased)
    {
      auto &beaconContainer =
          (ORIGINATOR (theBeacon) == as->asNumber)
              ? requestedPullBasedBeaconContainer
                                  : nonRequestedPullBasedBeaconContainer.at (pullBasedWrite);
      NS_ASSERT (beaconContainer.find (theBeacon.key) == beaconContainer.end ());
      beaconContainer.insert (std::make_pair (theBeacon.key, theBeacon));
      Beacon *toInsertBeacon = &beaconContainer.at (theBeacon.key);

      if (ORIGINATOR (theBeacon) == as->asNumber)
        {
          IncrementNextRoundValidBeaconsCount (dstAs);
        }

      InsertToAlgorithmDataStructures (toInsertBeacon, senderAs, remoteEgressIf, localIngressIf);
      return;
    }

  if (pathExists)
    {
      beaconToReplace->nextInitiationTime = theBeacon.nextInitiationTime;
      beaconToReplace->nextExpirationTime = theBeacon.nextExpirationTime;
      beaconToReplace->isNew = true;

      if (!existingPathValid)
        {
          IncrementNextRoundValidBeaconsCount (dstAs);
          InsertToAlgorithmDataStructures (beaconToReplace, senderAs, remoteEgressIf,
                                           localIngressIf);
        }
      return;
    }
  else
    {
      IncrementNextRoundValidBeaconsCount (dstAs);

      NS_ASSERT (pushBasedBeaconContainer.find (theBeacon.key) == pushBasedBeaconContainer.end ());
      pushBasedBeaconContainer.insert (std::make_pair (theBeacon.key, theBeacon));
      Beacon *toInsertBeacon = &pushBasedBeaconContainer.at (theBeacon.key);
      uint16_t pathLen = (uint16_t) toInsertBeacon->path.size ();

      if (beaconStore.find (dstAs) != beaconStore.end () &&
          beaconStore.at (dstAs).find (pathLen) != beaconStore.at (dstAs).end ())
        {
          beaconStore.at (dstAs).at (pathLen).insert (toInsertBeacon);
        }
      else if (beaconStore.find (dstAs) != beaconStore.end () &&
               beaconStore.at (dstAs).find (pathLen) == beaconStore.at (dstAs).end ())
        {
          beaconStore.at (dstAs).insert (std::make_pair (pathLen, BeaconsWithEqualLength_t ()));
          beaconStore.at (dstAs).at (pathLen).insert (toInsertBeacon);
        }
      else
        {
          beaconStore.insert (std::make_pair (dstAs, BeaconsWithSameDstAs_t ()));
          beaconStore.at (dstAs).insert (std::make_pair (pathLen, BeaconsWithEqualLength_t ()));
          beaconStore.at (dstAs).at (pathLen).insert (toInsertBeacon);
        }

      InsertToAlgorithmDataStructures (toInsertBeacon, senderAs, remoteEgressIf, localIngressIf);
    }
}

void
BeaconServer::IncrementValidBeaconsCount (uint16_t dstAs)
{
  if (validBeaconsCountPerDstAs.find (dstAs) == validBeaconsCountPerDstAs.end ())
    {
      validBeaconsCountPerDstAs.insert (std::make_pair (dstAs, 1));
    }
  else
    {
      validBeaconsCountPerDstAs.at (dstAs)++;
    }
}

void
BeaconServer::IncrementNextRoundValidBeaconsCount (uint16_t dstAs)
{
  if (nextRoundValidBeaconsCountPerDstAs.find (dstAs) == nextRoundValidBeaconsCountPerDstAs.end ())
    {
      nextRoundValidBeaconsCountPerDstAs.insert (std::make_pair (dstAs, 1));
    }
  else
    {
      nextRoundValidBeaconsCountPerDstAs.at (dstAs)++;
    }
}

void
BeaconServer::DeleteBeacon (Beacon *toBeRemovedBeacon, Ld_t replacementKey, uint16_t dstAs)
{
  if (toBeRemovedBeacon->beaconDirection == BeaconDirection::pushBased)
    {
      NS_ASSERT (beaconStore.find (dstAs) != beaconStore.end ());
      NS_ASSERT (beaconStore.at (dstAs).find (toBeRemovedBeacon->path.size ()) !=
                 beaconStore.at (dstAs).end ());
      NS_ASSERT (
          beaconStore.at (dstAs)
                     .at (toBeRemovedBeacon->path.size ())
                     .find (toBeRemovedBeacon) !=
          beaconStore.at (dstAs).at (toBeRemovedBeacon->path.size ()).end ());

      beaconStore.at (dstAs)
          .at (toBeRemovedBeacon->path.size ())
          .erase (toBeRemovedBeacon);
      if (beaconStore.at (dstAs).at (toBeRemovedBeacon->path.size ()).empty ())
        {
          beaconStore.at (dstAs).erase (toBeRemovedBeacon->path.size ());
        }
      if (beaconStore.at (dstAs).empty ())
        {
          beaconStore.erase (dstAs);
        }
    }

  if (toBeRemovedBeacon->beaconDirection == BeaconDirection::pushBased ||
      (toBeRemovedBeacon->beaconDirection == BeaconDirection::pullBased &&
       ORIGINATOR_PTR (toBeRemovedBeacon) == as->asNumber))
    {
      if (toBeRemovedBeacon->isNew)
        {
          DecrementNextRoundValidBeaconsCount (dstAs);
        }
      else if (toBeRemovedBeacon->isValid)
        {
          DecrementValidBeaconsCount (dstAs);
          DecrementNextRoundValidBeaconsCount (dstAs);
        }
    }

  auto &beaconContainer =
      toBeRemovedBeacon->beaconDirection == BeaconDirection::pushBased
                              ? pushBasedBeaconContainer
          : ((ORIGINATOR_PTR (toBeRemovedBeacon) == as->asNumber)
                 ? requestedPullBasedBeaconContainer
                                     : nonRequestedPullBasedBeaconContainer.at (pullBasedWrite));

  NS_ASSERT (beaconContainer.find (toBeRemovedBeacon->key) != beaconContainer.end ());

  DeleteFromAlgorithmDataStructures (toBeRemovedBeacon, replacementKey);
  beaconContainer.erase (toBeRemovedBeacon->key);
}

void
BeaconServer::DecrementValidBeaconsCount (uint16_t dstAs)
{
  NS_ASSERT (validBeaconsCountPerDstAs.find (dstAs) != validBeaconsCountPerDstAs.end () &&
             validBeaconsCountPerDstAs.at (dstAs) > 0);
  validBeaconsCountPerDstAs.at (dstAs)--;
  if (validBeaconsCountPerDstAs.at (dstAs) == 0)
    {
      validBeaconsCountPerDstAs.erase (dstAs);
    }
}

void
BeaconServer::DecrementNextRoundValidBeaconsCount (uint16_t dstAs)
{
  NS_ASSERT (nextRoundValidBeaconsCountPerDstAs.find (dstAs) !=
                 nextRoundValidBeaconsCountPerDstAs.end () &&
             nextRoundValidBeaconsCountPerDstAs.at (dstAs) > 0);
  nextRoundValidBeaconsCountPerDstAs.at (dstAs)--;
  if (nextRoundValidBeaconsCountPerDstAs.at (dstAs) == 0)
    {
      nextRoundValidBeaconsCountPerDstAs.erase (dstAs);
    }
}

void
BeaconServer::IncrementControlPlaneBytesSent (Beacon &theBeacon, uint16_t interface)
{
  beaconsSentPerInterface.at (interface)++;
  beaconsSentPerInterfacePerPeriod.at (now).at (interface)++;
  bytesSentPerInterfacePerPeriod.at (now).at (interface) +=
      (BEACON_HEADER_SIZE + BEACON_HOP_SIZE * theBeacon.path.size ());

  auto dstAs = DST_AS (theBeacon);

  auto &countersPerDst = beaconsSentPerDstPerInterface.at (interface);
  if (countersPerDst.find (dstAs) == countersPerDst.end ())
    {
      countersPerDst.insert (std::make_pair (dstAs, 0));
    }
  countersPerDst.at (dstAs)++;

  if (theBeacon.optimizationTarget != NULL)
    {
      auto &countersPerOpt = theBeacon.beaconDirection == BeaconDirection::pushBased
                                 ? pushBasedBeaconsSentPerOptPerInterface.at (interface)
                                   : pullBasedBeaconsSentPerOptPerInterface.at (interface);
      if (countersPerOpt.find (theBeacon.optimizationTarget) == countersPerOpt.end ())
        {
          countersPerOpt.insert (std::make_pair (theBeacon.optimizationTarget, 0));
        }
      countersPerOpt.at (theBeacon.optimizationTarget)++;
    }
}

void
BeaconServer::ReceiveBeacon (Beacon &receivedBeacon, uint16_t senderAs, uint16_t remoteIf,
                             uint16_t localIf)
{
  uint16_t dstAs = DST_AS (receivedBeacon);

  NS_ASSERT (receivedBeacon.beaconDirection == BeaconDirection::pullBased ||
             receivedBeacon.optimizationTarget == NULL ||
             ORIGINATOR (receivedBeacon) == receivedBeacon.optimizationTarget->targetAs);
  bool toImport;
  bool pathExists;
  bool existingPathValid;
  Beacon *beaconToReplace;
  Ld_t replacementKey;

  std::tie (toImport, pathExists, existingPathValid, beaconToReplace, replacementKey) =
      ImportPolicy (receivedBeacon, senderAs, remoteIf, localIf, now);

  NS_ASSERT (receivedBeacon.beaconDirection != BeaconDirection::pushBased ||
             nextRoundValidBeaconsCountPerDstAs.find (dstAs) !=
                 nextRoundValidBeaconsCountPerDstAs.end () ||
             toImport);

  if (!toImport)
    {
      return;
    }

  if (!pathExists && beaconToReplace != NULL)
    {
      NS_ASSERT (DST_AS_PTR (beaconToReplace) == DST_AS (receivedBeacon));
      DeleteBeacon (beaconToReplace, replacementKey, dstAs);
    }

  InsertBeacon (receivedBeacon, dstAs, senderAs, remoteIf, localIf, pathExists, existingPathValid,
                beaconToReplace);
}

std::tuple<bool, bool, bool, Beacon *, Ld_t>
BeaconServer::ImportPolicy (Beacon &theBeacon, uint16_t senderAs, uint16_t remoteEgressIfNo,
                             uint16_t selfIngressIfNo, uint16_t now)
{
  if (theBeacon.beaconDirection == BeaconDirection::pushBased)
    {
      if (pushBasedBeaconContainer.find (theBeacon.key) != pushBasedBeaconContainer.end ())
        {
          Beacon *existingBeacon = &pushBasedBeaconContainer.at (theBeacon.key);
          NS_ASSERT (existingBeacon->beaconDirection == BeaconDirection::pushBased);
          NS_ASSERT (ORIGINATOR_PTR (existingBeacon) == ORIGINATOR (theBeacon));
          if (!existingBeacon->isValid)
            {
              return std::tuple<bool, bool, bool, Beacon *, Ld_t> (true, true, false, existingBeacon,
                                                                 0);
            }
          return std::tuple<bool, bool, bool, Beacon *, Ld_t> (true, true, true, existingBeacon, 0);
        }

      if (theBeacon.path.size () == 1)
        {
          return std::tuple<bool, bool, bool, Beacon *, Ld_t> (true, false, false, NULL, 0);
        }
    }

  return AlgSpecificImportPolicy (theBeacon, senderAs, remoteEgressIfNo, selfIngressIfNo, now);
}

void
BeaconServer::UpdateStateBeforeBeaconing ()
{
  now = (uint16_t) Simulator::Now ().ToInteger (Time::MIN);
  nextPeriod = now + (uint16_t) beaconingPeriod.ToInteger (Time::MIN);
  bytesSentPerInterfacePerPeriod.insert (
      std::make_pair (now, std::vector<uint32_t> (as->GetNDevices (), 0)));
  beaconsSentPerInterfacePerPeriod.insert (
      std::make_pair (now, std::vector<uint32_t> (as->GetNDevices (), 0)));
}

const uint16_t
BeaconServer::GetCurrentTime () const
{
  return now;
}

void
BeaconServer::RegisterToLocalPathServer ()
{
  std::map<int32_t, std::unordered_map<std::string, Beacon> &> beaconContainers = {
      {1, pushBasedBeaconContainer}, {2, requestedPullBasedBeaconContainer}};

  for (auto const &beaconContainer : beaconContainers)
    {
      for (auto const &[key, theBeacon] : beaconContainer.second)
        {
          if (theBeacon.isNew)
            {
              PathSegment pathSegment;
              if (theBeacon.beaconDirection == BeaconDirection::pushBased)
                {
                  theBeacon.ExtractPathSegmentFromPushBasedBeacon (pathSegment);
                }
              else
                {
                  theBeacon.ExtractPathSegmentFromPullBasedBeacon (pathSegment);
                }

              if (dynamic_cast<ScionCoreAs *> (as) != NULL)
                {
                  as->GetPathServer ()->RegisterCorePathSegment (pathSegment, key);
                }
              else
                {
                  as->GetPathServer ()->RegisterUpPathSegment (pathSegment, key);
                }
            }
        }
    }
}

std::pair<Ld_t, Ld_t>
BeaconServer::CalculateFinalDiversityScores (Beacon *theBeacon)
{
  Ld_t asLevelDiversityScore = 0;
  Ld_t linkLevelDiversityScore = 0;
  int32_t counter = 0;

  uint16_t dstAs = DST_AS_PTR (theBeacon);
  auto const &equalDstAsBeacons = beaconStore.at (dstAs);
  for (auto const &lenBeaconsPair : equalDstAsBeacons)
    {
      auto const &beacons = lenBeaconsPair.second;
      for (auto const &currBeacon : beacons)
        {
          if (currBeacon != theBeacon)
            {
              asLevelDiversityScore +=
                  AsLevelJaccardDistanceBetweenTwoPaths (theBeacon, currBeacon);
              linkLevelDiversityScore +=
                  LinkLevelJaccardDistanceBetweenTwoPaths (theBeacon, currBeacon);
              counter++;
            }
        }
    }
  return (
      std::make_pair (asLevelDiversityScore / counter, linkLevelDiversityScore / counter));
}

const std::vector<std::vector<Ld_t>> &
BeaconServer::GetIntraAsEnergies () const
{
  return intraAsEnergies;
}

float
BeaconServer::GetDirtyEnergyRatio () const
{
  return dirtyEnergyRatio;
}

float
BeaconServer::GetSunEnergyRatio () const
{
  return sunEnergyRatio;
}

const std::unordered_map<uint16_t, BeaconsWithSameDstAs_t> &
BeaconServer::GetBeaconStore () const
{
  return beaconStore;
}

const std::unordered_map<std::string, Beacon> &
BeaconServer::GetPathMapToBeacon () const
{
  return pushBasedBeaconContainer;
}

const std::unordered_map<uint16_t, uint32_t> &
BeaconServer::GetValidBeaconsCountPerDstAs () const
{
  return validBeaconsCountPerDstAs;
}

const std::unordered_map<uint16_t, uint32_t> &
BeaconServer::GetNextRoundValidBeaconsCountPerDstAs () const
{
  return nextRoundValidBeaconsCountPerDstAs;
}

const std::unordered_map<uint16_t, std::vector<uint32_t>> &
BeaconServer::GetBytesSentPerInterfacePerPeriod () const
{
  return bytesSentPerInterfacePerPeriod;
}

const std::vector<std::unordered_map<uint16_t, uint32_t>> &
BeaconServer::GetBeaconsSentPerDstPerInterfacePerPeriod () const
{
  return beaconsSentPerDstPerInterface;
}
const std::vector<std::unordered_map<const OptimizationTarget *, uint32_t>> &
BeaconServer::GetPushBasedBeaconsSentPerOptPerInterfacePerPeriod () const
{
  return pushBasedBeaconsSentPerOptPerInterface;
}
const std::vector<std::unordered_map<const OptimizationTarget *, uint32_t>> &
BeaconServer::GetPullBasedBeaconsSentPerOptPerInterfacePerPeriod () const
{
  return pullBasedBeaconsSentPerOptPerInterface;
}

const std::unordered_map<uint16_t, std::vector<uint32_t>> &
BeaconServer::GetBeaconsSentPerInterfacePerPeriod () const
{
  return beaconsSentPerInterfacePerPeriod;
}

const std::vector<uint64_t> &
BeaconServer::GetBeaconsSentPerInterface () const
{
  return beaconsSentPerInterface;
}

void
BeaconServer::ReadBeacons ()
{
  if (fileToReadBeacons == "none")
    {
      return;
    }

  nlohmann::json beaconsJson;
  std::ifstream file (fileToReadBeacons);
  file >> beaconsJson;
  file.close ();

  for (auto const &beaconJson : beaconsJson)
    {
      StaticInfoExtension_t staticInfoExtension;
      Path_t thePath = beaconJson["path"].get<std::vector<uint64_t>> ();
      IsdPath_t theIsdPath = beaconJson["isd_path"].get<std::vector<uint16_t>> ();
      std::vector<uint16_t> keyV = beaconJson["key"].get<std::vector<uint16_t>> ();
      std::string key = std::string (keyV.begin (), keyV.end ());
      pushBasedBeaconContainer.insert (std::make_pair (
          key, Beacon (staticInfoExtension, NULL, BeaconDirection::pushBased,
                                       beaconJson["initiation_time"], beaconJson["expiration_time"],
                                       beaconJson["initiation_time"], beaconJson["expiration_time"], false, true, thePath, key, theIsdPath)));

      Beacon *toInsertBeacon = &pushBasedBeaconContainer.at (key);
      uint16_t pathLen = (uint16_t) toInsertBeacon->path.size ();
      uint16_t dstAs = DST_AS_PTR (toInsertBeacon);

      if (beaconStore.find (dstAs) != beaconStore.end () &&
          beaconStore.at (dstAs).find (pathLen) != beaconStore.at (dstAs).end ())
        {
          beaconStore.at (dstAs).at (pathLen).insert (toInsertBeacon);
        }
      else if (beaconStore.find (dstAs) != beaconStore.end () &&
               beaconStore.at (dstAs).find (pathLen) == beaconStore.at (dstAs).end ())
        {
          beaconStore.at (dstAs).insert (std::make_pair (pathLen, BeaconsWithEqualLength_t ()));
          beaconStore.at (dstAs).at (pathLen).insert (toInsertBeacon);
        }
      else
        {
          beaconStore.insert (std::make_pair (dstAs, BeaconsWithSameDstAs_t ()));
          beaconStore.at (dstAs).insert (std::make_pair (pathLen, BeaconsWithEqualLength_t ()));
          beaconStore.at (dstAs).at (pathLen).insert (toInsertBeacon);
        }

      IncrementValidBeaconsCount (dstAs);
      IncrementNextRoundValidBeaconsCount (dstAs);
    }
}

void
BeaconServer::WriteBeacons ()
{
  if (fileToWriteBeacons == "none")
    {
      return;
    }

  nlohmann::json beaconsJson;
  for (auto const &[key, theBeacon] : pushBasedBeaconContainer)
    {
      nlohmann::json beaconJson;

      std::vector<uint16_t> v (key.begin (), key.end ());
      beaconJson["key"] = nlohmann::json (v);
      beaconJson["initiation_time"] = 0;
      beaconJson["expiration_time"] = 0xFFFF;
      beaconJson["direction"] = "push";
      beaconJson["path"] = nlohmann::json (theBeacon.path);
      beaconJson["isd_path"] = nlohmann::json (theBeacon.isdPath);
      beaconsJson.push_back (beaconJson);
    }

  std::ofstream file (fileToWriteBeacons);
  file << beaconsJson.dump ();
  file.close ();
}

void
ReadBr2BrEnergy (NodeContainer asNodes, std::map<int32_t, uint16_t> realToAliasAsNo,
                 const YAML::Node &config)
{
  std::ifstream energyFile (config["beacon_service"]["br_br_energy_file"].as<std::string> ());
  std::string line;

  int counter = 0;
  while (getline (energyFile, line))
    {
      std::vector<std::string> fields;
      fields = Split (line, '\t', fields);

      int asNo = std::stoi (fields[0]);

      double lat1 = std::stod (fields[1]);
      double long1 = std::stod (fields[2]);

      double lat2 = std::stod (fields[3]);
      double long2 = std::stod (fields[4]);

      double energy = std::stod (fields[5]);

      if (realToAliasAsNo.find (asNo) == realToAliasAsNo.end ())
        {
          counter++;
          continue;
        }

      uint16_t index = realToAliasAsNo.at (asNo);
      ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (index)));
      NS_ASSERT (as->asNumber == index);

      BeaconServer *beaconServer = as->GetBeaconServer ();

      if (beaconServer->intraAsEnergies.size () == 0)
        {
          beaconServer->intraAsEnergies.resize (as->GetNDevices ());
          for (uint32_t i = 0; i < as->GetNDevices (); ++i)
            {
              beaconServer->intraAsEnergies.at (i).resize (as->GetNDevices ());
            }
        }

      for (uint32_t i = 0; i < as->interfacesCoordinates.size (); ++i)
        {
          std::pair<double, double> coordinates1 = as->interfacesCoordinates.at (i);
          double if1Lat = coordinates1.first;
          double if1Long = coordinates1.second;

          if (std::abs (if1Lat - lat1) < 0.001 && std::abs (if1Long - long1) < 0.001)
            {
              for (uint32_t j = 0; j < as->interfacesCoordinates.size (); ++j)
                {
                  std::pair<double, double> coordinates2 = as->interfacesCoordinates.at (j);
                  double if2Lat = coordinates2.first;
                  double if2Long = coordinates2.second;

                  if (std::abs (if2Lat - lat2) < 0.001 && std::abs (if2Long - long2) < 0.001)
                    {
                      beaconServer->intraAsEnergies.at (i).at (j) = energy;
                    }
                }
            }
        }
    }
  energyFile.close ();
  std::cout << counter << std::endl;

  for (uint32_t i = 0; i < asNodes.GetN (); ++i)
    {
      ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));
      for (uint32_t j = 0; j < as->GetBeaconServer ()->intraAsEnergies.size (); ++j)
        {
          for (uint32_t k = 0; k < as->GetBeaconServer ()->intraAsEnergies.at (j).size (); ++k)
            {
              NS_ASSERT (as->GetBeaconServer ()->intraAsEnergies.at (j).at (k) != 0);
            }
        }
    }
}
} // namespace ns3
