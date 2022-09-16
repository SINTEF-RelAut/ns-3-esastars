//
// Created by seyedali on 17.07.21.
//

#include <istream>
#include <omp.h>
#include <random>
#include <set>

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/nstime.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/ptr.h"

#include "src/SCION/headers/beaconing/baseline.h"
#include "src/SCION/headers/beaconing/beacon_server.h"
#include "src/SCION/headers/beaconing/diversity_age_based.h"
#include "src/SCION/headers/beaconing/green_beaconing.h"
#include "src/SCION/headers/beaconing/latency_optimized_beaconing.h"
#include "src/SCION/headers/beaconing/scionlab_algo.h"
#include "src/SCION/headers/post_simulation_evaluations.h"
#include "src/SCION/headers/schedule_periodic_events.h"
#include "src/SCION/headers/scion_as.h"
#include "src/SCION/headers/scion_core_as.h"
#include "src/SCION/headers/time_server.h"
#include "src/SCION/headers/utils.h"

namespace ns3 {

void
PostSimulationEvaluations::DoFinalEvaluations ()
{
  if (!config["post_eval"])
    {
      return;
    }

  const YAML::Node &evals = config["post_eval"];
  for (auto it = evals.begin (); it != evals.end (); ++it)
    {
      const YAML::Node &eval = *it;
      std::string func = eval["func"].as<std::string> ();
      ((this)->*functionNameToFunction.at (func)) ();
    }
}

void
PostSimulationEvaluations::PrintTrafficSentFromCollectorsPerDstPerPeriod ()
{
  std::cout << "####################################### Traffic sent from each collector "
               "#######################################"
            << std::endl;
  std::list<int32_t> collectors ({3303,  3130,  1239,  701,   5413,  34224, 7018,
                                  53767, 3741,  31019, 22652, 2497,  57866, 37100,
                                  3130,  3257,  3549,  6939,  18106, 1299,  23673,
                                  2914,  11537, 2152,  852,   8492,  34224, 11686});
  for (int32_t collector : collectors)
    {
      double_t consumedBwd = 0.0;
      for (uint32_t i = 0; i < asNodes.GetN (); ++i)
        {
          ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));
          if (aliasToRealAsNo.at (as->asNumber) == collector)
            {
              double_t periods = 0.0;
              for (Time t = Time (0); t < lastBeaconingEventTime; t += beaconingPeriod)
                {
                  periods += 1.0;
                  for (uint32_t ifIndex = 0; ifIndex < as->GetNDevices (); ++ifIndex)
                    {
                      consumedBwd += (double_t) as->GetBeaconServer ()
                                          ->GetBytesSentPerInterfacePerPeriod ()
                                          .at ((uint16_t) t.ToInteger (Time::MIN))
                                          .at (ifIndex);
                    }
                }
              consumedBwd = (double_t) consumedBwd / as->GetNDevices () / periods;
              break;
            }
        }
      std::cout << collector << "\t" << consumedBwd << std::endl;
    }
}

void
PostSimulationEvaluations::FindMinLatencyToDnsRootServers ()
{
  std::string probesFile = "/cluster/scratch/tabaeias/atlas_probes.xml";
  std::ifstream finProbes (probesFile.c_str ());
  std::ostringstream probesSstr;
  probesSstr << finProbes.rdbuf ();
  probesSstr.flush ();
  finProbes.close ();

  std::string xmlProbesData = probesSstr.str ();
  rapidxml::xml_document<> probesDoc;
  probesDoc.parse<0> (&xmlProbesData[0]);

  rapidxml::xml_node<> *probesRootNode = probesDoc.first_node ("root");
  rapidxml::xml_node<> *probesNode = probesRootNode->first_node ("Probes");

  std::list<std::string> rootServerNames ({"a-root", "b-root", "c-root", "d-root", "e-root",
                                             "f-root", "h-root", "j-root", "k-root", "l-root",
                                             "m-root"});
  for (auto const &rootServerName : rootServerNames)
    {
      std::cout << "################################################## " << rootServerName
                << " #########################################################" << std::endl;

      std::string dnsRootFile = "/cluster/scratch/tabaeias/" + rootServerName + ".xml";
      std::ifstream finDnsRoot (dnsRootFile.c_str ());
      std::ostringstream dnsRootSstr;
      dnsRootSstr << finDnsRoot.rdbuf ();
      dnsRootSstr.flush ();
      finDnsRoot.close ();

      std::set<int32_t> set_of_src_ases;

      std::string xmlDnsRootData = dnsRootSstr.str ();
      rapidxml::xml_document<> dnsRootDoc;
      dnsRootDoc.parse<0> (&xmlDnsRootData[0]);

      rapidxml::xml_node<> *dnsRootNode = dnsRootDoc.first_node ("root");
      int32_t dstAsNo = std::stoi (dnsRootNode->first_node ("ASN")->value ());

      if (realToAliasAsNo.find (dstAsNo) == realToAliasAsNo.end ())
        {
          std::cout << rootServerName << ": " << dstAsNo
                    << " The root DNS server's AS is not among the top 2000 g_realToAliasAsNo"
                    << std::endl;
          continue;
        }
      std::cout << "Probe|ASN|Latency|Distance|Probe coordinates|Path Coordinates|Instance "
                   "Coordinates|g_realToAliasAsNo on Path"
                << std::endl;

      uint16_t dstAliasAsNo = realToAliasAsNo.at (dstAsNo);
      ScionAs *dstAs = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (dstAliasAsNo)));

      rapidxml::xml_node<> *currProbe = probesNode->first_node ("item");
      while (currProbe)
        {
          int32_t srcAsNo = std::stoi (currProbe->first_node ("ASN")->value ());
          double probeLat = std::stod (currProbe->first_node ("Latitude")->value ());
          double probeLong = std::stod (currProbe->first_node ("Longitude")->value ());

          if (realToAliasAsNo.find (srcAsNo) == realToAliasAsNo.end ())
            {
              currProbe = currProbe->next_sibling ("item");
              continue;
            }

          set_of_src_ases.insert (srcAsNo);

          ScionAs *srcAliasAsNo = dynamic_cast<ScionAs *> (
              PeekPointer (asNodes.Get (realToAliasAsNo.at (srcAsNo))));

          uint16_t lastBr = 0;
          double minLatencyToDstAs = std::numeric_limits<double>::max ();
          Path_t *selectedPath = NULL;

          auto const &beaconsToDnsRootAs =
              srcAliasAsNo->GetBeaconServer ()->GetBeaconStore ().at (dstAliasAsNo);

          for (auto const &lenBeaconsPair : beaconsToDnsRootAs)
            {
              auto const &sameLenBeacons = lenBeaconsPair.second;

              for (auto const &theBeacon : sameLenBeacons)
                {
                  if (theBeacon->isValid)
                    {
                      assert (UPPER_16_BITS (theBeacon->path.at (0)) == dstAliasAsNo);

                      uint16_t firstBr = LOWER_16_BITS (theBeacon->path.back ());
                      std::pair<double, double> firstBrCoordinates =
                          srcAliasAsNo->interfacesCoordinates.at (firstBr);
                      double latencyFromProbeToFirstHop = CalculateGreatCircleLatency (
                          probeLat, probeLong, firstBrCoordinates.first, firstBrCoordinates.second);
                      if (theBeacon->staticInfoExtension.at (StaticInfoType::latency) +
                              latencyFromProbeToFirstHop <
                          minLatencyToDstAs)
                        {
                          minLatencyToDstAs =
                              theBeacon->staticInfoExtension.at (StaticInfoType::latency) +
                              latencyFromProbeToFirstHop;
                          lastBr = SECOND_UPPER_16_BITS (theBeacon->path.at (0));
                          selectedPath = &theBeacon->path;
                        }
                    }
                }
            }

          double minOverallLatency = std::numeric_limits<double>::max ();
          std::pair<double, double> selectedInstanceCoordinates;

          rapidxml::xml_node<> *sitesNode = dnsRootNode->first_node ("Sites");
          rapidxml::xml_node<> *currSite = sitesNode->first_node ("item");
          while (currSite)
            {
              double instanceLat = std::stod (currSite->first_node ("Latitude")->value ());
              double instanceLong = std::stod (currSite->first_node ("Longitude")->value ());
              std::pair<double, double> lastBrCoordinates =
                  dstAs->interfacesCoordinates.at (lastBr);
              double overallLatency =
                  minLatencyToDstAs + CalculateGreatCircleLatency (instanceLat, instanceLong,
                                                                   lastBrCoordinates.first,
                                                                   lastBrCoordinates.second);
              if (overallLatency < minOverallLatency)
                {
                  minOverallLatency = overallLatency;
                  selectedInstanceCoordinates =
                      std::pair<double, double> (instanceLat, instanceLong);
                }
              currSite = currSite->next_sibling ("item");
            }

          std::cout
              << currProbe->first_node ("ID")->value () << "|" << srcAsNo
              << "|"
              // << "(" << selected_instance_coordinates.first << ", " << selected_instance_coordinates.second << ")" << "|"
              << minOverallLatency << "|"
              << CalculateGreatCircleDistance (probeLat, probeLong,
                                               selectedInstanceCoordinates.first,
                                               selectedInstanceCoordinates.second)
              << "|"
              << "(" << probeLat << ", " << probeLong << ")"
              << "|";

          uint32_t hopCnt = 0;
          std::vector<LinkInformation_t>::reverse_iterator hop = selectedPath->rbegin ();
          for (; hop != selectedPath->rend (); ++hop)
            {
              if (hopCnt != 0)
                {
                  std::cout << " ";
                }

              ScionAs *as =
                  dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (UPPER_16_BITS (*hop))));
              std::pair<double, double> brCoordinates =
                  as->interfacesCoordinates.at (SECOND_UPPER_16_BITS (*hop));
              std::cout << "(" << brCoordinates.first << ", " << brCoordinates.second << ")";
              hopCnt++;
            }

          std::cout << "|"
                    << "(" << selectedInstanceCoordinates.first << ", "
                    << selectedInstanceCoordinates.second << ")"
                    << "|";

          hopCnt = 0;
          hop = selectedPath->rbegin ();
          for (; hop != selectedPath->rend (); ++hop)
            {
              if (hopCnt != 0)
                {
                  std::cout << " ";
                }
              std::cout << aliasToRealAsNo.at (SECOND_LOWER_16_BITS (*hop));
              hopCnt++;
            }

          std::cout << " " << aliasToRealAsNo.at (UPPER_16_BITS (selectedPath->at (0)));

          std::cout << std::endl;

          currProbe = currProbe->next_sibling ("item");
        }

      for (auto const &srcAsNo : set_of_src_ases)
        {
          ScionAs *srcAs = dynamic_cast<ScionAs *> (
              PeekPointer (asNodes.Get (realToAliasAsNo.at (srcAsNo))));
          auto const &beaconsToDnsRootAs =
              srcAs->GetBeaconServer ()->GetBeaconStore ().at (dstAliasAsNo);
          for (auto const &lenBeaconsPair : beaconsToDnsRootAs)
            {
              auto const &sameLenBeacons = lenBeaconsPair.second;
              for (auto const &theBeacon : sameLenBeacons)
                {
                  if (!theBeacon->isValid)
                    {
                      continue;
                    }

                  std::cout << srcAsNo << "|" << dstAsNo << "|";
                  Path_t *thePath = &theBeacon->path;

                  uint32_t hopCnt = 0;
                  std::vector<LinkInformation_t>::reverse_iterator hop = thePath->rbegin ();
                  for (; hop != thePath->rend (); ++hop)
                    {
                      if (hopCnt != 0)
                        {
                          std::cout << " ";
                        }

                      ScionAs *as = dynamic_cast<ScionAs *> (
                          PeekPointer (asNodes.Get (UPPER_16_BITS (*hop))));
                      std::pair<double, double> brCoordinates =
                          as->interfacesCoordinates.at (SECOND_UPPER_16_BITS (*hop));
                      std::cout << "(" << brCoordinates.first << ", " << brCoordinates.second
                                << ")";
                      hopCnt++;
                    }

                  std::cout << "|";

                  hopCnt = 0;
                  hop = thePath->rbegin ();
                  for (; hop != thePath->rend (); ++hop)
                    {
                      if (hopCnt != 0)
                        {
                          std::cout << " ";
                        }
                      std::cout << aliasToRealAsNo.at (SECOND_LOWER_16_BITS (*hop));
                      hopCnt++;
                    }

                  std::cout << " " << aliasToRealAsNo.at (UPPER_16_BITS (thePath->at (0)));

                  std::cout << std::endl;
                }
            }
        }
    }
}

void
PostSimulationEvaluations::PrintAllDiscoveredPaths ()
{
  std::cout << "################################################ Paths Information "
               "##############################################################"
            << std::endl;

  for (uint32_t i = 0; i < asNodes.GetN (); ++i)
    {
      ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));

      std::cout << "From: " << aliasToRealAsNo.at (as->asNumber) << std::endl;

      as->GetBeaconServer ()->InsertPulledBeaconsToBeaconStore ();

      for (auto const &dstAsBeaconsPair : as->GetBeaconServer ()->GetBeaconStore ())
        {
          uint16_t dstAs = dstAsBeaconsPair.first;
          auto const &sameDstAsBeacons = dstAsBeaconsPair.second;

          std::cout << "\t"
                    << "To: " << aliasToRealAsNo.at (dstAs) << std::endl;

          for (auto const &beaconsFromSameNbr : sameDstAsBeacons)
            {
              for (auto const &theBeacon : beaconsFromSameNbr.second)
                {
                  if (!theBeacon->isValid)
                    {
                      continue;
                    }
                  std::cout << "\t"
                            << "\t";
                  uint32_t hopCnt = 0;
                  std::vector<LinkInformation_t>::reverse_iterator hop = theBeacon->path.rbegin ();
                  for (; hop != theBeacon->path.rend (); ++hop)
                    {
                      if (hopCnt != 0)
                        {
                          std::cout << ", ";
                        }
                      std::cout << aliasToRealAsNo.at (SECOND_LOWER_16_BITS (*hop)) << ":"
                                << LOWER_16_BITS (*hop) << ", "
                                << aliasToRealAsNo.at (UPPER_16_BITS (*hop)) << ":"
                                << SECOND_UPPER_16_BITS (*hop);
                      hopCnt++;
                    }

                  if (theBeacon->beaconDirection == BeaconDirection::pullBased)
                    {
                      std::cout << "; pull"
                                << "; initiation_time = " << theBeacon->initiationTime;
                    }
                  else
                    {
                      std::cout << "; push";
                    }

                  std::cout << "; ";

                  for (auto const &metric : theBeacon->staticInfoExtension)
                    {
                      if (metric.first == latency)
                        {
                          std::cout << "latency = " << metric.second;
                          std::cout << "; ";
                        }
                      else if (metric.first == bw)
                        {
                          std::cout << "BWD = " << metric.second << ";";
                        }
                    }

                  std::cout << std::endl;
                }
            }
        }
    }
}

void
PostSimulationEvaluations::PrintAllPathsAttributes ()
{
  std::cout << "################################################ Paths Information "
               "##############################################################"
            << std::endl;

  for (uint32_t i = 0; i < asNodes.GetN (); ++i)
    {
      ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));

      std::cout << "From: " << aliasToRealAsNo.at (as->asNumber) << std::endl;

      for (auto const &dstAsBeaconsPair : as->GetBeaconServer ()->GetBeaconStore ())
        {
          uint16_t dstAs = dstAsBeaconsPair.first;
          auto const &sameDstAsBeacons = dstAsBeaconsPair.second;

          std::cout << "\t"
                    << "To: " << aliasToRealAsNo.at (dstAs) << std::endl;

          for (auto const &beaconsFromSameNbr : sameDstAsBeacons)
            {
              for (auto const &theBeacon : beaconsFromSameNbr.second)
                {
                  if (!theBeacon->isValid)
                    {
                      continue;
                    }
                  std::cout << "\t"
                            << "\t";
                  for (auto const &[attType, attValue] : theBeacon->staticInfoExtension)
                    {
                      if (attType == StaticInfoType::latency)
                        {
                          std::cout << "latency = " << attValue << "; ";
                        }
                      if (attType == StaticInfoType::bw)
                        {
                          std::cout << "BW = " << attValue << "; ";
                        }
                    }

                  if (theBeacon->optimizationTarget != NULL)
                    {
                      for (auto const &[criteriaType, criteriaCoef] :
                           theBeacon->optimizationTarget->criteria)
                        {
                          std::cout << "iface_group_id = "
                                    << theBeacon->optimizationTarget->targetIfGroup << "; ";
                          if (criteriaType == StaticInfoType::latency)
                            {
                              std::cout << "latency_coef = " << criteriaCoef << "; ";
                            }
                          if (criteriaType == StaticInfoType::bw)
                            {
                              std::cout << "BW_coef = " << criteriaCoef << "; ";
                            }
                        }
                    }
                  std::cout << std::endl;
                }
            }
        }
    }
}

void
PostSimulationEvaluations::PrintNoBeaconsPerInterfacePerDstOrOpt ()
{
  std::cout
      << "####################################### sent beacons on each interface in each period"
         "#######################################"
      << std::endl;
  for (uint16_t time = 0; time <= lastBeaconingEventTime.ToInteger (Time::MIN);
       time += beaconingPeriod.ToInteger (Time::MIN))
    {
      std::cout << time << "|";
      for (uint32_t i = 0; i < asNodes.GetN (); ++i)
        {
          ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));
          auto const &countersPerPeriod =
              as->GetBeaconServer ()->GetBeaconsSentPerInterfacePerPeriod ().at (time);
          for (uint32_t ifIndex = 0; ifIndex < as->GetNDevices (); ++ifIndex)
            {
              uint64_t noSentBeacons = countersPerPeriod.at (ifIndex);
              std::cout << aliasToRealAsNo.at (as->asNumber) << ":" << ifIndex << "="
                        << noSentBeacons << ",";
            }
        }
      std::cout << std::endl;
    }

  std::cout << "####################################### cumulative sent beacons on each interface "
               "#######################################"
            << std::endl;

  for (uint32_t i = 0; i < asNodes.GetN (); ++i)
    {
      ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));
      for (uint32_t ifIndex = 0; ifIndex < as->GetNDevices (); ++ifIndex)
        {
          uint64_t noSentBeacons =
              as->GetBeaconServer ()->GetBeaconsSentPerInterface ().at (ifIndex);
          std::cout << aliasToRealAsNo.at (as->asNumber) << ":" << ifIndex << "="
                    << noSentBeacons
                    << ",";
        }
    }

  std::cout << std::endl;

  std::cout << "####################################### sent beacons per interface per destination "
               "#######################################"
            << std::endl;

  std::unordered_map<uint16_t, std::vector<uint32_t>> beaconsSentPerDstPerInterface;

  for (uint32_t i = 0; i < asNodes.GetN (); ++i)
    {
      ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));
      auto &counters = as->GetBeaconServer ()->GetBeaconsSentPerDstPerInterfacePerPeriod ();
      for (uint32_t ifIndex = 0; ifIndex < as->GetNDevices (); ++ifIndex)
        {
          auto &countersPerInterface = counters.at (ifIndex);
          for (auto const &[dstAs, counter] : countersPerInterface)
            {
              if (beaconsSentPerDstPerInterface.find (dstAs) ==
                  beaconsSentPerDstPerInterface.end ())
                {
                  beaconsSentPerDstPerInterface.insert (
                      std::make_pair (dstAs, std::vector<uint32_t> ()));
                }
              beaconsSentPerDstPerInterface.at (dstAs).push_back (counter);
            }
        }
    }

  for (auto const &[dstAs, counters] : beaconsSentPerDstPerInterface)
    {
      std::cout << aliasToRealAsNo.at (dstAs) << "|";
      for (auto const counter : counters)
        {
          std::cout << counter << ",";
        }
      std::cout << std::endl;
    }

  std::cout << "####################################### push sent beacons per interface per "
               "optimization target "
               "#######################################"
            << std::endl;

  std::unordered_map<const OptimizationTarget *, std::vector<uint32_t>>
      beaconsSentPerOptPerInterface;

  for (uint32_t i = 0; i < asNodes.GetN (); ++i)
    {
      ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));
      auto &counters =
          as->GetBeaconServer ()->GetPushBasedBeaconsSentPerOptPerInterfacePerPeriod ();

      for (uint32_t ifIndex = 0; ifIndex < as->GetNDevices (); ++ifIndex)
        {
          auto &countersPerInterface = counters.at (ifIndex);
          for (auto const &[optTarget, counter] : countersPerInterface)
            {
              if (beaconsSentPerOptPerInterface.find (optTarget) ==
                  beaconsSentPerOptPerInterface.end ())
                {
                  beaconsSentPerOptPerInterface.insert (
                      std::make_pair (optTarget, std::vector<uint32_t> ()));
                }
              beaconsSentPerOptPerInterface.at (optTarget).push_back (counter);
            }
        }
    }

  for (auto const &[optTarget, counters] : beaconsSentPerOptPerInterface)
    {
      std::cout << int64_t (optTarget) << "|" << aliasToRealAsNo.at (optTarget->targetAs)
                << "|" << optTarget->targetId << "|" << optTarget->targetIfGroup << "|";
      for (auto const counter : counters)
        {
          std::cout << counter << ",";
        }
      std::cout << std::endl;
    }

  std::cout << "####################################### pull sent beacons per interface per "
               "optimization target "
               "#######################################"
            << std::endl;

  beaconsSentPerOptPerInterface.clear ();

  for (uint32_t i = 0; i < asNodes.GetN (); ++i)
    {
      ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));
      auto &counters =
          as->GetBeaconServer ()->GetPullBasedBeaconsSentPerOptPerInterfacePerPeriod ();

      for (uint32_t ifIndex = 0; ifIndex < as->GetNDevices (); ++ifIndex)
        {
          auto &countersPerInterface = counters.at (ifIndex);
          for (auto const &[optTarget, counter] : countersPerInterface)
            {
              if (beaconsSentPerOptPerInterface.find (optTarget) ==
                  beaconsSentPerOptPerInterface.end ())
                {
                  beaconsSentPerOptPerInterface.insert (
                      std::make_pair (optTarget, std::vector<uint32_t> ()));
                }
              beaconsSentPerOptPerInterface.at (optTarget).push_back (counter);
            }
        }
    }

  for (auto const &[optTarget, counters] : beaconsSentPerOptPerInterface)
    {
      std::cout << int64_t (optTarget) << "|" << aliasToRealAsNo.at (optTarget->targetAs)
                << "|" << aliasToRealAsNo.at (0xFFFF - optTarget->targetId) << "|";
      for (auto const counter : counters)
        {
          std::cout << counter << ",";
        }
      std::cout << std::endl;
    }
}

void
PostSimulationEvaluations::PrintNoBeaconsPerInterface ()
{
  std::cout << "####################################### cumulative sent beacons on each interface "
               "#######################################"
            << std::endl;
  std::cout << "link"
            << "\t"
            << "sent beacons" << std::endl;
  for (uint32_t i = 0; i < asNodes.GetN (); ++i)
    {
      ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));
      for (uint32_t ifIndex = 0; ifIndex < as->GetNDevices (); ++ifIndex)
        {
          uint64_t noSentBeacons =
              as->GetBeaconServer ()->GetBeaconsSentPerInterface ().at (ifIndex);
          std::cout << aliasToRealAsNo.at (as->asNumber) << ":" << ifIndex << "\t"
                    << noSentBeacons
                    << std::endl;
        }
    }

  for (Time t = Seconds (0.0); t < lastBeaconingEventTime; t += beaconingPeriod)
    {
      std::cout << "####################################### frequencies of sent beacons at Time "
                << t << " #######################################" << std::endl;

      std::map<uint32_t, uint32_t> frequenciesOfSentBeaconNumbers;
      for (uint32_t i = 0; i < asNodes.GetN (); ++i)
        {
          ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));
          for (uint32_t ifIndex = 0; ifIndex < as->GetNDevices (); ++ifIndex)
            {
              uint32_t noSentBeacons = as->GetBeaconServer ()
                                             ->GetBeaconsSentPerInterfacePerPeriod ()
                                             .at (t.ToInteger (Time::MIN))
                                             .at (ifIndex);

              if (frequenciesOfSentBeaconNumbers.find (noSentBeacons) !=
                  frequenciesOfSentBeaconNumbers.end ())
                {
                  frequenciesOfSentBeaconNumbers.at (noSentBeacons)++;
                }
              else
                {
                  frequenciesOfSentBeaconNumbers.insert (std::make_pair (noSentBeacons, 1));
                }
            }
        }

      std::cout << "sent beacons on a link"
                << "\t"
                << "frequency" << std::endl;
      for (auto const &bwdFreqPair : frequenciesOfSentBeaconNumbers)
        {
          std::cout << bwdFreqPair.first << "\t" << bwdFreqPair.second << std::endl;
        }
    }
}

void
PostSimulationEvaluations::PrintConsumedBwAtEachPeriod ()
{
  for (Time t = Seconds (0.0); t < lastBeaconingEventTime; t += beaconingPeriod)
    {
      std::cout
          << "####################################### frequencies of consumed bandwidth at Time "
          << t << " #######################################" << std::endl;

      std::map<uint32_t, uint32_t> frequenciesOfConsumedBwd;
      for (uint32_t i = 0; i < asNodes.GetN (); ++i)
        {
          ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));
          for (uint32_t ifIndex = 0; ifIndex < as->GetNDevices (); ++ifIndex)
            {
              uint32_t consumedBwd = as->GetBeaconServer ()
                                          ->GetBytesSentPerInterfacePerPeriod ()
                                          .at (t.ToInteger (Time::MIN))
                                          .at (ifIndex);

              if (frequenciesOfConsumedBwd.find (consumedBwd) != frequenciesOfConsumedBwd.end ())
                {
                  frequenciesOfConsumedBwd.at (consumedBwd)++;
                }
              else
                {
                  frequenciesOfConsumedBwd.insert (std::make_pair (consumedBwd, 1));
                }
            }
        }

      std::cout << "consumed bandwidth on a link"
                << "\t"
                << "frequency" << std::endl;
      for (auto const &bwdFreqPair : frequenciesOfConsumedBwd)
        {
          std::cout << bwdFreqPair.first << "\t" << bwdFreqPair.second << std::endl;
        }
    }
}

void
PostSimulationEvaluations::PrintDistributionOfPathsWithSpecificHopCount ()
{
  for (uint32_t pathLength = 1; pathLength <= 4; ++pathLength)
    {
      std::cout << "######################################### frequencies of path counts per "
                   "destination AS with "
                   "hop count: "
                << pathLength << "#########################################" << std::endl;
      std::map<uint64_t, uint64_t> frequenciesOfPathCountsPerDstAsWithCertainLength;
      for (uint32_t i = 0; i < asNodes.GetN (); ++i)
        {
          for (auto const &dstAsBeaconsPair :
               DynamicCast<ScionAs> (asNodes.Get (i))->GetBeaconServer ()->GetBeaconStore ())
            {
              uint64_t numberOfPathsWithCertainLength = 0;
              if (dstAsBeaconsPair.second.find (pathLength) == dstAsBeaconsPair.second.end ())
                {
                  continue;
                }
              else
                {
                  numberOfPathsWithCertainLength =
                      dstAsBeaconsPair.second.at (pathLength).size ();
                }

              if (frequenciesOfPathCountsPerDstAsWithCertainLength.find (
                      numberOfPathsWithCertainLength) !=
                  frequenciesOfPathCountsPerDstAsWithCertainLength.end ())
                {
                  frequenciesOfPathCountsPerDstAsWithCertainLength.at (
                      numberOfPathsWithCertainLength)++;
                }
              else
                {
                  frequenciesOfPathCountsPerDstAsWithCertainLength.insert (
                      std::make_pair (numberOfPathsWithCertainLength, 1));
                }
            }
        }

      std::cout << "path count per source AS"
                << "\t"
                << "frequency" << std::endl;
      for (auto const &countFreqPair : frequenciesOfPathCountsPerDstAsWithCertainLength)
        {
          std::cout << countFreqPair.first << "\t" << countFreqPair.second << std::endl;
        }
    }
}

void
PostSimulationEvaluations::PrintMinimumLatencyDist ()
{
  std::cout << "###################################################### MINIMUM "
               "LATENCY####################################"
            << std::endl;

  std::map<float, int> distribution;
  for (uint32_t i = 0; i < asNodes.GetN (); i++)
    {
      ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));
      for (uint32_t j = 0; j < asNodes.GetN (); ++j)
        {
          if (i == j)
            continue;
          float minLatency = std::numeric_limits<float>::max ();
          for (auto const &lenBeaconsPair : as->GetBeaconServer ()->GetBeaconStore ().at (j))
            {
              for (auto const &theBeacon : lenBeaconsPair.second)
                {
                  assert (theBeacon->path.size () != 1 ||
                          theBeacon->staticInfoExtension.at (StaticInfoType::latency) ==
                              (float) 0);
                  if (theBeacon->staticInfoExtension.at (StaticInfoType::latency) < minLatency)
                    {
                      minLatency = theBeacon->staticInfoExtension.at (StaticInfoType::latency);
                    }
                }
            }

          if (distribution.find (minLatency) == distribution.end ())
            {
              distribution.insert (std::make_pair (minLatency, 0));
            }
          distribution.at (minLatency)++;
        }
    }

  int cumulativeCounter = 0;
  for (auto const &entry : distribution)
    {
      float latency = entry.first;
      distribution.at (latency) += cumulativeCounter;
      cumulativeCounter = distribution.at (latency);
    }

  for (auto const &entry : distribution)
    {
      std::cout << entry.first << "\t" << (double) entry.second / cumulativeCounter << std::endl;
    }
}

void
PostSimulationEvaluations::PrintPathNoDistribution ()
{
  std::cout << "############################################# Path No Distribution "
               "##################################"
            << std::endl;
  std::map<uint32_t, uint32_t> distribution;
  for (uint32_t i = 0; i < asNodes.GetN (); ++i)
    {
      ScionAs *node = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));
      for (auto const &dstCountPair : node->GetBeaconServer ()->GetValidBeaconsCountPerDstAs ())
        {
          if (distribution.find (dstCountPair.second) == distribution.end ())
            {
              distribution.insert (std::make_pair (dstCountPair.second, 0));
            }

          distribution.at (dstCountPair.second) = distribution.at (dstCountPair.second) + 1;
        }
    }

  uint32_t cumulativeCounter = 0;
  for (auto const &pathCntCntPair : distribution)
    {
      distribution.at (pathCntCntPair.first) =
          distribution.at (pathCntCntPair.first) + cumulativeCounter;
      cumulativeCounter = distribution.at (pathCntCntPair.first);
    }

  for (auto const &entry : distribution)
    {
      std::cout << entry.first << "\t" << (double) entry.second / cumulativeCounter << std::endl;
    }
}

void
PostSimulationEvaluations::PrintPathPollutionIndex ()
{
  int x = 5;
  std::map<double, uint32_t> minDistribution;
  std::map<double, uint32_t> meanDistribution;
  std::map<double, uint32_t> topXMeanDistribution;

  std::cout
      << "############################################# Latencies of paths; Min pollution; Mean of "
      << x << "-least-polluting; Mean of all ##################################" << std::endl;

  for (uint32_t i = 0; i < asNodes.GetN (); ++i)
    {
      ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));

      for (uint32_t j = 0; j < asNodes.GetN (); ++j)
        {
          if (i == j)
            continue;

          float min = std::numeric_limits<float>::max ();

          float mean = (float) 0;
          int cnt = 0;

          float topXMean = (float) 0;
          int topXCnt = 0;
          std::multimap<float, float> pollutionIndexes;

          float latencyOfPathWithMinPollution = 0;
          float avgLatencyOfXLeastPollutingPaths = 0;
          float avgLatencyOfAllPaths = 0;

          for (auto const &lenBeaconsPair : as->GetBeaconServer ()->GetBeaconStore ().at (j))
            {
              for (auto const &theBeacon : lenBeaconsPair.second)
                {
                  pollutionIndexes.insert (std::make_pair (
                      theBeacon->staticInfoExtension.at (StaticInfoType::co2),
                      theBeacon->staticInfoExtension.at (StaticInfoType::latency)));

                  cnt++;
                  mean += theBeacon->staticInfoExtension.at (StaticInfoType::co2);
                  avgLatencyOfAllPaths +=
                      theBeacon->staticInfoExtension.at (StaticInfoType::latency);

                  if (theBeacon->staticInfoExtension.at (StaticInfoType::co2) < min)
                    {
                      min = theBeacon->staticInfoExtension.at (StaticInfoType::co2);
                      latencyOfPathWithMinPollution =
                          theBeacon->staticInfoExtension.at (StaticInfoType::latency);
                    }
                }
            }

          for (auto const &pollutionIndexLatencyPair : pollutionIndexes)
            {
              if (topXCnt >= x)
                break;
              topXMean += pollutionIndexLatencyPair.first;
              avgLatencyOfXLeastPollutingPaths += pollutionIndexLatencyPair.second;
              topXCnt++;
            }
          pollutionIndexes.clear ();

          topXMean /= topXCnt;
          if (topXMeanDistribution.find (topXMean) == topXMeanDistribution.end ())
            {
              topXMeanDistribution.insert (std::make_pair (topXMean, 0));
            }

          mean /= cnt;
          if (meanDistribution.find (mean) == meanDistribution.end ())
            {
              meanDistribution.insert (std::make_pair (mean, 0));
            }

          if (minDistribution.find (min) == minDistribution.end ())
            {
              minDistribution.insert (std::make_pair (min, 0));
            }

          topXMeanDistribution.at (topXMean)++;
          meanDistribution.at (mean)++;
          minDistribution.at (min)++;

          avgLatencyOfAllPaths /= cnt;
          avgLatencyOfXLeastPollutingPaths /= topXCnt;

          std::cout << aliasToRealAsNo.at (i) << "\t" << aliasToRealAsNo.at (j) << "\t"
                    << latencyOfPathWithMinPollution << "\t"
                    << avgLatencyOfXLeastPollutingPaths
                    << "\t" << avgLatencyOfAllPaths << std::endl;
        }
    }

  uint32_t cumulativeCounter = 0;
  for (auto const &pathCntCntPair : minDistribution)
    {
      minDistribution.at (pathCntCntPair.first) =
          minDistribution.at (pathCntCntPair.first) + cumulativeCounter;
      cumulativeCounter = minDistribution.at (pathCntCntPair.first);
    }

  std::cout << "############################################# MIN Pollution Index Distribution "
               "##################################"
            << std::endl;
  for (auto const &entry : minDistribution)
    {
      std::cout << entry.first << "\t" << (double) entry.second / cumulativeCounter << std::endl;
    }

  cumulativeCounter = 0;
  for (auto const &pathCntCntPair : topXMeanDistribution)
    {
      topXMeanDistribution.at (pathCntCntPair.first) =
          topXMeanDistribution.at (pathCntCntPair.first) + cumulativeCounter;
      cumulativeCounter = topXMeanDistribution.at (pathCntCntPair.first);
    }

  std::cout << "############################################# TOP" << x
            << " Paths MEAN Pollution Index Distribution ##################################"
            << std::endl;
  for (auto const &entry : topXMeanDistribution)
    {
      std::cout << entry.first << "\t" << (double) entry.second / cumulativeCounter << std::endl;
    }

  cumulativeCounter = 0;
  for (auto const &pathCntCntPair : meanDistribution)
    {
      meanDistribution.at (pathCntCntPair.first) =
          meanDistribution.at (pathCntCntPair.first) + cumulativeCounter;
      cumulativeCounter = meanDistribution.at (pathCntCntPair.first);
    }

  std::cout << "############################################# MEAN Pollution Index Distribution "
               "##################################"
            << std::endl;
  for (auto const &entry : meanDistribution)
    {
      std::cout << entry.first << "\t" << (double) entry.second / cumulativeCounter << std::endl;
    }
}

void
PostSimulationEvaluations::PrintLeastPollutingPaths ()
{
  std::ifstream bgpPathsFile ("/cluster/scratch/tabaeias/BGP_path_and_pollution.txt");
  std::string line;

  std::map<std::tuple<int, int>, int> bgpPathNo = std::map<std::tuple<int, int>, int> ();
  while (getline (bgpPathsFile, line))
    {
      std::vector<std::string> fields;
      fields = Split (line, '|', fields);

      int from = std::stoi (fields[0]);
      int to = std::stoi (fields[1]);
      int bgpPaths = std::stoi (fields[6]);

      bgpPathNo.insert (std::make_pair (std::make_pair (from, to), bgpPaths));
    }
  bgpPathsFile.close ();

  std::cout.precision (10);
  for (uint32_t i = 0; i < asNodes.GetN (); ++i)
    {
      ScionAs *as1 = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));

      for (uint32_t j = 0; j < asNodes.GetN (); ++j)
        {
          if (i == j)
            continue;

          ScionAs *as2 = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (j)));

          if (bgpPathNo.find (std::make_pair (aliasToRealAsNo.at (as1->asNumber),
                                                aliasToRealAsNo.at (as2->asNumber))) ==
              bgpPathNo.end ())
            {
              continue;
            }

          std::map<double, std::map<double, std::set<Beacon *>>> sortedBeaconsByPollutionByLatency =
                  std::map<double, std::map<double, std::set<Beacon *>>> ();
          SortBeaconsByPollutionByLatency (asNodes, as1, as2, beaconingPolicyStr,
                                           sortedBeaconsByPollutionByLatency);

          double minPollution = sortedBeaconsByPollutionByLatency.begin ()->first;
          double latencyOfMinPollution =
              sortedBeaconsByPollutionByLatency.begin ()->second.begin ()->first;
          Beacon *minPollutingBeacon =
              *sortedBeaconsByPollutionByLatency.begin ()->second.begin ()->second.begin ();

          double avgPollutionTop5 = 0;
          double avgPollutionTopN = 0;
          double avgPollutionAll = 0;

          double avgLatencyTop5 = 0;
          double avgLatencyTopN = 0;
          double avgLatencyAll = 0;

          int counter = 0;
          int n = bgpPathNo.at (std::make_pair (aliasToRealAsNo.at (as1->asNumber),
                                                  aliasToRealAsNo.at (as2->asNumber)));

          for (auto const &[pollution, latency2SetOfBeaconsMap] :
               sortedBeaconsByPollutionByLatency)
            {
              for (auto const &[latency, setOfBeacons] : latency2SetOfBeaconsMap)
                {
                  for (uint32_t k = 0; k < setOfBeacons.size (); ++k)
                    {
                      avgPollutionAll += pollution;
                      avgLatencyAll += latency;

                      if (counter < 5)
                        {
                          avgPollutionTop5 += pollution;
                          avgLatencyTop5 += latency;
                        }

                      if (counter < n)
                        {
                          avgPollutionTopN += pollution;
                          avgLatencyTopN += latency;
                        }

                      counter += 1;
                    }
                }
            }

          avgPollutionAll /= counter;
          avgLatencyAll /= counter;

          if (counter < 5)
            {
              avgPollutionTop5 /= counter;
              avgLatencyTop5 /= counter;
            }
          else
            {
              avgPollutionTop5 /= 5;
              avgLatencyTop5 /= 5;
            }

          if (counter < n)
            {
              avgPollutionTopN /= counter;
              avgLatencyTopN /= counter;
            }
          else
            {
              avgPollutionTopN /= n;
              avgLatencyTopN /= n;
            }

          std::cout << aliasToRealAsNo.at (as1->asNumber) << "|"
                    << aliasToRealAsNo.at (as2->asNumber) << "|" << minPollution << "|"
                    << avgPollutionTop5 << "|" << avgPollutionTopN << "|" << avgPollutionAll << "|" << latencyOfMinPollution << "|" << avgLatencyTop5 << "|"
                    << avgLatencyTopN
                    << "|" << avgLatencyAll << "|" << counter << "|";

          int hopCnt = 0;
          std::vector<LinkInformation_t>::reverse_iterator hop =
              minPollutingBeacon->path.rbegin ();
          for (; hop != minPollutingBeacon->path.rend (); ++hop)
            {
              if (hopCnt != 0)
                {
                  std::cout << ", ";
                }
              std::cout << aliasToRealAsNo.at (SECOND_LOWER_16_BITS (*hop)) << ":"
                        << LOWER_16_BITS (*hop) << ", "
                        << aliasToRealAsNo.at (UPPER_16_BITS (*hop)) << ":"
                        << SECOND_UPPER_16_BITS (*hop);
              hopCnt++;
            }

          std::cout << "|";

          hopCnt = 0;
          hop = minPollutingBeacon->path.rbegin ();
          for (; hop != minPollutingBeacon->path.rend (); ++hop)
            {
              if (hopCnt != 0)
                {
                  std::cout << ", ";
                }
              ScionAs *hopAs = dynamic_cast<ScionAs *> (
                  PeekPointer (asNodes.Get (SECOND_LOWER_16_BITS (*hop))));
              std::cout << "(" << hopAs->interfacesCoordinates.at (LOWER_16_BITS (*hop)).first
                        << "," << hopAs->interfacesCoordinates.at (LOWER_16_BITS (*hop)).second
                        << ")";
              hopCnt++;
            }

          std::cout << std::endl;
        }
    }
}

void
PostSimulationEvaluations::PrintBestPerHopPollutionIndexes ()
{
  std::cout
      << "############################################### BestPerHopLatencyAndPollutionIndexes "
         "########################################################################"
      << std::endl;
  for (uint32_t i = 0; i < asNodes.GetN (); ++i)
    {
      ScionAs *as = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));
      for (auto const &theIfacesList1 : as->interfacesPerNeighborAs)
        {
          uint16_t theNeighbor1 = theIfacesList1.first;
          for (auto const &theIfacesList2 : as->interfacesPerNeighborAs)
            {
              uint16_t theNeighbor2 = theIfacesList2.first;
              if (theNeighbor1 == theNeighbor2)
                continue;
              double minLatency = std::numeric_limits<double>::max ();
              for (uint16_t iface1 : theIfacesList1.second)
                {
                  for (uint16_t iface2 : theIfacesList2.second)
                    {
                      if (as->latenciesBetweenInterfaces.at (iface1).at (iface2) < minLatency)
                        {
                          minLatency = as->latenciesBetweenInterfaces.at (iface1).at (iface2);
                        }
                    }
                }
              std::cout << aliasToRealAsNo.at (theNeighbor1) << " "
                        << aliasToRealAsNo.at (as->asNumber) << " "
                        << aliasToRealAsNo.at (theNeighbor2) << "\t"
                        << minLatency *
                               ((GreenBeaconing *) as->GetBeaconServer ())->GetDirtyEnergyRatio ()
                        << "\t" << minLatency << "\t" << std::endl;
            }
        }
    }
}

void
PostSimulationEvaluations::PrintConsumedBwForBeaconing ()
{
  for (uint32_t i = 0; i < asNodes.GetN (); ++i)
    {
      ScionAs *asNode = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));
      for (auto const &el : asNode->GetBeaconServer ()->GetBytesSentPerInterfacePerPeriod ())
        {
          auto const &vector = el.second;
          std::cerr << "\nNode: " << realToAliasAsNo.at (asNode->asNumber) << " at time 0."
                    << std::endl;
          for (auto const &element : vector)
            {
              std::cerr << element << " ";
            }
        }
    }
}

void
PostSimulationEvaluations::PrintBeaconStores ()
{
  for (uint32_t i = 0; i < asNodes.GetN (); ++i)
    {
      ScionAs *asNode = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));
      std::cout << "From: " << realToAliasAsNo.at (asNode->asNumber) << std::endl;

      for (auto const &dstAsBeaconsPair : asNode->GetBeaconServer ()->GetBeaconStore ())
        {
          uint16_t dstAs = dstAsBeaconsPair.first;
          auto const &sameDstAsBeacons = dstAsBeaconsPair.second;

          std::cout << "\t"
                    << "To: " << realToAliasAsNo.at (dstAs) << std::endl;

          for (auto const &beaconsFromSameNbr : sameDstAsBeacons)
            {
              for (auto const &theBeacon : beaconsFromSameNbr.second)
                {
                  if (!theBeacon->isValid)
                    {
                      continue;
                    }
                  std::cout << "\t"
                            << "\t";
                  uint32_t hopCnt = 0;
                  std::vector<LinkInformation_t>::reverse_iterator hop = theBeacon->path.rbegin ();
                  for (; hop != theBeacon->path.rend (); ++hop)
                    {
                      if (hopCnt != 0)
                        {
                          std::cout << ", ";
                        }
                      std::cout << realToAliasAsNo.at (SECOND_LOWER_16_BITS (*hop)) << ":"
                                << LOWER_16_BITS (*hop) << ", "
                                << realToAliasAsNo.at (UPPER_16_BITS (*hop)) << ":"
                                << SECOND_UPPER_16_BITS (*hop);
                      hopCnt++;
                    }
                  std::cout << "; ";
                  std::cout << "latency = "
                            << theBeacon->staticInfoExtension.at (StaticInfoType::latency);
                  std::cout << "; ";
                  std::cout << "BWD = "
                            << theBeacon->staticInfoExtension.at (StaticInfoType::bw);
                  std::cout << std::endl;
                }
            }
        }
    }
}

void
PostSimulationEvaluations::PrintNumberOfValidBeaconEntriesInBeaconStore ()
{
  for (uint32_t i = 0; i < asNodes.GetN (); ++i)
    {
      ScionAs *asNode = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));
      std::cerr << "Beacon Store on Node: " << realToAliasAsNo.at (asNode->asNumber)
                << std::endl;
      for (auto const &srcAsBeaconsPair : asNode->GetBeaconServer ()->GetBeaconStore ())
        {
          auto const &srcAs = srcAsBeaconsPair.first;
          auto const &beacons = srcAsBeaconsPair.second;

          int count = 0;
          for (auto const &lenBeaconSetPair : beacons)
            {
              auto const &length = lenBeaconSetPair.first;
              auto const &beaconSet = lenBeaconSetPair.second;
              std::cout << length;
              for (auto b : beaconSet)
                {
                  if (b->isValid)
                    {
                      count++;
                    }
                }
            }
          std::cerr << "\t" << realToAliasAsNo.at (srcAs) << ":" << count << std::endl;
        }
      std::cerr << std::endl;
    }
}

void
PostSimulationEvaluations::InvestigateAffectedTimeServers ()
{
  std::cout << "########################### InvestigateAffectedTimeServers "
               "#####################################"
            << std::endl;
  std::vector<std::pair<std::string, uint32_t>> pathSelections = {
      std::make_pair ("random", 1), std::make_pair ("short", 1), std::make_pair ("random", 5),
      std::make_pair ("short", 5), std::make_pair ("disjoint", 5)};

  std::vector<std::map<uint32_t, std::unordered_set<Ia_t>>> preCalculatedInherentlyMaliciousAses;
  std::unordered_set<Ia_t> inherentlyMaliciousAses;
  std::set<Ia_t> benign_ases;
  uint32_t numAllAses = asNodes.GetN ();

  for (uint32_t i = 0; i < numAllAses; ++i)
    {
      Ia_t iaAddr = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)))->iaAddr;
      benign_ases.insert (iaAddr);
    }

  uint32_t maliciousIncrementalStep = std::ceil (numAllAses / 100);

  for (uint32_t rpt = 0; rpt < 20; ++rpt)
    {
      std::map<uint32_t, std::unordered_set<Ia_t>> numInherentMaliciousToMalicious;
      preCalculatedInherentlyMaliciousAses.push_back (numInherentMaliciousToMalicious);

      benign_ases.insert (inherentlyMaliciousAses.begin (), inherentlyMaliciousAses.end ());
      inherentlyMaliciousAses.clear ();

      for (uint32_t numInherentMalicious = maliciousIncrementalStep;
           numInherentMalicious <= std::ceil (numAllAses / 3) + maliciousIncrementalStep;
           numInherentMalicious += maliciousIncrementalStep)
        {
          std::set<Ia_t> new_inherently_malicious;
          std::set<Ia_t> new_benign;

          std::sample (benign_ases.begin (), benign_ases.end (),
                       std::inserter (new_inherently_malicious, new_inherently_malicious.begin ()),
                       maliciousIncrementalStep, std::random_device{});

          std::set_difference (std::make_move_iterator (benign_ases.begin ()),
                               std::make_move_iterator (benign_ases.end ()),
                               new_inherently_malicious.begin (), new_inherently_malicious.end (),
                               std::inserter (new_benign, new_benign.end ()));

          benign_ases.swap (new_benign);
          inherentlyMaliciousAses.insert (new_inherently_malicious.begin (),
                                            new_inherently_malicious.end ());

          preCalculatedInherentlyMaliciousAses.at (rpt).insert (
              std::make_pair (numInherentMalicious, inherentlyMaliciousAses));
        }
    }

  for (auto const &pathSelection : pathSelections)
    {
      std::cout << "******************************** " << pathSelection.second << " "
                << pathSelection.first << " ********************************" << std::endl;

#pragma omp parallel for
      for (uint32_t i = 0; i < numAllAses; ++i)
        {
          ScionAs *scionAs = dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));
          TimeServer *timeServer = dynamic_cast<TimeServer *> (scionAs->GetHost (2));

          timeServer->pathSelection = pathSelection.first;
          timeServer->numberOfPathsToUseForGlobalSync = pathSelection.second;

          timeServer->ConstructSetOfSelectedPaths ();
        }

      for (uint32_t rpt = 0; rpt < 20; ++rpt)
        {
          for (auto const &numInherentMaliciousToMalicious :
               preCalculatedInherentlyMaliciousAses.at (rpt))
            {
              std::unordered_set<Ia_t> inherentlyAndTransitiveMalicious =
                  numInherentMaliciousToMalicious.second;
              uint32_t numInherentMalicious = numInherentMaliciousToMalicious.first;
              bool c = false;
              while (true)
                {
                  c = false;
#pragma omp parallel for
                  for (uint32_t i = 0; i < numAllAses; ++i)
                    {
                      ScionAs *scionAs =
                          dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));

                      if (inherentlyAndTransitiveMalicious.find (scionAs->iaAddr) !=
                          inherentlyAndTransitiveMalicious.end ())
                        {
                          continue;
                        }

                      uint32_t numberOfAffectedDst = 0;
                      TimeServer *timeServer = dynamic_cast<TimeServer *> (scionAs->GetHost (2));

                      assert (timeServer->setOfSelectedPaths.size () >= asNodes.GetN () - 1);

                      for (auto const &[dstIa, selectedPathsToDst] :
                           timeServer->setOfSelectedPaths)
                        {
                          assert (selectedPathsToDst.size () != 0);

                          uint32_t numberOfAffectedPaths = 0;
                          for (auto const &pathSeg : selectedPathsToDst)
                            {
                              uint32_t pathLen = pathSeg->hops.size ();

                              assert (GET_HOP_IA (pathSeg->hops.at (0)) == scionAs->iaAddr);
                              assert (GET_HOP_IA (pathSeg->hops.at (pathLen - 1)) == dstIa);
                              assert (pathLen >= 2);
                              bool pathAffected = false;

                              for (uint32_t j = 1; j < pathLen; ++j)
                                {
                                  uint64_t hop = pathSeg->hops.at (j);
                                  Ia_t hopIa = GET_HOP_IA (hop);
                                  if (inherentlyAndTransitiveMalicious.find (hopIa) !=
                                      inherentlyAndTransitiveMalicious.end ())
                                    {
                                      pathAffected = true;
                                      break;
                                    }
                                }
                              assert (inherentlyAndTransitiveMalicious.find (dstIa) ==
                                          inherentlyAndTransitiveMalicious.end () ||
                                      pathAffected);
                              if (pathAffected)
                                {
                                  numberOfAffectedPaths++;
                                }
                            }
                          assert (inherentlyAndTransitiveMalicious.find (dstIa) ==
                                      inherentlyAndTransitiveMalicious.end () ||
                                  numberOfAffectedPaths == selectedPathsToDst.size ());
                          if (numberOfAffectedPaths * 2 >= selectedPathsToDst.size ())
                            {
                              numberOfAffectedDst++;
                            }
                        }

                      assert (numberOfAffectedDst >= numInherentMalicious);

                      if (3 * numberOfAffectedDst + 1 > numAllAses)
                        {
                          timeServer->affectedByMaliciousAses = true;
                        }
                    }

                  for (uint32_t i = 0; i < numAllAses; ++i)
                    {
                      ScionAs *scionAs =
                          dynamic_cast<ScionAs *> (PeekPointer (asNodes.Get (i)));

                      if (inherentlyAndTransitiveMalicious.find (scionAs->iaAddr) !=
                          inherentlyAndTransitiveMalicious.end ())
                        {
                          continue;
                        }

                      TimeServer *timeServer = dynamic_cast<TimeServer *> (scionAs->GetHost (2));

                      if (timeServer->affectedByMaliciousAses)
                        {
                          inherentlyAndTransitiveMalicious.insert (scionAs->iaAddr);
                          timeServer->affectedByMaliciousAses = false;
                          c = true;
                        }
                    }
                  if (!c)
                    {
                      break;
                    }
                }
              std::cout << numInherentMalicious << "\t"
                        << inherentlyAndTransitiveMalicious.size () << std::endl;
            }
        }
    }
}

void
SortBeaconsByPollutionByLatency (
    NodeContainer &asNodes, ScionAs *as1, ScionAs *as2, std::string beaconingPolicyStr,
    std::map<double, std::map<double, std::set<Beacon *>>> &sortedBeaconsByPollutionByLatency)
{
  if (beaconingPolicyStr == "green_beaconing")
    {
      const std::vector<std::multimap<Ld_t, Beacon *>> &sortedBeaconsPerIngIf =
          ((GreenBeaconing *) as1->GetBeaconServer ())
              ->GetBeaconsSortedByPollution ()
              .at (as2->asNumber);

      for (auto const &sortedBeacons : sortedBeaconsPerIngIf)
        {
          for (auto const &[pollution, beacon] : sortedBeacons)
            {
              sortedBeaconsByPollutionByLatency.insert (
                  std::make_pair (pollution, std::map<double, std::set<Beacon *>> ()));

              double latency = beacon->staticInfoExtension.at (StaticInfoType::latency);
              if (sortedBeaconsByPollutionByLatency.at (pollution).find (latency) ==
                  sortedBeaconsByPollutionByLatency.at (pollution).end ())
                {
                  sortedBeaconsByPollutionByLatency.at (pollution).insert (
                      std::make_pair (latency, std::set<Beacon *> ()));
                }
              sortedBeaconsByPollutionByLatency.at (pollution).at (latency).insert (beacon);
            }
        }
    } /*else if (beaconing_policy_str == "baseline") {
            auto const & beacons_from_AS1_to_AS2 = AS1->GetBeaconServer()->beacon_store.at(AS2->as_number);
            for (auto const & [length, beacons] : beacons_from_AS1_to_AS2) {
                for (auto const & beacon : beacons) {
                    double pollution = 0;
                    bool first_hop = true;
                    uint64_t previous_hop = 0;
                    for (uint64_t hop : beacon->the_path) {
                        if (first_hop) {
                            previous_hop = hop;
                            first_hop = false;
                            continue;
                        }

                        uint16_t ingress_if = LOWER_16_BITS(previous_hop);
                        uint16_t egress_if = SECOND_UPPER_16_BITS(hop);
                        assert(SECOND_LOWER_16_BITS(previous_hop) == UPPER_16_BITS(hop));
                        uint16_t as_hop_nr = UPPER_16_BITS(hop);
                        ScionAs* as_hop = dynamic_cast<ScionAs*>(PeekPointer(AS_nodes.Get(as_hop_nr)));


                        pollution += (double) (as_hop->intra_as_energies.at(ingress_if).at(egress_if) * as_hop->dirty_energy_ratio * 700 / 3.6e6);

                        previous_hop = hop;
                    }
                    double latency = (double) beacon->static_info_extension.at(static_info_type_t::LATENCY);

                    if (sorted_beacons_by_pollution_by_latency.find(pollution) == sorted_beacons_by_pollution_by_latency.end()) {
                        sorted_beacons_by_pollution_by_latency.insert(std::make_pair(pollution, std::map<double, std::set<Beacon*>>()));
                    }

                    if (sorted_beacons_by_pollution_by_latency.at(pollution).find(latency) == sorted_beacons_by_pollution_by_latency.at(pollution).end()) {
                        sorted_beacons_by_pollution_by_latency.at(pollution).insert(std::make_pair(latency, std::set<Beacon*> ()));
                    }

                    sorted_beacons_by_pollution_by_latency.at(pollution).at(latency).insert(beacon);
                }
            }
        }*/
}

} // namespace ns3
