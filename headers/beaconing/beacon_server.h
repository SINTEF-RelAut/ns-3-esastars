/**
 * @file beacon_server.h
 * @authors Seyedali Tabaeiaghdaei, Christelle Gloor
 * @date 2020
 */

#ifndef SCION_SIMULATOR_BEACON_SERVER_H
#define SCION_SIMULATOR_BEACON_SERVER_H

#include <map>
#include <unordered_map>
#include <unordered_set>

#include "ns3/nstime.h"
#include "ns3/rapidxml.hpp"

#include "src/SCION/headers/beaconing/beacon.h"
#include "src/SCION/headers/externs.h"
#include "src/SCION/headers/scion_as.h"

namespace ns3 {

#define MAX_BEACONS_TO_STORE 20
#define MAX_BEACONS_TO_SEND 5

class ScionAs;

typedef std::unordered_set<Beacon *> BeaconsWithEqualLength_t;
typedef std::map<uint16_t, BeaconsWithEqualLength_t> BeaconsWithSameDstAs_t;
typedef std::pair<Time, uint16_t> BeaconingTimingParams_t;

class BeaconServer
{
public:
  BeaconServer (ScionAs *as, bool parallelScheduler, rapidxml::xml_node<> *xmlNode,
                const YAML::Node &config)
      : as (as),
        parallelScheduler (parallelScheduler),
        beaconingPeriod (Time (config["beacon_service"]["period"].as<std::string> ())),
        expirationPeriod (Time (config["beacon_service"]["expiration_period"].as<std::string> ())
                               .ToInteger (Time::MIN)),
        lastBeaconingEventTime (
            Time (config["beacon_service"]["last_beaconing"].as<std::string> ()))
  {
    nonRequestedPullBasedBeaconContainer.resize (2);
    PropertyContainer p = ParseProperties (xmlNode);
    if (p.HasProperty ("dirty_energy_ratio"))
      {
        dirtyEnergyRatio = std::stod (p.GetProperty ("dirty_energy_ratio"));
      }

    if (p.HasProperty ("sun_energy_ratio"))
      {
        sunEnergyRatio = std::stod (p.GetProperty ("sun_energy_ratio"));
      }

    if (config["beacon_service"]["read_beacons_directory"])
      {
        fileToReadBeacons =
            config["beacon_service"]["read_beacons_directory"].as<std::string> () + "beacons_" +
            std::to_string (as->asNumber) + ".json";
      }
    else
      {
        fileToReadBeacons = "none";
      }

    if (config["beacon_service"]["write_beacons_directory"])
      {
        fileToWriteBeacons =
            config["beacon_service"]["write_beacons_directory"].as<std::string> () + "beacons_" +
            std::to_string (as->asNumber) + ".json";
      }
    else
      {
        fileToWriteBeacons = "none";
      }
  }

  virtual void DoInitializations (uint32_t numAses, rapidxml::xml_node<> *xmlNode,
                                  const YAML::Node &config);

  virtual void PerLinkInitializations (rapidxml::xml_node<> *xmlNode, const YAML::Node &config);

  void SetAs (ScionAs *as);

  void ReceiveBeacon (Beacon &receivedBeacon, uint16_t senderAs, uint16_t remoteIf,
                      uint16_t localIf);

  void ScheduleBeaconing (Time lastBeaconingEventTime);

  void InsertPulledBeaconsToBeaconStore ();

  const uint16_t GetCurrentTime () const;

  const std::vector<std::vector<Ld_t>> &GetIntraAsEnergies () const;

  float GetDirtyEnergyRatio () const;

  float GetSunEnergyRatio () const;

  const std::unordered_map<uint16_t, BeaconsWithSameDstAs_t> &GetBeaconStore () const;

  const std::unordered_map<std::string, Beacon> &GetPathMapToBeacon () const;

  const std::unordered_map<uint16_t, uint32_t> &GetValidBeaconsCountPerDstAs () const;

  const std::unordered_map<uint16_t, uint32_t> &GetNextRoundValidBeaconsCountPerDstAs () const;

  const std::unordered_map<uint16_t, std::vector<uint32_t>> &
  GetBytesSentPerInterfacePerPeriod () const;

  const std::unordered_map<uint16_t, std::vector<uint32_t>> &
  GetBeaconsSentPerInterfacePerPeriod () const;

  const std::vector<std::unordered_map<uint16_t, uint32_t>> &
  GetBeaconsSentPerDstPerInterfacePerPeriod () const;
  const std::vector<std::unordered_map<const OptimizationTarget *, uint32_t>> &
  GetPushBasedBeaconsSentPerOptPerInterfacePerPeriod () const;
  const std::vector<std::unordered_map<const OptimizationTarget *, uint32_t>> &
  GetPullBasedBeaconsSentPerOptPerInterfacePerPeriod () const;

  const std::vector<uint64_t> &GetBeaconsSentPerInterface () const;

protected:
  ScionAs *as;

  const bool parallelScheduler;

  const Time beaconingPeriod;
  const uint16_t expirationPeriod;
  const Time lastBeaconingEventTime;

  float dirtyEnergyRatio;
  float sunEnergyRatio;

  std::string fileToWriteBeacons;
  std::string fileToReadBeacons;

  uint16_t now;
  uint16_t nextPeriod;

  std::vector<std::vector<Ld_t>> intraAsEnergies;

  std::unordered_map<uint16_t, BeaconsWithSameDstAs_t> beaconStore;

  // All beacon instances are stored in either of the containers, with no overlap
  uint16_t pullBasedWrite = 0;
  uint16_t pullBasedRead = 1;
  std::unordered_map<std::string, Beacon> pushBasedBeaconContainer;
  std::vector<std::unordered_map<std::string, Beacon>> nonRequestedPullBasedBeaconContainer;
  std::unordered_map<std::string, Beacon>
      requestedPullBasedBeaconContainer; // the result of pull based beaconing returned to the source AS

  std::unordered_map<uint16_t, uint32_t> validBeaconsCountPerDstAs;
  std::unordered_map<uint16_t, uint32_t> nextRoundValidBeaconsCountPerDstAs;

  std::unordered_map<uint16_t, std::vector<uint32_t>> bytesSentPerInterfacePerPeriod;
  std::unordered_map<uint16_t, std::vector<uint32_t>> beaconsSentPerInterfacePerPeriod;

  std::vector<std::unordered_map<uint16_t, uint32_t>> beaconsSentPerDstPerInterface;
  std::vector<std::unordered_map<const OptimizationTarget *, uint32_t>>
      pushBasedBeaconsSentPerOptPerInterface;
  std::vector<std::unordered_map<const OptimizationTarget *, uint32_t>>
      pullBasedBeaconsSentPerOptPerInterface;

  std::vector<uint64_t> beaconsSentPerInterface;

  void InitiateBeacons (NeighbourRelation relation);

  virtual void InitiateBeaconsPerInterface (uint16_t selfEgressIfNo, ScionAs *remoteAs,
                                               uint16_t remoteIngressIfNo);

  virtual void CreateInitialStaticInfoExtension (StaticInfoExtension_t &staticInfoExtension,
                                        uint16_t selfEgressIfNo,
                                        const OptimizationTarget *optimizationTarget);

  virtual void DisseminateBeacons (NeighbourRelation relation) = 0;

  void GenerateBeaconAndSend (Beacon *selectedBeacon, uint16_t selfEgressIfNo,
                            uint16_t remoteIngressIfNo, ScionAs *remoteAs,
                              StaticInfoExtension_t &staticInfoExtension,
                            const OptimizationTarget *optimizationTarget = NULL,
                              BeaconDirection beaconDirection = BeaconDirection::pushBased);

  std::tuple<bool, bool, bool, Beacon *, Ld_t> ImportPolicy (Beacon &theBeacon, uint16_t senderAs,
                                                            uint16_t remoteEgressIfNo,
                                                            uint16_t selfIngressIfNo,
                                                            uint16_t now);

  void InsertBeacon (Beacon &theBeacon, uint16_t dstAs, uint16_t senderAs,
                      uint16_t remoteEgressIf, uint16_t localIngressIf, bool pathExists,
                      bool existingPathValid, Beacon *beaconToReplace);

  void IncrementValidBeaconsCount (uint16_t dstAs);

  void IncrementNextRoundValidBeaconsCount (uint16_t dstAs);

  void DeleteBeacon (Beacon *toBeRemovedBeacon, Ld_t replacementKey, uint16_t dstAs);

  void DecrementValidBeaconsCount (uint16_t dstAs);

  void DecrementNextRoundValidBeaconsCount (uint16_t dstAs);

  virtual std::tuple<bool, bool, bool, Beacon *, Ld_t>
  AlgSpecificImportPolicy (Beacon &theBeacon, uint16_t senderAs, uint16_t remoteEgressIfNo,
                              uint16_t selfIngressIfNo, uint16_t now) = 0;

  virtual void InsertToAlgorithmDataStructures (Beacon *theBeacon, uint16_t senderAs,
                                                    uint16_t remoteEgressIfNo,
                                                    uint16_t selfIngressIfNo) = 0;

  virtual void DeleteFromAlgorithmDataStructures (Beacon *theBeacon, Ld_t replacementKey) = 0;

  void UpdateStatePeriodic ();

  virtual void UpdateStateBeforeBeaconing ();

  void UpdateBeaconState (Beacon *theBeacon);

  virtual void UpdateAlgorithmDataStructuresPeriodic (Beacon *theBeacon, bool invalidated) = 0;

  void RegisterToLocalPathServer ();

  void IncrementControlPlaneBytesSent (Beacon &theBeacon, uint16_t interface);

  std::pair<Ld_t, Ld_t> CalculateFinalDiversityScores (Beacon *theBeacon);

  friend void ReadBr2BrEnergy (ns3::NodeContainer asNodes,
                               std::map<int32_t, uint16_t> realToAliasAsNo,
                               const YAML::Node &config);

  void ReadBeacons ();

  void WriteBeacons ();
};

void ReadBr2BrEnergy (NodeContainer asNodes, std::map<int32_t, uint16_t> realToAliasAsNo,
                      const YAML::Node &config);

} // namespace ns3
#endif //SCION_SIMULATOR_BEACON_SERVER_H
