/**
 * @file diversity_age_based.cpp
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 * @see diversity_age_based.h
 */
#include <omp.h>

#include "ns3/point-to-point-channel.h"

#include "src/SCION/headers/beaconing/diversity_age_based.h"
#include "src/SCION/headers/scion_as.h"
#include "src/SCION/headers/utils.h"

namespace ns3 {

void
DiversityAgeBased::DoInitializations (uint32_t numASes, rapidxml::xml_node<> *xmlNode,
                                      const YAML::Node &config)
{
  BeaconServer::DoInitializations (numASes, xmlNode, config);

  sentBeacons.resize (as->GetNDevices ());
  sentBeaconsCnt.resize (numASes);

  for (uint32_t i = 0; i < as->GetNDevices (); ++i)
    {
      sentBeacons.at (i) = new std::unordered_map<Beacon *, std::pair<float, uint16_t>> ();
    }

  for (uint16_t i = 0; i < (uint16_t) numASes; ++i)
    {
      sentBeaconsCnt.at (i) = new std::unordered_map<uint16_t, uint16_t> ();
      for (auto const &neighborIfacesPair : as->interfacesPerNeighborAs)
        {
          uint16_t neighbor = neighborIfacesPair.first;
          sentBeaconsCnt.at (i)->insert (std::make_pair (neighbor, 0));
        }
    }

  for (uint32_t i = 0; i < as->neighbors.size (); ++i)
    {
      uint16_t neighborAsNo = as->neighbors.at (i).first;
      linksJointnessesOnSentPaths.insert (std::make_pair (neighborAsNo, std::vector<std::unordered_map<uint32_t, uint32_t> *> ()));
      linksJointnessesOnSentPaths.at (neighborAsNo).resize (numASes);
      linksJointnessesOnReceivedPaths.resize (numASes);
      for (uint32_t j = 0; j < numASes; ++j)
        {
          linksJointnessesOnSentPaths.at (neighborAsNo).at (j) =
              new std::unordered_map<uint32_t, uint32_t> ();
          linksJointnessesOnReceivedPaths.at (j) =
              new std::unordered_map<uint32_t, uint32_t> ();
        }
    }
}

void
DiversityAgeBased::CreateInitialStaticInfoExtension (StaticInfoExtension_t &staticInfoExtension, uint16_t selfEgressIfNo,
    const OptimizationTarget *optimizationTarget)
{
  staticInfoExtension.insert (std::make_pair (StaticInfoType::latency, 0));
}

void
DiversityAgeBased::DisseminateBeacons (NeighbourRelation relation)
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

std::tuple<bool, bool, bool, Beacon *, Ld_t>
DiversityAgeBased::AlgSpecificImportPolicy (Beacon &theBeacon, uint16_t senderAs,
                                               uint16_t remoteEgressIfNo,
                                               uint16_t selfIngressIfNo, uint16_t now)
{
  uint16_t dstAs = UPPER_16_BITS (theBeacon.path.at (0));

  if (nextRoundValidBeaconsCountPerDstAs.find (dstAs) == nextRoundValidBeaconsCountPerDstAs.end ())
    {
      return std::tuple<bool, bool, bool, Beacon *, Ld_t> (true, false, false, NULL, 0);
    }

  if (nextRoundValidBeaconsCountPerDstAs.at (dstAs) < MAX_BEACONS_TO_STORE)
    {
      return std::tuple<bool, bool, bool, Beacon *, Ld_t> (true, false, false, NULL, 0);
    }

  Ld_t rawScore = CalculateImportRawScore (theBeacon);
  Ld_t beaconAge = (Ld_t) (now - theBeacon.nextInitiationTime);
  Ld_t beaconExpPeriod = (Ld_t) (theBeacon.nextExpirationTime - theBeacon.nextInitiationTime);
  Ld_t score = std::pow (rawScore, ALPHA * (beaconAge / beaconExpPeriod));

  if (nextRoundValidBeaconsCountPerDstAs.at (dstAs) < MAX_BEACONS_TO_STORE && score > 0.9)
    {
      return std::tuple<bool, bool, bool, Beacon *, Ld_t> (true, false, false, NULL, 0);
    }

  return std::tuple<bool, bool, bool, Beacon *, Ld_t> (false, false, false, NULL, 0);
}

void
DiversityAgeBased::DeleteFromAlgorithmDataStructures (Beacon *theBeacon, Ld_t replacementKey)
{
  DecLinksJointnessesOnReceivedPaths (theBeacon, UPPER_16_BITS (theBeacon->path.at (0)));
}

void
DiversityAgeBased::InsertToAlgorithmDataStructures (Beacon *theBeacon, uint16_t senderAs,
                                                        uint16_t remoteEgressIfNo,
                                                        uint16_t selfIngressIfNo)
{
  IncLinksJointnessOnReceivedPaths (UPPER_16_BITS (theBeacon->path.at (0)), theBeacon);
}

void
DiversityAgeBased::UpdateAlgorithmDataStructuresPeriodic (Beacon *theBeacon, bool invalidated)
{
  uint16_t dstAs = UPPER_16_BITS (theBeacon->path.at (0));
  RemoveInvalidSentBeacons (theBeacon, dstAs);

  if (invalidated)
    {
      DecLinksJointnessesOnReceivedPaths (theBeacon, dstAs);
    }
}

std::multimap<Ld_t, std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *, StaticInfoExtension_t>>
DiversityAgeBased::SelectBeaconsToDisseminatePerDstPerNbr (
    uint16_t remoteAsNo, uint16_t dstAsNo,
    const BeaconsWithSameDstAs_t &beaconsToTheDstAs)
{
  std::multimap<Ld_t, std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *, StaticInfoExtension_t>>
      scoreMapToBeaconAndMetadata;
  std::map<std::pair<Beacon *, uint16_t>, std::pair<Ld_t, Ld_t>> validCandidates;

  Ld_t maxScore = 0.0;
  Ld_t maxScoreRawScore = 0.0;
  Beacon *maxScoreBeacon = NULL;
  uint16_t maxScoreIface = 0;

  uint16_t remoteIngressIfNo;
  ScionAs *remoteAs =
      as->GetRemoteAsInfo (as->interfacesPerNeighborAs.at (remoteAsNo).at (0)).second;

  uint32_t minNoPathsToSend = (20 * as->interfacesPerNeighborAs.at (remoteAsNo).size ()) /
                                  remoteAs->interfacesCoordinates.size ();

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
              Ld_t rawScore = 0.0;
              Ld_t score = 0.0;
              bool mustBeAdded = false;
              if (PathNotSentBefore (remoteAsNo, selfEgressIfNo, theBeacon))
                {
                  rawScore = CalculateRawScore (theBeacon, dstAsNo, selfEgressIfNo, remoteAs);
                  Ld_t beaconAge = (Ld_t) (now - theBeacon->initiationTime);
                  Ld_t beaconExpPeriod =
                      (Ld_t) (theBeacon->expirationTime - theBeacon->initiationTime);
                  score = std::pow (rawScore, ALPHA * (beaconAge / beaconExpPeriod));

                  if (sentBeaconsCnt.at (dstAsNo)->at (remoteAsNo) < minNoPathsToSend &&
                      validCandidates.size () < minNoPathsToSend)
                    {
                      mustBeAdded = true;
                    }
                }
              else
                {
                  rawScore = sentBeacons.at (selfEgressIfNo)->at (theBeacon).first;
                  Ld_t sentBeaconTimeToExpiration =
                      (Ld_t) (sentBeacons.at (selfEgressIfNo)->at (theBeacon).second - now);
                  Ld_t currentBeaconTimeToExpiration = (Ld_t) (theBeacon->expirationTime - now);
                  score = std::pow (rawScore, std::pow (BETA * (sentBeaconTimeToExpiration /
                                                                currentBeaconTimeToExpiration),
                                                         GAMMA));
                }

              if (!mustBeAdded && score < SCORE_THRESHOLD)
                {
                  continue;
                }

              if (score > maxScore)
                {
                  maxScore = score;
                  maxScoreRawScore = rawScore;
                  maxScoreBeacon = theBeacon;
                  maxScoreIface = selfEgressIfNo;
                }

              validCandidates.insert (
                  std::make_pair (std::make_pair (theBeacon, selfEgressIfNo),
                                  std::make_pair (rawScore, score)));
            }
        }
    }

  bool countersChanged = false;

  for (uint32_t i = 0; i < MAX_BEACONS_TO_SEND; ++i)
    {
      if (maxScoreBeacon == NULL)
        {
          break;
        }
      else
        {
          validCandidates.erase (std::make_pair (maxScoreBeacon, maxScoreIface));

          remoteIngressIfNo = as->GetRemoteAsInfo (maxScoreIface).first;

          Ld_t latency =
              maxScoreBeacon->staticInfoExtension.at (StaticInfoType::latency) +
                       as->latenciesBetweenInterfaces.at (LOWER_16_BITS (maxScoreBeacon->path.back ()))
                           .at (maxScoreIface);

          StaticInfoExtension_t staticInfoExtension;
          staticInfoExtension.insert (std::make_pair (StaticInfoType::latency, latency));

          scoreMapToBeaconAndMetadata.insert (std::make_pair (
              maxScore,
              std::tuple<Beacon *, uint16_t, uint16_t, ScionAs *, StaticInfoExtension_t> (
                            maxScoreBeacon, maxScoreIface, remoteIngressIfNo, remoteAs,
                             staticInfoExtension)));

          if (PathNotSentBefore (remoteAsNo, maxScoreIface, maxScoreBeacon))
            {
              countersChanged = true;
              AddToSentBeacons (dstAsNo, remoteAsNo, maxScoreIface, maxScoreBeacon,
                                (float) maxScoreRawScore);
              IncLinksJointnessOnSentPaths (dstAsNo, remoteAsNo, maxScoreIface, maxScoreBeacon);
            }
          else
            {
              countersChanged = false;
              UpdateSentBeaconTimer (remoteAsNo, maxScoreIface, maxScoreBeacon);
            }
        }

      maxScore = 0.0;
      maxScoreRawScore = 0.0;
      maxScoreBeacon = NULL;
      maxScoreIface = 0;

      for (auto const &candidate : validCandidates)
        {
          Beacon *theBeacon = candidate.first.first;
          uint16_t selfEgressIfNo = candidate.first.second;
          Ld_t score = 0.0;
          Ld_t rawScore = 0.0;

          bool mustBeSent = PathNotSentBefore (remoteAsNo, selfEgressIfNo, theBeacon) &&
                              sentBeaconsCnt.at (dstAsNo)->at (remoteAsNo) < minNoPathsToSend;

          if (!countersChanged ||
              !PathNotSentBefore (remoteAsNo, selfEgressIfNo, theBeacon))
            {
              rawScore = candidate.second.first;
              score = candidate.second.second;
            }
          else
            {
              rawScore = CalculateRawScore (theBeacon, dstAsNo, selfEgressIfNo, remoteAs);
              Ld_t beaconAge = (Ld_t) (now - theBeacon->initiationTime);
              Ld_t beaconExpPeriod =
                  (Ld_t) (theBeacon->expirationTime - theBeacon->initiationTime);
              score = std::pow (rawScore, ALPHA * (beaconAge / beaconExpPeriod));
              validCandidates.at (std::make_pair (theBeacon, selfEgressIfNo)) =
                  std::make_pair (rawScore, score);
            }

          if (!mustBeSent && score < SCORE_THRESHOLD)
            {
              continue;
            }

          if (score > maxScore)
            {
              maxScore = score;
              maxScoreRawScore = rawScore;
              maxScoreBeacon = theBeacon;
              maxScoreIface = selfEgressIfNo;
            }
        }
    }
  return scoreMapToBeaconAndMetadata;
}

inline Ld_t
DiversityAgeBased::CalculateRawScore (Beacon *theBeacon, uint16_t dstAsNo,
                                        uint16_t selfEgressIfNo, ScionAs *remoteAs)
{
  Ld_t linkDiversityScore = CalculateLinkDiversityScoreForDissemination (
      remoteAs->asNumber, dstAsNo, selfEgressIfNo, theBeacon);
  Ld_t rawScore = linkDiversityScore * SCALING_FACTOR;
  return rawScore;
}

inline Ld_t
DiversityAgeBased::CalculateImportRawScore (Beacon &theBeacon)
{
  uint16_t dstAsNo = UPPER_16_BITS (theBeacon.path.at (0));
  Ld_t linkDiversityScore = CalculateLinkDiversityScoreForImport (dstAsNo, theBeacon);
  Ld_t rawScore = linkDiversityScore * SCALING_FACTOR;
  return rawScore;
}

void
DiversityAgeBased::UpdateSentBeaconTimer (uint16_t remoteAs, uint16_t selfEgressIfNo,
                                             Beacon *theBeacon)
{
  uint16_t newExpTime = theBeacon->expirationTime;
  float rawScore = sentBeacons.at (selfEgressIfNo)->at (theBeacon).first;
  sentBeacons.at (selfEgressIfNo)->at (theBeacon) = std::make_pair (rawScore, newExpTime);
}

void
DiversityAgeBased::IncLinksJointnessOnSentPaths (uint16_t dstAsNo, uint16_t remoteAsNo,
                                                      uint16_t selfEgressIfNo,
                                                      Beacon *theBeacon)
{
  if (theBeacon != NULL)
    {
      auto const &thePath = theBeacon->path;
      for (auto const &seg : thePath)
        {
          uint32_t link = UPPER_32_BITS (seg);
          if (linksJointnessesOnSentPaths.at (remoteAsNo).at (dstAsNo)->find (link) ==
              linksJointnessesOnSentPaths.at (remoteAsNo).at (dstAsNo)->end ())
            {
              linksJointnessesOnSentPaths.at (remoteAsNo)
                  .at (dstAsNo)
                  ->insert (std::make_pair (link, 0));
            }
          linksJointnessesOnSentPaths.at (remoteAsNo).at (dstAsNo)->at (link) =
              linksJointnessesOnSentPaths.at (remoteAsNo).at (dstAsNo)->at (link) + 1;
        }
    }

  uint32_t link = (((uint32_t) as->asNumber) << 16) | ((uint32_t) selfEgressIfNo);
  if (linksJointnessesOnSentPaths.at (remoteAsNo).at (dstAsNo)->find (link) ==
      linksJointnessesOnSentPaths.at (remoteAsNo).at (dstAsNo)->end ())
    {
      linksJointnessesOnSentPaths.at (remoteAsNo)
          .at (dstAsNo)
          ->insert (std::make_pair (link, 0));
    }
  linksJointnessesOnSentPaths.at (remoteAsNo).at (dstAsNo)->at (link) =
      linksJointnessesOnSentPaths.at (remoteAsNo).at (dstAsNo)->at (link) + 1;
}

void
DiversityAgeBased::IncLinksJointnessOnReceivedPaths (uint16_t dstAsNo, Beacon *theBeacon)
{
  auto const &thePath = theBeacon->path;
  for (auto const &seg : thePath)
    {
      uint32_t link = UPPER_32_BITS (seg);
      if (linksJointnessesOnReceivedPaths.at (dstAsNo)->find (link) ==
          linksJointnessesOnReceivedPaths.at (dstAsNo)->end ())
        {
          linksJointnessesOnReceivedPaths.at (dstAsNo)->insert (std::make_pair (link, 0));
        }
      linksJointnessesOnReceivedPaths.at (dstAsNo)->at (link) =
          linksJointnessesOnReceivedPaths.at (dstAsNo)->at (link) + 1;
    }
}

void
DiversityAgeBased::AddToSentBeacons (uint16_t dstAsNo, uint16_t remoteAs,
                                        uint16_t selfEgressIfNo, Beacon *theBeacon,
                                        float rawScore)
{
  sentBeacons.at (selfEgressIfNo)
      ->insert (
          std::make_pair (theBeacon, std::make_pair (rawScore, theBeacon->expirationTime)));
  sentBeaconsCnt.at (dstAsNo)->at (remoteAs)++;
}

Ld_t
DiversityAgeBased::CalculateLinkDiversityScoreForDissemination (uint16_t remoteAs,
                                                                     uint16_t dstAs,
                                                                     uint16_t egressIfNo,
                                                                     Beacon *theBeacon)
{
  if (linksJointnessesOnSentPaths.at (remoteAs).at (dstAs)->empty ())
    {
      return 1.0;
    }

  Ld_t addOne = PathNotSentBefore (remoteAs, egressIfNo, theBeacon) ? 1.0 : 0.0;

  Ld_t jointness = 1.0;
  auto const &thePath = theBeacon->path;
  for (auto const &seg : thePath)
    {
      uint32_t link = UPPER_32_BITS (seg);
      if (linksJointnessesOnSentPaths.at (remoteAs).at (dstAs)->find (link) !=
          linksJointnessesOnSentPaths.at (remoteAs).at (dstAs)->end ())
        {
          jointness *=
              (addOne +
               1.0 * linksJointnessesOnSentPaths.at (remoteAs).at (dstAs)->at (link));
        }
    }

  uint32_t link = (((uint32_t) as->asNumber) << 16) | ((uint32_t) egressIfNo);
  if (linksJointnessesOnSentPaths.at (remoteAs).at (dstAs)->find (link) !=
      linksJointnessesOnSentPaths.at (remoteAs).at (dstAs)->end ())
    {
      jointness *=
          (addOne + 1.0 * linksJointnessesOnSentPaths.at (remoteAs).at (dstAs)->at (link));
    }

  jointness = std::pow (jointness, 1.0 / (theBeacon->path.size () + 1.0));

  if (jointness >= MAX_ACCEPTABLE_JOINTNESS)
    {
      return 0.0;
    }
  return (MAX_ACCEPTABLE_JOINTNESS - jointness) / (MAX_ACCEPTABLE_JOINTNESS - 1.0);
}

Ld_t
DiversityAgeBased::CalculateLinkDiversityScoreForImport (uint16_t dstAs, Beacon &theBeacon)
{
  if (linksJointnessesOnReceivedPaths.at (dstAs)->empty ())
    {
      return 1.0;
    }

  Ld_t jointness = 1.0;
  auto const &thePath = theBeacon.path;
  for (auto const &seg : thePath)
    {
      uint32_t link = UPPER_32_BITS (seg);
      if (linksJointnessesOnReceivedPaths.at (dstAs)->find (link) !=
          linksJointnessesOnReceivedPaths.at (dstAs)->end ())
        {
          jointness *= (1.0 + 1.0 * linksJointnessesOnReceivedPaths.at (dstAs)->at (link));
        }
    }

  jointness = std::pow (jointness, 1.0 / (theBeacon.path.size () + 1.0));

  if (jointness >= MAX_ACCEPTABLE_JOINTNESS)
    {
      return 0.0;
    }
  return (MAX_ACCEPTABLE_JOINTNESS - jointness) / (MAX_ACCEPTABLE_JOINTNESS - 1.0);
}

bool
DiversityAgeBased::PathNotSentBefore (uint16_t remoteAs, uint16_t selfEgressIfNo,
                                         Beacon *theBeacon)
{
  if (sentBeacons.at (selfEgressIfNo)->find (theBeacon) == sentBeacons.at (selfEgressIfNo)->end ())
    {
      return true;
    }
  return false;
}

void
DiversityAgeBased::RemoveInvalidSentBeacons (Beacon *theBeacon, uint16_t dstAs)
{
  for (uint32_t i = 0; i < as->GetNDevices (); ++i)
    {
      uint16_t remoteAsNo = as->interfaceToNeighborMap.at (i);
      if (sentBeacons.at (i)->find (theBeacon) == sentBeacons.at (i)->end ())
        {
          continue;
        }

      if (sentBeacons.at (i)->at (theBeacon).second <= nextPeriod)
        {
          sentBeacons.at (i)->erase (theBeacon);
          sentBeaconsCnt.at (dstAs)->at (as->interfaceToNeighborMap.at (i))--;
          DecLinksJointnessesOnSentPaths (theBeacon, dstAs, remoteAsNo, i);
        }
    }
}

void
DiversityAgeBased::DecLinksJointnessesOnSentPaths (Beacon *theBeacon, uint16_t dstAs,
                                                        uint16_t remoteAsNo,
                                                        uint16_t selfEgressIf)
{
  auto const &thePath = theBeacon->path;
  for (auto const &seg : thePath)
    {
      uint32_t link = UPPER_32_BITS (seg);
      linksJointnessesOnSentPaths.at (remoteAsNo).at (dstAs)->at (link) =
          linksJointnessesOnSentPaths.at (remoteAsNo).at (dstAs)->at (link) - 1;
      if (linksJointnessesOnSentPaths.at (remoteAsNo).at (dstAs)->at (link) == 0)
        {
          linksJointnessesOnSentPaths.at (remoteAsNo).at (dstAs)->erase (link);
        }
    }

  uint32_t link = (((uint32_t) as->asNumber) << 16) | ((uint32_t) selfEgressIf);

  linksJointnessesOnSentPaths.at (remoteAsNo).at (dstAs)->at (link) =
      linksJointnessesOnSentPaths.at (remoteAsNo).at (dstAs)->at (link) - 1;
  if (linksJointnessesOnSentPaths.at (remoteAsNo).at (dstAs)->at (link) == 0)
    {
      linksJointnessesOnSentPaths.at (remoteAsNo).at (dstAs)->erase (link);
    }
}

void
DiversityAgeBased::DecLinksJointnessesOnReceivedPaths (Beacon *theBeacon, uint16_t dstAs)
{
  auto const &thePath = theBeacon->path;
  for (auto const &seg : thePath)
    {
      uint32_t link = UPPER_32_BITS (seg);
      linksJointnessesOnReceivedPaths.at (dstAs)->at (link) =
          linksJointnessesOnReceivedPaths.at (dstAs)->at (link) - 1;
      if (linksJointnessesOnReceivedPaths.at (dstAs)->at (link) == 0)
        {
          linksJointnessesOnReceivedPaths.at (dstAs)->erase (link);
        }
    }
}
} // namespace ns3
