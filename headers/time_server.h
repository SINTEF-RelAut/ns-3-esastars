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

enum READ_OR_WRITE_DISJOINT_PATHS { R = 0, W = 2, NO_R_NO_W = 3 };
enum TIME_SERVER_TYPE { NORMAL = 0, MALICIOUS_SERVER = 1 };
enum REFERENCE_TIME_TYPE { OFF = 0, ON = 1, MALICIOUS_REF = 2 };

enum SNAPSHOT_TYPE {
  LOCAL_SNAPSHOT = 0,
  ASSERT_OFFSET_DIFF = 1,
  PRINT_OFFSET_DIFF = 2,
  SNAPSHOT_OFF = 3
};
enum ALG_V { V1 = 0, V2 = 1, V3 = 2, V4 = 3, LOCAL_SYNC = 4 };

class TimeServer : public SCIONHost
{
public:
  TimeServer (uint32_t system_id, uint16_t isd_number, uint16_t as_number,
              host_addr_t local_address, double latitude, double longitude, SCION_AS *AS,
              bool parallel_scheduler, Time max_initial_drift, Time max_drift_per_day,
              bool jitter_in_drift, uint32_t max_drift_coefficient, Time global_cut_off,
              Time first_event, Time last_event, Time snapshot_period, Time list_of_ases_req_period,
              Time time_sync_period, uint32_t G, uint32_t number_of_paths_to_use_for_global_sync,
              std::string read_disjoint_paths, std::string time_service_output_path,
              std::string reference_time_type, std::string server_type, std::string snapshot_type,
              std::string alg_v, Time minimum_malicious_offset, std::string path_selection)
      : SCIONHost (system_id, isd_number, as_number, local_address, latitude, longitude, AS),
        parallel_scheduler (parallel_scheduler),
        max_initial_drift (max_initial_drift),
        max_drift_per_day (max_drift_per_day),
        jitter_in_drift (jitter_in_drift),
        max_drift_coefficient (max_drift_coefficient),
        global_cut_off (global_cut_off),
        first_event (first_event),
        last_event (last_event),
        snapshot_period (snapshot_period),
        list_of_ases_req_period (list_of_ases_req_period),
        time_sync_period (time_sync_period),
        G (G),
        number_of_paths_to_use_for_global_sync (number_of_paths_to_use_for_global_sync),
        read_disjoint_paths (READ_OR_WRITE_DISJOINT_PATHS_MAP[read_disjoint_paths]),
        reference_time_type (REFERENCE_TIME_TYPE_MAP[reference_time_type]),
        server_type (TIME_SERVER_TYPE_MAP[server_type]),
        snapshot_type (SNAPSHOT_TYPE_MAP[snapshot_type]),
        alg_v (ALG_V_MAP[alg_v]),
        minimum_malicious_offset (minimum_malicious_offset),
        path_selection (path_selection)
  {
    synchronization_round = 0;

    local_time = TimeStep (0);
    real_time_of_last_time_advance = TimeStep (0);
    set_of_all_core_ases.insert (ia_addr);

    set_of_disjoint_paths_file =
        time_service_output_path + "set_of_disjoint_path_TS_" + std::to_string (ia_addr) + ".json";

    std::random_device rd;
    std::uniform_int_distribution<int64_t> dist (-std::abs (max_drift_per_day.GetTimeStep ()),
                                                 std::abs (max_drift_per_day.GetTimeStep ()));
    constant_drift_per_day = dist (rd);

    if (REFERENCE_TIME_TYPE_MAP[reference_time_type] == REFERENCE_TIME_TYPE::MALICIOUS_REF ||
        TIME_SERVER_TYPE_MAP[server_type] == TIME_SERVER_TYPE::MALICIOUS_SERVER)
      {
        std::uniform_int_distribution<uint32_t> neg_or_pos_dist (0, 1);
        if (neg_or_pos_dist (rd) == 0)
          {
            std::uniform_int_distribution<int64_t> neg_dist (
                -10 * std::abs (minimum_malicious_offset.GetTimeStep ()),
                -std::abs (minimum_malicious_offset.GetTimeStep ()));
            malicious_offset_in_ps = neg_dist (rd);
          }
        else
          {
            std::uniform_int_distribution<int64_t> pos_dist (
                std::abs (minimum_malicious_offset.GetTimeStep ()),
                10 * std::abs (minimum_malicious_offset.GetTimeStep ()));
            malicious_offset_in_ps = pos_dist (rd);
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
  bool affected_by_malicious_ases = false;

  std::map<std::string, READ_OR_WRITE_DISJOINT_PATHS> READ_OR_WRITE_DISJOINT_PATHS_MAP = {
      {"R", READ_OR_WRITE_DISJOINT_PATHS::R},
      {"W", READ_OR_WRITE_DISJOINT_PATHS::W},
      {"NO_R_NO_W", READ_OR_WRITE_DISJOINT_PATHS::NO_R_NO_W}};

  std::map<std::string, REFERENCE_TIME_TYPE> REFERENCE_TIME_TYPE_MAP = {
      {"OFF", REFERENCE_TIME_TYPE::OFF},
      {"ON", REFERENCE_TIME_TYPE::ON},
      {"MALICIOUS", REFERENCE_TIME_TYPE::MALICIOUS_REF}};

  std::map<std::string, TIME_SERVER_TYPE> TIME_SERVER_TYPE_MAP = {
      {"NORMAL", TIME_SERVER_TYPE::NORMAL}, {"MALICIOUS", TIME_SERVER_TYPE::MALICIOUS_SERVER}};

  std::map<std::string, SNAPSHOT_TYPE> SNAPSHOT_TYPE_MAP = {
      {"LOCAL_SNAPSHOT", SNAPSHOT_TYPE::LOCAL_SNAPSHOT},
      {"ASSERT_OFFSET_DIFF", SNAPSHOT_TYPE::ASSERT_OFFSET_DIFF},
      {"PRINT_OFFSET_DIFF", SNAPSHOT_TYPE::PRINT_OFFSET_DIFF},
      {"OFF", SNAPSHOT_TYPE::SNAPSHOT_OFF}};

  std::map<std::string, ALG_V> ALG_V_MAP = {{"V1", ALG_V::V1},
                                            {"V2", ALG_V::V2},
                                            {"V3", ALG_V::V3},
                                            {"V4", ALG_V::V4},
                                            {"LOCAL_SYNC", ALG_V::LOCAL_SYNC}};

  bool parallel_scheduler;
  Time max_initial_drift;
  Time max_drift_per_day;
  bool jitter_in_drift;
  uint32_t max_drift_coefficient;
  Time global_cut_off;
  Time first_event, last_event, snapshot_period;
  Time list_of_ases_req_period;
  Time time_sync_period;

  uint32_t G;
  uint32_t number_of_paths_to_use_for_global_sync;
  READ_OR_WRITE_DISJOINT_PATHS read_disjoint_paths;
  uint32_t synchronization_round; // i in the Listing 2

  Time real_time_of_last_time_advance;

  std::set<ia_t> set_of_all_core_ases;

  std::string set_of_disjoint_paths_file;

  REFERENCE_TIME_TYPE reference_time_type;
  TIME_SERVER_TYPE server_type;
  SNAPSHOT_TYPE snapshot_type;
  ALG_V alg_v;

  Time minimum_malicious_offset;
  std::string path_selection;
  int64_t constant_drift_per_day;
  int64_t malicious_offset_in_ps;

  std::unordered_map<ia_t, std::multiset<int64_t>> poff;

  std::unordered_map<ia_t, std::unordered_set<const PathSegment *>> set_of_selected_paths;

  Time GetReferenceTime ();

  Time GetMaxDrift (Time duration);

  void RequestSetOfAllCoreAsesFromPathServer ();

  void RequestForPathsToAllCoreAses ();

  void SendSetOfAllCoreAsesToNeighbors ();

  void SendNtpReqToPeers ();

  void ModifyPktUponSend (SCIONPacket *packet) override;

  void ReceiveSetOfAllCoreAsesFromPathServer (SCIONPacket *packet);

  void ConstructSetOfSelectedPaths ();

  void ConstructSetOfShortestPaths ();

  void ConstructSetOfRandomPaths ();

  void ConstructSetOfMostDisjointPaths ();

  void ReadSetOfDisjointPaths ();

  void WriteSetOfDisjointPaths ();

  void ReceiveSetOfAllCoreAsesFromOtherTimeServer (SCIONPacket *packet);

  void ReceiveNtpReqFromPeer (SCIONPacket *packet, Time receive_time);

  void ReceiveNtpResFromPeer (SCIONPacket *packet, Time receive_time);

  void TriggerCoreTimeSyncAlgo ();

  void ContinueGlobalTimeSync ();

  void CorrectLocalTime (int64_t corr, Time duration, double coefficient);

  void ProcessReceivedPacket (uint16_t local_if, SCIONPacket *packet, Time receive_time) override;

  void CaptureLocalSnapshot ();

  void CaptureOffsetDiff ();

  void CompareOffsWithRealOffs ();

  void ResetTime ();
};
} // namespace ns3

#endif //SCION_SIMULATOR_TIME_SERVER_H
