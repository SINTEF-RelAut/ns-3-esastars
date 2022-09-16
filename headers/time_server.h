//
// Created by seyedali on 30.08.21.
//

#ifndef SCION_SIMULATOR_TIME_SERVER_H
#define SCION_SIMULATOR_TIME_SERVER_H

#include <algorithm>
#include <random>
#include <set>

#include "ns3/nstime.h"

#include "src/SCION/headers/scion_host.h"
#include "src/SCION/headers/user_defined_events.h"

namespace ns3 {

#define PATH_RQ_TIME_SYNC_DIFF "1s"
#define NTP_REQ_GLOBAL_SYNC_DIFF "2s"

enum ReadOrWriteDisjointPaths { r = 0, w = 2, noRNoW = 3 };
enum TimeServerType { normal = 0, maliciousServer = 1 };
enum ReferenceTimeType { off = 0, on = 1, maliciousRef = 2 };

enum SnapshotType { localSnapshot = 0, assertOffsetDiff = 1, printOffsetDiff = 2, snapshotOff = 3
};
enum AlgV { v1 = 0, v2 = 1, v3 = 2, v4 = 3, localSync = 4 };

class TimeServer : public ScionHost
{
public:
  TimeServer (uint32_t systemId, uint16_t isdNumber, uint16_t asNumber, HostAddr_t localAddress, double latitude, double longitude, ScionAs *as,
              bool parallelScheduler, Time maxInitialDrift, Time maxDriftPerDay,
              bool jitterInDrift, uint32_t maxDriftCoefficient, Time globalCutOff,
              Time firstEvent, Time lastEvent, Time snapshotPeriod, Time listOfAsesReqPeriod,
              Time timeSyncPeriod, uint32_t g, uint32_t numberOfPathsToUseForGlobalSync,
              std::string readDisjointPaths, std::string timeServiceOutputPath,
              std::string referenceTimeType, std::string serverType, std::string snapshotType,
              std::string algV, Time minimumMaliciousOffset, std::string pathSelection)
      : ScionHost (systemId, isdNumber, asNumber, localAddress, latitude, longitude, as),
        parallelScheduler (parallelScheduler),
        maxInitialDrift (maxInitialDrift),
        maxDriftPerDay (maxDriftPerDay),
        jitterInDrift (jitterInDrift),
        maxDriftCoefficient (maxDriftCoefficient),
        globalCutOff (globalCutOff),
        firstEvent (firstEvent),
        lastEvent (lastEvent),
        snapshotPeriod (snapshotPeriod),
        listOfAsesReqPeriod (listOfAsesReqPeriod),
        timeSyncPeriod (timeSyncPeriod),
        g (g),
        numberOfPathsToUseForGlobalSync (numberOfPathsToUseForGlobalSync),
        readDisjointPaths (readOrWriteDisjointPathsMap[readDisjointPaths]),
        referenceTimeType (referenceTimeTypeMap[referenceTimeType]),
        serverType (timeServerTypeMap[serverType]),
        snapshotType (snapshotTypeMap[snapshotType]),
        algV (algVMap[algV]),
        minimumMaliciousOffset (minimumMaliciousOffset),
        pathSelection (pathSelection)
  {
    synchronizationRound = 0;

    localTime = TimeStep (0);
    realTimeOfLastTimeAdvance = TimeStep (0);
    setOfAllCoreAses.insert (iaAddr);

    setOfDisjointPathsFile =
        timeServiceOutputPath + "set_of_disjoint_path_TS_" + std::to_string (iaAddr) + ".json";

    std::random_device rd;
    std::uniform_int_distribution<int64_t> dist (-std::abs (maxDriftPerDay.GetTimeStep ()),
                                                 std::abs (maxDriftPerDay.GetTimeStep ()));
    constantDriftPerDay = dist (rd);

    if (referenceTimeTypeMap[referenceTimeType] == ReferenceTimeType::maliciousRef ||
        timeServerTypeMap[serverType] == TimeServerType::maliciousServer)
      {
        std::uniform_int_distribution<uint32_t> negOrPosDist (0, 1);
        if (negOrPosDist (rd) == 0)
          {
            std::uniform_int_distribution<int64_t> negDist (
                -10 * std::abs (minimumMaliciousOffset.GetTimeStep ()),
                -std::abs (minimumMaliciousOffset.GetTimeStep ()));
            maliciousOffsetInPs = negDist (rd);
          }
        else
          {
            std::uniform_int_distribution<int64_t> posDist (
                std::abs (minimumMaliciousOffset.GetTimeStep ()),
                10 * std::abs (minimumMaliciousOffset.GetTimeStep ()));
            maliciousOffsetInPs = posDist (rd);
          }
      }
  }

  void ScheduleListOfAllASesRequest ();
  void ScheduleTimeSync ();
  void ScheduleSnapShots ();
  void AdvanceLocalTime () override;
  int64_t GetConstantDriftPerDay ();
  int64_t GetDrift (Time duration);

  friend class PostSimulationEvaluations;
  friend class UserDefinedEvents;

private:
  bool affectedByMaliciousAses = false;

  std::map<std::string, ReadOrWriteDisjointPaths> readOrWriteDisjointPathsMap = {
      {"R", ReadOrWriteDisjointPaths::r},
      {"W", ReadOrWriteDisjointPaths::w},
      {"NO_R_NO_W", ReadOrWriteDisjointPaths::noRNoW}};

  std::map<std::string, ReferenceTimeType> referenceTimeTypeMap = {
      {"OFF", ReferenceTimeType::off},
      {"ON", ReferenceTimeType::on},
      {"MALICIOUS", ReferenceTimeType::maliciousRef}};

  std::map<std::string, TimeServerType> timeServerTypeMap = {
      {"NORMAL", TimeServerType::normal}, {"MALICIOUS", TimeServerType::maliciousServer}};

  std::map<std::string, SnapshotType> snapshotTypeMap = {
      {"LOCAL_SNAPSHOT", SnapshotType::localSnapshot},
      {"ASSERT_OFFSET_DIFF", SnapshotType::assertOffsetDiff},
      {"PRINT_OFFSET_DIFF", SnapshotType::printOffsetDiff},
      {"OFF", SnapshotType::snapshotOff}};

  std::map<std::string, AlgV> algVMap = {{"V1", AlgV::v1},
                                            {"V2", AlgV::v2},
                                            {"V3", AlgV::v3},
                                            {"V4", AlgV::v4},
                                            {"LOCAL_SYNC", AlgV::localSync}};

  bool parallelScheduler;
  Time maxInitialDrift;
  Time maxDriftPerDay;
  bool jitterInDrift;
  uint32_t maxDriftCoefficient;
  Time globalCutOff;
  Time firstEvent, lastEvent, snapshotPeriod;
  Time listOfAsesReqPeriod;
  Time timeSyncPeriod;

  uint32_t g;
  uint32_t numberOfPathsToUseForGlobalSync;
  ReadOrWriteDisjointPaths readDisjointPaths;
  uint32_t synchronizationRound; // i in the Listing 2

  Time realTimeOfLastTimeAdvance;

  std::set<Ia_t> setOfAllCoreAses;

  std::string setOfDisjointPathsFile;

  ReferenceTimeType referenceTimeType;
  TimeServerType serverType;
  SnapshotType snapshotType;
  AlgV algV;

  Time minimumMaliciousOffset;
  std::string pathSelection;
  int64_t constantDriftPerDay;
  int64_t maliciousOffsetInPs;

  std::unordered_map<Ia_t, std::multiset<int64_t>> poff;

  std::unordered_map<Ia_t, std::unordered_set<const PathSegment *>> setOfSelectedPaths;

  Time GetReferenceTime ();

  Time GetMaxDrift (Time duration);

  void RequestSetOfAllCoreAsesFromPathServer ();

  void RequestForPathsToAllCoreAses ();

  void SendSetOfAllCoreAsesToNeighbors ();

  void SendNtpReqToPeers ();

  void ModifyPktUponSend (ScionPacket *packet) override;

  void ReceiveSetOfAllCoreAsesFromPathServer (ScionPacket *packet);

  void ConstructSetOfSelectedPaths ();

  void ConstructSetOfShortestPaths ();

  void ConstructSetOfRandomPaths ();

  void ConstructSetOfMostDisjointPaths ();

  void ReadSetOfDisjointPaths ();

  void WriteSetOfDisjointPaths ();

  void ReceiveSetOfAllCoreAsesFromOtherTimeServer (ScionPacket *packet);

  void ReceiveNtpReqFromPeer (ScionPacket *packet, Time receiveTime);

  void ReceiveNtpResFromPeer (ScionPacket *packet, Time receiveTime);

  void TriggerCoreTimeSyncAlgo ();

  void ContinueGlobalTimeSync ();

  void CorrectLocalTime (int64_t corr, Time duration, double coefficient);

  void ProcessReceivedPacket (uint16_t localIf, ScionPacket *packet, Time receiveTime) override;

  void CaptureLocalSnapshot ();

  void CaptureOffsetDiff ();

  void CompareOffsWithRealOffs ();

  void ResetTime ();
};
} // namespace ns3

#endif //SCION_SIMULATOR_TIME_SERVER_H
