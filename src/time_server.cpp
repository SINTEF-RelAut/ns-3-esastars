//
// Created by seyedali on 30.08.21.
//

#include <omp.h>

#include "ns3/log.h"

#include "src/SCION/headers/externs.h"
#include "src/SCION/headers/json.hpp"
#include "src/SCION/headers/run_parallel_events.h"
#include "src/SCION/headers/schedule_periodic_events.h"
#include "src/SCION/headers/scion_core_as.h"
#include "src/SCION/headers/time_server.h"
#include "src/SCION/headers/utils.h"

namespace ns3 {
NS_LOG_COMPONENT_DEFINE ("TimeServer");

void
TimeServer::RequestSetOfAllCoreAsesFromPathServer ()
{
  AdvanceLocalTime ();

  NS_LOG_FUNCTION ("TimeSrv at " << isdNumber << ":" << asNumber
                                 << " sent req for all core ASes to PthSrv");

  PayloadType payloadType = PayloadType::reqForListOfAllCoreAses;
  Payload payload;
  ScionPacket *packet = CreateScionPacket (payload, payloadType, iaAddr, 1, 0);

  SendScionPacket (packet);
}

void
TimeServer::ProcessReceivedPacket (uint16_t localIf, ScionPacket *packet, Time receiveTime)
{
  ScionHost::ProcessReceivedPacket (localIf, packet, receiveTime);

  if (packet->payloadType == PayloadType::listOfAllCoreAses)
    {
      NS_LOG_FUNCTION ("TimeSrv at " << isdNumber << ":" << asNumber
                                     << " rcv all core ASes from PthSrv");
      ReceiveSetOfAllCoreAsesFromPathServer (packet);
      packet->packetOriginator->DestroyScionPacket (packet);
      return;
    }

  if (packet->payloadType == PayloadType::broadcastListOfAllCoreAses)
    {
      NS_LOG_FUNCTION ("TimeSrv at "
                       << isdNumber << ":" << asNumber << " rcv all core ASes from other TimeSrv "
                       << GET_ISDN (packet->srcIa) << ":" << GET_ASN (packet->srcIa));
      ReceiveSetOfAllCoreAsesFromOtherTimeServer (packet);
      packet->packetOriginator->DestroyScionPacket (packet);
      return;
    }

  if (packet->payloadType == PayloadType::ntpReq)
    {
      NS_LOG_FUNCTION ("TimeSrv at " << isdNumber << ":" << asNumber << " rcv ntp req from "
                                     << GET_ISDN (packet->srcIa) << ":"
                                     << GET_ASN (packet->srcIa));
      ReceiveNtpReqFromPeer (packet, receiveTime);
      return;
    }

  if (packet->payloadType == PayloadType::ntpResp)
    {
      NS_LOG_FUNCTION ("TimeSrv at " << isdNumber << ":" << asNumber << " rcv ntp resp from "
                                     << GET_ISDN (packet->srcIa) << ":"
                                     << GET_ASN (packet->srcIa));
      ReceiveNtpResFromPeer (packet, receiveTime);
      DestroyScionPacket (packet);
      return;
    }
}

void
TimeServer::ReceiveSetOfAllCoreAsesFromPathServer (ScionPacket *packet)
{
  if (*packet->payload.listOfAllAses.setOfAllAses != setOfAllCoreAses)
    {
      std::vector<Ia_t> v1 (setOfAllCoreAses.begin (), setOfAllCoreAses.end ());
      std::vector<Ia_t> v2 (packet->payload.listOfAllAses.setOfAllAses->begin (),
                            packet->payload.listOfAllAses.setOfAllAses->end ());
      std::vector<Ia_t> result (v1.size () + v2.size ());
      std::vector<Ia_t>::iterator it =
          std::set_union (v1.begin (), v1.end (), v2.begin (), v2.end (), result.begin ());
      std::set<Ia_t> result_set (result.begin (), it);
      setOfAllCoreAses = result_set;

      RequestForPathsToAllCoreAses ();
      //Simulator::Schedule(MilliSeconds(350), &TimeServer::SendSetOfAllCoreAsesToNeighbors, this);

      if (parallelScheduler)
        {
          if (readDisjointPaths == ReadOrWriteDisjointPaths::w ||
              readDisjointPaths == ReadOrWriteDisjointPaths::noRNoW)
            {
              Simulator::Schedule (MilliSeconds (300),
                                   &RunParallelEvents<void (TimeServer::*) (), TimeServer *>,
                                   localAddress, &TimeServer::ConstructSetOfSelectedPaths);
              if (readDisjointPaths == ReadOrWriteDisjointPaths::w)
                {
                  Simulator::Schedule (MilliSeconds (310),
                                       &RunParallelEvents<void (TimeServer::*) (), TimeServer *>,
                                       localAddress, &TimeServer::WriteSetOfDisjointPaths);
                }
            }
          else if (readDisjointPaths == ReadOrWriteDisjointPaths::r)
            {
              if (setOfSelectedPaths.empty ())
                {
                  Simulator::Schedule (MilliSeconds (310),
                                       &RunParallelEvents<void (TimeServer::*) (), TimeServer *>,
                                       localAddress, &TimeServer::ReadSetOfDisjointPaths);
                }
            }
        }
    }
}

void
TimeServer::ConstructSetOfSelectedPaths ()
{
  setOfSelectedPaths.clear ();
  if (pathSelection == "disjoint")
    {
      ConstructSetOfMostDisjointPaths ();
      return;
    }

  if (pathSelection == "short")
    {
      ConstructSetOfShortestPaths ();
      return;
    }

  if (pathSelection == "random")
    {
      ConstructSetOfRandomPaths ();
      return;
    }
}

void
TimeServer::ConstructSetOfMostDisjointPaths ()
{
  NS_LOG_FUNCTION ("TimeSrv at " << isdNumber << ":" << asNumber << " constructing disjoint paths");
  for (auto const &dstIa : setOfAllCoreAses)
    {
      if (dstIa == iaAddr)
        {
          continue;
        }

      setOfSelectedPaths.insert (
          std::make_pair (dstIa, std::unordered_set<const PathSegment *> ()));
      auto &setOfSelectedPathsPerDstIa = setOfSelectedPaths.at (dstIa);

      std::unordered_map<Ia_t, uint32_t> number_of_paths_per_hop_ia;
      auto const &pathSegments = *cachedCorePathSegments.at (dstIa)->at (iaAddr);

      while (setOfSelectedPathsPerDstIa.size () < numberOfPathsToUseForGlobalSync &&
             setOfSelectedPathsPerDstIa.size () < pathSegments.size ())
        {
          const PathSegment *bestPath = NULL;
          uint64_t bestPathScore = std::numeric_limits<uint64_t>::max ();
          uint32_t bestPathLen = std::numeric_limits<uint32_t>::max ();

          uint32_t minLen = pathSegments.begin ()->first;
          for (auto const &[pathLen, pathSeg] : pathSegments)
            {
              if (minLen == 2 && pathLen > 2)
                {
                  break;
                }
              if (setOfSelectedPathsPerDstIa.find (pathSeg) != setOfSelectedPathsPerDstIa.end ())
                {
                  continue;
                }

              uint64_t pathSegScore = 1;
              for (uint32_t j = 1; j < (uint32_t) pathLen - 1; ++j)
                {
                  uint64_t hop = pathSeg->hops.at (j);
                  Ia_t hopIa = GET_HOP_IA (hop);

                  if (number_of_paths_per_hop_ia.find (hopIa) != number_of_paths_per_hop_ia.end ())
                    {
                      pathSegScore *= (number_of_paths_per_hop_ia.at (hopIa) + 1);
                    }
                }

              if (pathSegScore == 1)
                {
                  bestPath = pathSeg;
                  bestPathLen = pathLen;
                  break;
                }

              if (pathSegScore < bestPathScore)
                {
                  bestPath = pathSeg;
                  bestPathScore = pathSegScore;
                  bestPathLen = pathLen;
                }
              else if (pathSegScore == bestPathScore && pathLen < bestPathLen)
                {
                  bestPath = pathSeg;
                  bestPathScore = pathSegScore;
                  bestPathLen = pathLen;
                }
            }

          if (bestPath == NULL)
            {
              break;
            }

          setOfSelectedPathsPerDstIa.insert (bestPath);

          for (uint32_t j = 1; j < bestPathLen - 1; ++j)
            {
              uint64_t hop = bestPath->hops.at (j);
              Ia_t hopIa = GET_HOP_AS_ING (hop);
              if (number_of_paths_per_hop_ia.find (hopIa) == number_of_paths_per_hop_ia.end ())
                {
                  number_of_paths_per_hop_ia.insert (std::make_pair (hopIa, 0));
                }
              number_of_paths_per_hop_ia.at (hopIa)++;
            }
        }
    }

  NS_LOG_FUNCTION ("TimeSrv at " << isdNumber << ":" << asNumber
                                 << " FINISHED constructing disjoint paths");
}

void
TimeServer::ConstructSetOfShortestPaths ()
{
  NS_LOG_FUNCTION ("TimeSrv at " << isdNumber << ":" << asNumber << " constructing shortest paths");
  for (auto const &dstIa : setOfAllCoreAses)
    {
      if (dstIa == iaAddr)
        {
          continue;
        }

      setOfSelectedPaths.insert (
          std::make_pair (dstIa, std::unordered_set<const PathSegment *> ()));
      auto &setOfSelectedPathsPerDstIa = setOfSelectedPaths.at (dstIa);

      auto const &pathSegments = *cachedCorePathSegments.at (dstIa)->at (iaAddr);
      uint32_t minLen = pathSegments.begin ()->first;
      for (auto const &[pathLen, pathSeg] : pathSegments)
        {
          if (minLen == 2 && pathLen > 2)
            {
              break;
            }
          if (setOfSelectedPathsPerDstIa.size () >= numberOfPathsToUseForGlobalSync)
            {
              break;
            }
          setOfSelectedPathsPerDstIa.insert (pathSeg);
        }
    }
}

void
TimeServer::ConstructSetOfRandomPaths ()
{
  NS_LOG_FUNCTION ("TimeSrv at " << isdNumber << ":" << asNumber << " constructing shortest paths");
  for (auto const &dstIa : setOfAllCoreAses)
    {
      if (dstIa == iaAddr)
        {
          continue;
        }

      setOfSelectedPaths.insert (
          std::make_pair (dstIa, std::unordered_set<const PathSegment *> ()));
      auto &setOfSelectedPathsPerDstIa = setOfSelectedPaths.at (dstIa);
      auto const &pathSegments = *cachedCorePathSegments.at (dstIa)->at (iaAddr);
      uint32_t minLen = pathSegments.begin ()->first;

      std::vector<uint32_t> selectedIndices;
      for (uint32_t i = 0; i < pathSegments.size (); ++i)
        {
          selectedIndices.push_back (i);
        }

      std::shuffle (selectedIndices.begin (), selectedIndices.end (), std::random_device{});

      uint32_t j = 0;
      while (setOfSelectedPathsPerDstIa.size () < numberOfPathsToUseForGlobalSync &&
             setOfSelectedPathsPerDstIa.size () < pathSegments.size ())
        {
          auto iter = pathSegments.cbegin ();
          std::advance (iter, selectedIndices.at (j));

          if (!(iter->second->hops.size () > 2 && minLen == 2))
            {
              setOfSelectedPathsPerDstIa.insert (iter->second);
            }

          j++;
          if (j >= pathSegments.size ())
            {
              break;
            }
        }
    }
}

void
TimeServer::ReadSetOfDisjointPaths ()
{
  nlohmann::json setOfDisjointPathsJson;
  std::ifstream disjointPathsFile (setOfDisjointPathsFile);
  disjointPathsFile >> setOfDisjointPathsJson;
  disjointPathsFile.close ();

  for (auto const &[dstIaStr, pathSegsJson] : setOfDisjointPathsJson.items ())
    {
      Ia_t dstIa = std::stoi (dstIaStr);

      setOfSelectedPaths.insert (
          std::make_pair (dstIa, std::unordered_set<const PathSegment *> ()));

      for (auto const &pathSegJson : pathSegsJson)
        {
          PathSegment *pathSeg = new PathSegment ();
          pathSeg->initiationTime = pathSegJson["initiation_time"];
          pathSeg->expirationTime = pathSegJson["expiration_time"];
          pathSeg->originator = pathSegJson["originator"];
          pathSeg->reverse = pathSegJson["reverse"];
          pathSeg->hops = pathSegJson["hops"].get<std::vector<uint64_t>> ();
          setOfSelectedPaths.at (dstIa).insert (pathSeg);
        }
    }
}

void
TimeServer::WriteSetOfDisjointPaths ()
{
  nlohmann::json setOfDisjointPathsJson;

  for (auto const &[dstIa, pathSegs] : setOfSelectedPaths)
    {
      nlohmann::json pathSegsJson;
      for (auto const &pathSeg : pathSegs)
        {
          nlohmann::json pathSegJson;
          pathSegJson["initiation_time"] = 0;
          pathSegJson["expiration_time"] = 0xFFFF;
          pathSegJson["originator"] = pathSeg->originator;
          pathSegJson["reverse"] = 0;
          pathSegJson["hops"] = nlohmann::json (pathSeg->hops);
          pathSegsJson.push_back (pathSegJson);
        }
      setOfDisjointPathsJson[std::to_string (dstIa)] = pathSegsJson;
    }

  std::ofstream disjointPathsFile (setOfDisjointPathsFile);
  disjointPathsFile << setOfDisjointPathsJson.dump ();
  disjointPathsFile.close ();
}

void
TimeServer::ReceiveSetOfAllCoreAsesFromOtherTimeServer (ScionPacket *packet)
{
  if (*packet->payload.listOfAllAses.setOfAllAses != setOfAllCoreAses)
    {
      std::vector<Ia_t> v1 (setOfAllCoreAses.begin (), setOfAllCoreAses.end ());
      std::vector<Ia_t> v2 (packet->payload.listOfAllAses.setOfAllAses->begin (),
                            packet->payload.listOfAllAses.setOfAllAses->end ());
      std::vector<Ia_t> result (v1.size () + v2.size ());
      std::vector<Ia_t>::iterator it =
          std::set_union (v1.begin (), v1.end (), v2.begin (), v2.end (), result.begin ());
      std::set<Ia_t> result_set (result.begin (), it);
      setOfAllCoreAses = result_set;

      SendSetOfAllCoreAsesToNeighbors ();
    }
}

void
TimeServer::RequestForPathsToAllCoreAses ()
{
  std::set<uint16_t> all_isds;

  for (auto const &isdAs : setOfAllCoreAses)
    {
      if (isdAs == iaAddr)
        {
          continue;
        }

      all_isds.insert (GET_ISDN (isdAs));
    }

  for (auto const &isd : all_isds)
    {
      NS_LOG_FUNCTION ("TimeSrv at " << isdNumber << ":" << asNumber
                                     << " send req for paths to isd " << isd);
      if (isd == isdNumber)
        {
          SendRequestForPathSegments (PathSegmentType::coreSeg, 0, 0);
        }
      else
        {
          SendRequestForPathSegments (PathSegmentType::coreSeg, 0, MAKE_IA (isd, 0));
        }
    }
}

void
TimeServer::SendSetOfAllCoreAsesToNeighbors ()
{
  std::set<const PathSegment *> pathsToNeighborAses;

  for (auto const &dstIaCachedPathsPair : cachedCorePathSegments)
    {
      for (auto const &[expTime, pathSeg] : *dstIaCachedPathsPair.second->at (iaAddr))
        {
          if (expTime > localTime.GetMinutes ())
            {
              if (pathSeg->hops.size () == 2)
                {
                  pathsToNeighborAses.insert (pathSeg);
                }
            }
        }
    }

  for (auto const &path : pathsToNeighborAses)
    {
      NS_LOG_FUNCTION ("TimeSrv at " << isdNumber << ":" << asNumber << " sent list of all ases to "
                                     << GET_HOP_ISD (path->hops.back ()) << ":"
                                     << GET_HOP_AS (path->hops.back ()));
      PayloadType payloadType = PayloadType::broadcastListOfAllCoreAses;

      Payload payload;
      payload.listOfAllAses.setOfAllAses = &setOfAllCoreAses;

      std::vector<const PathSegment *> thePath;
      thePath.push_back (path);

      ScionPacket *packet =
          CreateScionPacket (payload, payloadType, GET_HOP_IA (path->hops.back ()), 2,
                             setOfAllCoreAses.size () * 8, thePath);

      SendScionPacket (packet);
    }
}

void
TimeServer::AdvanceLocalTime ()
{
  Time advance = Simulator::Now () - realTimeOfLastTimeAdvance;

  if (advance == 0)
    {
      return;
    }

  int64_t driftInt = GetDrift (advance);

  Time tmpLocalTime = localTime;

  localTime += advance;
  if (driftInt < 0)
    {
      localTime -= TimeStep (std::abs (driftInt));
      NS_LOG_FUNCTION ("ia_addr: " << isdNumber << "-" << asNumber << ", local_time: "
                                   << tmpLocalTime << ", updated_local_time: " << localTime
                                   << ", last update: " << realTimeOfLastTimeAdvance
                                   << ", advance: " << advance << ", random_drift: -"
                                   << TimeStep (std::abs (driftInt)));
    }
  else
    {
      localTime += TimeStep (std::abs (driftInt));
      NS_LOG_FUNCTION ("ia_addr: " << isdNumber << "-" << asNumber << ", local_time: "
                                   << tmpLocalTime << ", updated_local_time: " << localTime
                                   << ", last update: " << realTimeOfLastTimeAdvance
                                   << ", advance: " << advance << ", random_drift: +"
                                   << TimeStep (std::abs (driftInt)));
    }

  realTimeOfLastTimeAdvance = Simulator::Now ();
}

Time
TimeServer::GetReferenceTime ()
{
  if (referenceTimeType == ReferenceTimeType::off)
    {
      return localTime;
    }

  if (referenceTimeType == ReferenceTimeType::maliciousRef)
    {
      if (maliciousOffsetInPs > 0)
        {
          return Simulator::Now () + TimeStep (maliciousOffsetInPs);
        }
      return Simulator::Now () - TimeStep (maliciousOffsetInPs);
    }

  return Simulator::Now ();
}

int64_t
TimeServer::GetDrift (Time duration)
{
  if (jitterInDrift)
    {
      Time maxDrift = GetMaxDrift (duration);
      std::random_device rd;
      std::uniform_int_distribution<int64_t> dist (-std::abs (maxDrift.GetTimeStep ()),
                                                   std::abs (maxDrift.GetTimeStep ()));
      int64_t randomDriftInt = dist (rd);
      return randomDriftInt;
    }

  double drift = (((double) constantDriftPerDay) * ((double) duration.GetTimeStep ())) /
                 ((double) Days (1).GetTimeStep ());

  return (int64_t) std::round (drift);
}

Time
TimeServer::GetMaxDrift (Time duration)
{
  double maxDrift =
      (((double) maxDriftPerDay.GetTimeStep ()) * ((double) duration.GetTimeStep ())) /
      ((double) Days (1).GetTimeStep ());
  return TimeStep ((uint64_t) std::round (maxDrift));
}

void
TimeServer::TriggerCoreTimeSyncAlgo ()
{
  AdvanceLocalTime ();

  if (algV == AlgV::v4 || synchronizationRound != 0 || algV == AlgV::localSync)
    {
      int64_t loff = GetReferenceTime ().GetTimeStep () - localTime.GetTimeStep ();
      double coefficient = (algV == AlgV::v4) ? 1.25 : 1.0;
      CorrectLocalTime (loff, timeSyncPeriod, coefficient);
    }

  if (synchronizationRound == 0 && algV != AlgV::localSync)
    {
      SendNtpReqToPeers ();
      if (parallelScheduler)
        {
          Simulator::Schedule (Time (NTP_REQ_GLOBAL_SYNC_DIFF),
                               &RunParallelEvents<void (TimeServer::*) (), TimeServer *>,
                               localAddress, &TimeServer::ContinueGlobalTimeSync);
        }
      // ********************************* Debug: To print goffsets **********************************************************
      //            Simulator::Schedule(Time(NTP_REQ_GLOBAL_SYNC_DIFF), &TimeServer::ContinueGlobalTimeSync, this);
      // *********************************************************************************************************************
    }
  synchronizationRound = (synchronizationRound + 1) % g;
}

void
TimeServer::ContinueGlobalTimeSync ()
{
  AdvanceLocalTime ();

  int32_t n = setOfAllCoreAses.size ();
  int32_t f = std::floor ((n - 1) / 3);

  int64_t loff;
  loff =
      (algV == AlgV::v4) ? 0 : (GetReferenceTime ().GetTimeStep () - localTime.GetTimeStep ());
  int64_t corr = loff;

  std::multiset<int64_t> off;
  off.insert (loff);

  for (auto const &peerIa : setOfAllCoreAses)
    {
      if (peerIa == iaAddr)
        {
          continue;
        }

      if (poff.find (peerIa) == poff.end ())
        {
          off.insert (loff);
        }
      else
        {
          int64_t medianOff = (int64_t) std::round (GetMedian (poff.at (peerIa)));
          off.insert (medianOff);
        }
    }

  auto iter1 = off.cbegin ();
  auto iter2 = off.cbegin ();
  std::advance (iter1, f);
  std::advance (iter2, n - 1 - f);

  int64_t goff = std::floor ((*iter1 + *iter2) / 2);
  int64_t doff = loff - goff;

  //***************************** Debug: To print goffsets ***************************************************************
  //        std::cout << " ************************************* " << std::endl;
  //        std::cout << "goff: " << goff / 1000000000.0 <<
  //                   ", real_time_diff: " << (Simulator::Now().GetTimeStep() - local_time.GetTimeStep()) / 1000000000.0 << std::endl;
  //        for (auto const & an_off : off) {
  //            std::cout << an_off / 1000000000.0 << std::endl;
  //        }
  //**********************************************************************************************************************

  if (std::abs (doff) > std::abs (globalCutOff.GetTimeStep ()))
    {
      if (algV == AlgV::v1 || algV == AlgV::v2)
        {
          doff = doff > 0 ? std::abs (globalCutOff.GetTimeStep ())
                          : -std::abs (globalCutOff.GetTimeStep ());
          corr = goff + doff;
        }
      else if (algV == AlgV::v3 || algV == AlgV::v4)
        {
          corr = goff;
        }
    }

  double coefficient = (algV == AlgV::v4) ? 2.5 : 1.0;
  Time duration = (algV == AlgV::v1) ? timeSyncPeriod : (g * timeSyncPeriod);
  CorrectLocalTime (corr, duration, coefficient);

  poff.clear ();

  if (pathSelection == "random")
    {
      ConstructSetOfSelectedPaths ();
    }
}

void
TimeServer::CorrectLocalTime (int64_t corr, Time duration, double coefficient)
{
  Time maxDrift = GetMaxDrift (duration);
  int64_t maxCorr = std::abs ((int64_t) std::round (coefficient * maxDrift.GetTimeStep ()));
  int64_t finalCorrAbs = (std::abs (corr) < maxCorr) ? std::abs (corr) : maxCorr;

  Time tmpLocalTime = localTime;

  if (corr > 0)
    {
      localTime += TimeStep (finalCorrAbs);
      NS_LOG_FUNCTION ("ia_addr: " << isdNumber << "-" << asNumber
                                   << ", local_time: " << tmpLocalTime
                                   << ", updated_local_time: " << localTime << ", final_corr: +"
                                   << TimeStep (finalCorrAbs) << ", max_drift: " << maxDrift
                                   << ", corr: +" << TimeStep (std::abs (corr)));
    }
  else
    {
      localTime -= TimeStep (finalCorrAbs);
      NS_LOG_FUNCTION ("ia_addr: " << isdNumber << "-" << asNumber
                                   << ", local_time: " << tmpLocalTime
                                   << ", updated_local_time: " << localTime << ", final_corr: -"
                                   << TimeStep (finalCorrAbs) << ", max_drift: " << maxDrift
                                   << ", corr: -" << TimeStep (std::abs (corr)));
    }
}

void
TimeServer::SendNtpReqToPeers ()
{
  NS_LOG_FUNCTION ("ia_addr: " << isdNumber << "-" << asNumber << ", local_time: " << localTime);

  for (auto const &peerIa : setOfAllCoreAses)
    {
      if (peerIa == iaAddr)
        {
          continue;
        }

      for (auto const &pathSeg : setOfSelectedPaths.at (peerIa))
        {
          NS_LOG_FUNCTION ("TimeSrv at " << isdNumber << ":" << asNumber << " sent ntp req to "
                                         << GET_ISDN (peerIa) << ":" << GET_ASN (peerIa));
          PayloadType payloadType = PayloadType::ntpReq;

          Payload payload;
          payload.ntpReqOrResp.t0 = localTime.GetTimeStep ();

          std::vector<const PathSegment *> thePath;
          thePath.push_back (pathSeg);

          ScionPacket *packet = CreateScionPacket (payload, payloadType, peerIa, 2,
                                                   8 + 48 /* udp + ntp*/, thePath);

          SendScionPacket (packet);
        }
    }
}

void
TimeServer::ModifyPktUponSend (ScionPacket *packet)
{
  if (packet->payloadType == PayloadType::ntpReq)
    {
      packet->payload.ntpReqOrResp.t0 = localTime.GetTimeStep ();
      return;
    }

  if (packet->payloadType == PayloadType::ntpResp)
    {
      packet->payload.ntpReqOrResp.t2 = localTime.GetTimeStep ();
      return;
    }
}

void
TimeServer::ReceiveNtpReqFromPeer (ScionPacket *packet, Time receiveTime)
{
  int64_t t0 = packet->payload.ntpReqOrResp.t0;
  NS_LOG_FUNCTION ("ia_addr: " << isdNumber << "-" << asNumber << ", local_time: " << localTime
                               << ", sender_ia: " << GET_ISDN (packet->srcIa) << "-"
                               << GET_ASN (packet->srcIa) << ", receive_time: " << receiveTime
                               << ", t0: " << (t0 < 0 ? "-" : "+") << TimeStep (std::abs (t0)));

  packet->payloadType = PayloadType::ntpResp;
  packet->payload.ntpReqOrResp.t1 = receiveTime.GetTimeStep ();
  packet->payload.ntpReqOrResp.t2 = localTime.GetTimeStep ();
  ReturnScionPacket (packet);
}

void
TimeServer::ReceiveNtpResFromPeer (ScionPacket *packet, Time receiveTime)
{
  int64_t t0 = packet->payload.ntpReqOrResp.t0;
  int64_t t1 = packet->payload.ntpReqOrResp.t1;
  int64_t t2 = packet->payload.ntpReqOrResp.t2;

  NS_LOG_FUNCTION ("TimeServ at " << isdNumber << ":" << asNumber << " RCV NTP resp from peer "
                                  << GET_ISDN (packet->srcIa) << ":" << GET_ASN (packet->srcIa)
                                  << ", local_time: " << localTime
                                  << ", t0: " << (t0 < 0 ? "-" : "+") << TimeStep (std::abs (t0))
                                  << ", t1: " << (t1 < 0 ? "-" : "+") << TimeStep (std::abs (t1))
                                  << ", t2: " << (t2 < 0 ? "-" : "+") << TimeStep (std::abs (t2))
                                  << ", t3: " << receiveTime);

  int64_t poffTmp = ((packet->payload.ntpReqOrResp.t1 - packet->payload.ntpReqOrResp.t0) +
                      (packet->payload.ntpReqOrResp.t2 - receiveTime.GetTimeStep ())) /
                     2;

  if (poff.find (packet->srcIa) == poff.end ())
    {
      poff.insert (std::make_pair (packet->srcIa, std::multiset<int64_t> ()));
    }

  poff.at (packet->srcIa).insert (poffTmp);
}

void
TimeServer::ResetTime ()
{
  realTimeOfLastTimeAdvance = Simulator::Now ();

  localTime = Simulator::Now ();

  if (maxInitialDrift.GetTimeStep () == 0)
    {
      return;
    }

  std::random_device rd;
  std::uniform_int_distribution<int64_t> dist (-std::abs (maxInitialDrift.GetTimeStep ()),
                                               std::abs (maxInitialDrift.GetTimeStep ()));
  int64_t randomDriftInt = dist (rd);

  if (randomDriftInt < 0)
    {
      localTime -= TimeStep (std::abs (randomDriftInt));
    }
  else
    {
      localTime += TimeStep (std::abs (randomDriftInt));
    }
}

void
TimeServer::CaptureLocalSnapshot ()
{
  AdvanceLocalTime ();

  if (parallelScheduler)
    {
      std::cout << "##################################### Snapshot at "
                << Simulator::Now ().GetTimeStep () << " ##################################"
                << std::endl;
    }

  if (referenceTimeType == ReferenceTimeType::maliciousRef)
    {
      std::cout << "AS m " << isdNumber << "-" << asNumber << ": " << localTime << std::endl;
    }
  else
    {
      std::cout << "AS " << isdNumber << "-" << asNumber << ", degree "
                << as->interfacesPerNeighborAs.size () << ": " << localTime << std::endl;
    }
}

void
TimeServer::CaptureOffsetDiff ()
{
  AdvanceLocalTime ();

  std::cout << "##################################### Snapshot at "
            << Simulator::Now ().GetTimeStep () << " ##################################"
            << std::endl;
  std::cout << "poff_size: " << poff.size () << std::endl;

  int32_t n = g_nodes.GetN ();
  int32_t f = std::floor ((n - 1) / 3);

  int64_t loff = GetReferenceTime ().GetTimeStep () - localTime.GetTimeStep ();

  std::multiset<int64_t> off;
  off.insert (loff);

  for (uint32_t i = 0; i < g_nodes.GetN (); ++i)
    {
      ScionAs *node = dynamic_cast<ScionAs *> (PeekPointer (g_nodes.Get (i)));
      if (node->iaAddr == iaAddr)
        {
          continue;
        }

      if (poff.find (node->iaAddr) == poff.end ())
        {
          std::cout << "NOT FOUND" << std::endl;
          continue;
        }

      dynamic_cast<TimeServer *> (node->GetHost (localAddress))->AdvanceLocalTime ();
      Time remoteLocalTime =
          dynamic_cast<TimeServer *> (node->GetHost (localAddress))->GetLocalTime ();
      int64_t timeDiff = (remoteLocalTime - localTime).GetTimeStep ();

      int64_t medianOff = (int64_t) std::round (GetMedian (poff.at (node->iaAddr)));
      off.insert (medianOff);

      for (auto const &offset : poff.at (node->iaAddr))
        {
          int64_t diff = offset - timeDiff;
          std::cout << diff << "\t";
        }

      std::cout << std::endl;
    }

  auto iter1 = off.cbegin ();
  auto iter2 = off.cbegin ();
  std::advance (iter1, f);
  std::advance (iter2, n - 1 - f);

  int64_t goff = std::floor ((*iter1 + *iter2) / 2);

  std::cout << "goff: " << goff << ", loff: " << loff << std::endl;
}

void
TimeServer::CompareOffsWithRealOffs ()
{
  AdvanceLocalTime ();
  for (uint32_t i = 0; i < g_nodes.GetN (); ++i)
    {
      ScionAs *node = dynamic_cast<ScionAs *> (PeekPointer (g_nodes.Get (i)));
      if (node->iaAddr == iaAddr)
        {
          continue;
        }

      if (poff.find (node->iaAddr) == poff.end ())
        {
          std::cout << "NOT FOUND" << std::endl;
          continue;
        }

      dynamic_cast<TimeServer *> (node->GetHost (localAddress))->AdvanceLocalTime ();
      Time remoteLocalTime =
          dynamic_cast<TimeServer *> (node->GetHost (localAddress))->GetLocalTime ();
      int64_t timeDiff = (remoteLocalTime - localTime).GetTimeStep ();

      int64_t remoteDrift = dynamic_cast<TimeServer *> (node->GetHost (localAddress))
                                 ->GetDrift (Time (NTP_REQ_GLOBAL_SYNC_DIFF));
      int64_t localDrift = GetDrift (Time (NTP_REQ_GLOBAL_SYNC_DIFF));

      int64_t offset = *poff.at (node->iaAddr).begin ();

      int64_t upperBound = std::abs (remoteDrift) + std::abs (localDrift);

      NS_ASSERT_MSG (std::abs (offset - timeDiff) < upperBound,
                     "offset: " << TimeStep (offset) << ", real time diff: " << TimeStep (timeDiff)
                                << ", upper bound: " << TimeStep (upperBound));
    }
}

void
TimeServer::ScheduleListOfAllASesRequest ()
{
  if (readDisjointPaths == ReadOrWriteDisjointPaths::w)
    {
      Simulator::Schedule (firstEvent - Time (PATH_RQ_TIME_SYNC_DIFF),
                           &TimeServer::RequestSetOfAllCoreAsesFromPathServer, this);
    }
  else
    {
      for (Time t = firstEvent; t < lastEvent; t += listOfAsesReqPeriod)
        {
          Time diffWithFirstEvent = t - firstEvent;
          if (diffWithFirstEvent.GetTimeStep () % timeSyncPeriod.GetTimeStep () == 0)
            {
              Simulator::Schedule (t - Time (PATH_RQ_TIME_SYNC_DIFF),
                                   &TimeServer::RequestSetOfAllCoreAsesFromPathServer,
                                   this);
            }
          else
            {
              Simulator::Schedule (t, &TimeServer::RequestSetOfAllCoreAsesFromPathServer,
                                   this);
            }
        }
    }
}

void
TimeServer::ScheduleTimeSync ()
{
  if (readDisjointPaths == ReadOrWriteDisjointPaths::r ||
      readDisjointPaths == ReadOrWriteDisjointPaths::noRNoW)
    {
      Simulator::Schedule (firstEvent, &TimeServer::ResetTime, this);
      for (Time t = firstEvent; t < lastEvent; t += timeSyncPeriod)
        {
          Simulator::Schedule (t, &TimeServer::TriggerCoreTimeSyncAlgo, this);
        }
    }
}

void
TimeServer::ScheduleSnapShots ()
{
  if (snapshotType == SnapshotType::snapshotOff)
    {
      return;
    }

  if (snapshotType == SnapshotType::localSnapshot)
    {
      Simulator::Schedule (firstEvent + TimeStep (1), &TimeServer::CaptureLocalSnapshot, this);
    }

  for (Time t = firstEvent; t < lastEvent; t += snapshotPeriod)
    {
      Time diffWithFirstEvent = t - firstEvent;
      if (diffWithFirstEvent.GetTimeStep () % timeSyncPeriod.GetTimeStep () == 0)
        {
          if ((diffWithFirstEvent.GetTimeStep () / timeSyncPeriod.GetTimeStep ()) % g == 0)
            {
              if (snapshotType == SnapshotType::localSnapshot)
                {
                  Simulator::Schedule (t + Time (NTP_REQ_GLOBAL_SYNC_DIFF) + TimeStep (1),
                                       &TimeServer::CaptureLocalSnapshot, this);
                }
              else if (snapshotType == SnapshotType::printOffsetDiff)
                {
                  Simulator::Schedule (t + Time (NTP_REQ_GLOBAL_SYNC_DIFF),
                                       &TimeServer::CaptureOffsetDiff, this);
                }
              else if (snapshotType == SnapshotType::assertOffsetDiff)
                {
                  Simulator::Schedule (t + Time (NTP_REQ_GLOBAL_SYNC_DIFF),
                                       &TimeServer::CompareOffsWithRealOffs, this);
                }
            }
          else
            {
              if (snapshotType == SnapshotType::localSnapshot)
                {
                  Simulator::Schedule (t + TimeStep (1), &TimeServer::CaptureLocalSnapshot, this);
                }
            }
        }
      else
        {
          if (snapshotType == SnapshotType::localSnapshot)
            {
              Simulator::Schedule (t, &TimeServer::CaptureLocalSnapshot, this);
            }
        }
    }
}

int64_t
TimeServer::GetConstantDriftPerDay ()
{
  return constantDriftPerDay;
}
} // namespace ns3
