#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/point-to-point-channel.h"
#include "ns3/rapidxml.hpp"
#include <vector>
#include <stdio.h>
#include <omp.h>
#include <map>
#include <unordered_map>
#include <fstream>
#include <istream>
#include <sstream>

#define FIXED_BEACONS_NUMBER_TO_SEND 5
#define FIXED_BEACONS_NUMBER_TO_STORE 50
using namespace ns3;


typedef uint16_t* link_information;
typedef std::vector<link_information> path;
struct beacon {
  int64_t initiation_time, expiration_time, next_initiation_time, next_expiration_time, arrival_time;
  path* the_path;
  std::string key;
  bool is_new;
};

typedef std::vector <beacon*> beacons_with_same_length;
typedef std::map<uint16_t, beacons_with_same_length*> beacons_with_same_src_as; //map length of paths to apths

int64_t now;
Time beaconing_period;
int64_t expiration_period;

namespace ns3 {

  class myNode : public Node {

    public:

      uint16_t as_number; 
      std::unordered_map <uint16_t, beacons_with_same_src_as* > beacon_store;
      std::unordered_map <uint16_t, uint64_t> valid_beacons_per_src_as_counters;
      std::unordered_map <uint16_t, uint64_t> next_round_valid_beacons_per_src_as_counters;
      std::unordered_map <std::string, beacon*> beacon_existence_check_map;
      uint64_t *beacons_sent_per_link;
      std::list<uint64_t> beacons_sent_per_period;
      uint64_t sent_count = 0;


      myNode(uint16_t as_number, uint32_t system_id) : Node(system_id), as_number (as_number) {}

      void set_beacons_sent_per_link() {
        beacons_sent_per_link = new uint64_t[GetNDevices()];
        for (uint64_t i = 0; i < GetNDevices(); ++i) {
          beacons_sent_per_link[i] = 0;
        }
      }



      void UpdateBeaconStoreAndCountersBeforeBeaconing()
      {
        now = Simulator::Now().ToInteger(Time::NS);
        valid_beacons_per_src_as_counters.clear();
        next_round_valid_beacons_per_src_as_counters.clear();

        if (as_number == 0) {
          std::cout << now << std::endl;
        }


        for (auto const & pair:beacon_existence_check_map) {
          beacon *the_beacon = pair.second;
          if (the_beacon->is_new && the_beacon->arrival_time < now) {
            the_beacon->is_new = false;


            the_beacon->initiation_time = the_beacon->next_initiation_time;
            the_beacon->expiration_time = the_beacon->next_expiration_time;
          }

          if (the_beacon->expiration_time > now) {
            if (valid_beacons_per_src_as_counters.find(*the_beacon->the_path->at(0)) != valid_beacons_per_src_as_counters.end()) {
              valid_beacons_per_src_as_counters.at(*the_beacon->the_path->at(0)) = valid_beacons_per_src_as_counters.at(*the_beacon->the_path->at(0)) + 1;
            } else {
              valid_beacons_per_src_as_counters.insert(std::make_pair(*the_beacon->the_path->at(0), 1));
            }

          }

          if (the_beacon->expiration_time > now || the_beacon->expiration_time == -1) {
            if (next_round_valid_beacons_per_src_as_counters.find(*the_beacon->the_path->at(0)) != next_round_valid_beacons_per_src_as_counters.end()) {
              next_round_valid_beacons_per_src_as_counters.at(*the_beacon->the_path->at(0)) = next_round_valid_beacons_per_src_as_counters.at(*the_beacon->the_path->at(0)) + 1;
            } else {
              next_round_valid_beacons_per_src_as_counters.insert(std::make_pair(*the_beacon->the_path->at(0), 1));
            }

          }

        }

        std::cout << valid_beacons_per_src_as_counters.size() << std::endl;
      }


      void UpdateCountersAfterBeaconing() 
      {

        uint64_t tmp_sent_count = 0;
        for (uint64_t i = 0; i < GetNDevices(); ++i) {
          tmp_sent_count += beacons_sent_per_link[i];
        }

        beacons_sent_per_period.push_back(tmp_sent_count - sent_count);

        sent_count = tmp_sent_count;
      }

      void DoBeaconing()
      {

	UpdateBeaconStoreAndCountersBeforeBeaconing();

#pragma omp parallel for
        for (uint32_t i = 0; i < GetNDevices(); ++i) {
          Ptr<PointToPointNetDevice> device_to_send_to =  DynamicCast<PointToPointNetDevice> (GetDevice(i));
          Ptr<PointToPointChannel> channel = DynamicCast<PointToPointChannel> (device_to_send_to->GetChannel());
          uint32_t wire = device_to_send_to == channel->GetSource (0) ? 0 : 1;
          Ptr<PointToPointNetDevice> dst = channel->GetDestination (wire);

          uint16_t src_if_index = (uint16_t) device_to_send_to->GetIfIndex();
          uint16_t dst_if_index = (uint16_t)  dst->GetIfIndex();
          uint16_t dst_as_number = (DynamicCast<myNode> (dst->GetNode ()))->as_number;
          Ptr<myNode> dst_as = (DynamicCast<myNode> (dst->GetNode ()));

          for (auto const & beacon_store_entry:beacon_store) {
            SelectBeaconsAndSend (beacon_store_entry.second, src_if_index, dst_as_number, dst_if_index, dst_as);
          }

          GenerateBeaconAndSend (NULL, src_if_index, dst_as_number, dst_if_index, dst_as);
        }

	UpdateCountersAfterBeaconing();
            
      }

      void SelectBeaconsAndSend (beacons_with_same_src_as* equal_src_as_beacons, uint16_t src_if_index, uint16_t dst_as_number, uint16_t dst_if_index, Ptr<myNode> dst_as) {
        unsigned int counter = 0;

        for (auto const& equal_length_beacons : *equal_src_as_beacons) {
          for (auto const& the_beacon : *equal_length_beacons.second) {
            if (counter == FIXED_BEACONS_NUMBER_TO_SEND) { // Not more than 5 beacons
              return;
            }       

            if (the_beacon->expiration_time <= now) { // Do not send expired beacons
              continue;
            }

            bool generates_loop = false;
            for (auto const & link_info : *the_beacon->the_path) { // remove loops
              if (*link_info == dst_as_number) {
                generates_loop = true;
                break;
              }
            }

            if (!generates_loop) {
              GenerateBeaconAndSend (the_beacon, src_if_index, dst_as_number, dst_if_index, dst_as);
              counter++;
            }
          }
        }
      }


      void GenerateBeaconAndSend (beacon* old_beacon, uint16_t src_if_index, uint16_t dst_as_number, uint16_t dst_if_index, Ptr<myNode> dst_as) {
        beacons_sent_per_link[src_if_index]++;

	uint16_t src_as;
        uint16_t path_len;

        std::string key;

        if (old_beacon == NULL){
          src_as = as_number;
          path_len = 1;
        } else {
          key = old_beacon->key;

          src_as = *old_beacon->the_path->at(0);
          path_len = old_beacon->the_path->size() + 1;

        }

        key = key + std::string((char*) &as_number, 2);


        if (dst_as->beacon_existence_check_map.find(key) != dst_as->beacon_existence_check_map.end()) {
          if (dst_as->beacon_existence_check_map.at(key)->is_new) {
            dst_as->beacon_existence_check_map.at(key)->initiation_time = dst_as->beacon_existence_check_map.at(key)->next_initiation_time;
            dst_as->beacon_existence_check_map.at(key)->expiration_time = dst_as->beacon_existence_check_map.at(key)->next_expiration_time;
          }

          if (old_beacon == NULL) {
            dst_as->beacon_existence_check_map.at(key)->next_initiation_time = now;
            dst_as->beacon_existence_check_map.at(key)->next_expiration_time = now + expiration_period;
          } else {
            dst_as->beacon_existence_check_map.at(key)->next_initiation_time = old_beacon->initiation_time;
            dst_as->beacon_existence_check_map.at(key)->next_expiration_time = old_beacon->expiration_time;
          }
          dst_as->beacon_existence_check_map.at(key)->is_new = true;              
          dst_as->beacon_existence_check_map.at(key)->arrival_time = now;
          return;
        }

        if (dst_as->next_round_valid_beacons_per_src_as_counters.find(src_as) != dst_as->next_round_valid_beacons_per_src_as_counters.end()) {

          if (dst_as->next_round_valid_beacons_per_src_as_counters.at(src_as) >= FIXED_BEACONS_NUMBER_TO_STORE) {
            return;
          }

          dst_as->next_round_valid_beacons_per_src_as_counters.at(src_as) = dst_as->next_round_valid_beacons_per_src_as_counters.at(src_as) + 1;

        } else {
          dst_as->next_round_valid_beacons_per_src_as_counters.insert(std::make_pair(src_as, 1));
        }


        beacon* new_beacon = new beacon;
        path*   new_path = new path;
        new_beacon->the_path = new_path;

        uint16_t* link_info = new uint16_t[4];
        link_info[0] = as_number;
        link_info[1] = src_if_index;
        link_info[2] = dst_as_number;
        link_info[3] = dst_if_index;

        new_beacon->initiation_time = -1;
        new_beacon->expiration_time = -1;
        new_beacon->arrival_time = now;
        new_beacon->key = key;
        new_beacon->is_new = true;

        if (old_beacon == NULL) {
          new_beacon->next_initiation_time = now;
          new_beacon->next_expiration_time = now + expiration_period;
        } else {
          new_beacon->next_initiation_time = old_beacon->initiation_time;
          new_beacon->next_expiration_time = old_beacon->expiration_time;;

          *new_path = *(old_beacon->the_path);
        }

        new_path->push_back(link_info);
        dst_as->beacon_existence_check_map.insert(std::make_pair(key, new_beacon));

        if (dst_as->beacon_store.find(src_as) != dst_as->beacon_store.end() && dst_as->beacon_store.at(src_as)->find(path_len) != dst_as->beacon_store.at(src_as)->end()) {
          dst_as->beacon_store.at(src_as)->at(path_len)->push_back(new_beacon);
        } else if (dst_as->beacon_store.find(src_as) != dst_as->beacon_store.end() && dst_as->beacon_store.at(src_as)->find(path_len) == dst_as->beacon_store.at(src_as)->end()) {
          dst_as->beacon_store.at(src_as)->insert(std::make_pair(path_len, new beacons_with_same_length));
          dst_as->beacon_store.at(src_as)->at(path_len)->push_back(new_beacon);
        } else {
          dst_as->beacon_store.insert(std::make_pair(src_as, new beacons_with_same_src_as));
          dst_as->beacon_store.at(src_as)->insert(std::make_pair(path_len, new beacons_with_same_length));
          dst_as->beacon_store.at(src_as)->at(path_len)->push_back(new_beacon);
        }

      }

  };

}

class PropertyContainer {
  public: 

    std::string getProperty(const std::string &name) const {
      propertiesType::const_iterator it;
      it = this->properties.find(name);

      if(it != this->properties.end())
        return it->second;
      else
        exit(1);

    }


    void setProperty(const std::string &name, const std::string &value) {
      this->properties[name] = value;
    }


    bool hasProperty(const std::string &name) const {
      propertiesType::const_iterator it = this->properties.find(name);

      if(it == this->properties.end()) {
        return false;
      } else {
        return true;
      }

    }


  private:
    typedef std::map <std::string, std::string> propertiesType;
    propertiesType properties;

};


std::string getAttribute(rapidxml::xml_node<>* node, const std::string &name) {
  rapidxml::xml_attribute<> *attr = node->first_attribute(name.c_str());
  if(attr) {
    return attr->value();
  } else {
    return std::string();
  }
}

PropertyContainer parseProperties(rapidxml::xml_node<>* node) {
  PropertyContainer p;
  rapidxml::xml_node<>* curNode = node->first_node("property");

  while(curNode) {
    std::string name = getAttribute(curNode, "name");
    if(name != "") {
      p.setProperty(name, curNode->value());
    }
    curNode = curNode->next_sibling("property");
  }

  return p;
}

  int
main (int argc, char *argv[])
{

  beaconing_period = Time(argv[1]);
  expiration_period = Time(argv[2]).ToInteger(Time::NS);
  std::string file = "/home/tabaeias/workspace/ns-3-allinone/ns-3-dev/topology/" + std::string(argv[4]) + ".xml";

  std::ifstream fin(file.c_str());
  std::ostringstream sstr;
  sstr << fin.rdbuf();

  std::string out_path = "/home/tabaeias/workspace/ns-3-allinone/ns-3-dev/results/baseline_" + std::string(argv[4]) +  "_" + std::string(argv[1]) + "_" + std::string(argv[2]) + "_" + std::string(argv[3]) + ".txt";
  std::ofstream out(out_path);
  std::cout.rdbuf(out.rdbuf()); 

  sstr.flush();
  fin.close();

  std::string xmlData = sstr.str();
  rapidxml::xml_document<> doc;
  doc.parse<0> (&xmlData[0]);

  rapidxml::xml_node<> *rootNode = doc.first_node("topology");
  rapidxml::xml_node<> *curNode;

  if(!rootNode) {
    std::cerr << "Empty topology!" << std::endl;
    return 1;
  }


  NodeContainer nodes;
  int node_counter = 0;
  std::map<int, uint16_t> ASes;


  curNode = rootNode->first_node("node");
  while (curNode) {
    int as_number = std::stoi(getAttribute(curNode, "id"));
    nodes.Add(CreateObject <myNode>(node_counter, 0));

    ASes.insert(std::make_pair(as_number, node_counter));

    node_counter++;


    curNode = curNode->next_sibling("node");
  }


  curNode = rootNode->first_node("link");
  while (curNode) {
    int to = std::stoi(curNode->first_node("to")->value());
    int from = std::stoi(curNode->first_node("from")->value());

    Ptr<Node> fromNode;
    Ptr<Node> toNode;

    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
      if ((DynamicCast<myNode> (nodes.Get(i)))->as_number == ASes.at(to)) {
        toNode = nodes.Get(i);
        break;
      }

    }

    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
      if ((DynamicCast<myNode> (nodes.Get(i)))->as_number == ASes.at(from)) {
        fromNode = nodes.Get(i);
        break;
      }
    }


    PointToPointHelper helper;
    helper.Install(fromNode, toNode);

    PropertyContainer edgeProperties = parseProperties(curNode);
    if(edgeProperties.hasProperty("delay")) {
      helper.SetChannelAttribute ("Delay", StringValue (edgeProperties.getProperty("delay")));
    }

    curNode = curNode->next_sibling("link");
  }

  for (uint64_t i = 0; i < nodes.GetN(); ++i) {
    DynamicCast<myNode> (nodes.Get(i))->set_beacons_sent_per_link();
  }

  Time t = Seconds(0.0);
  while (t < Time(argv[3])) {
    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
      Ptr<myNode> the_node = DynamicCast<myNode> (nodes.Get(i));
      Simulator::Schedule (t, &myNode::DoBeaconing, the_node);
    }
    t = t + beaconing_period;
  }



  Simulator::Stop (Time(argv[3]));
  Simulator::Run ();

  std::map <uint64_t, uint64_t> beacons_per_link_distribution;

  for (uint32_t i = 0; i < nodes.GetN(); ++i) {
    for (uint32_t j = 0; j < nodes.Get(i)->GetNDevices(); ++j) {
      uint64_t the_count = DynamicCast<myNode> (nodes.Get(i))->beacons_sent_per_link[j];
      if (beacons_per_link_distribution.find(the_count) == beacons_per_link_distribution.end()) {
        beacons_per_link_distribution.insert(std::make_pair(the_count, 1));
      } else {
        beacons_per_link_distribution.at(the_count) = beacons_per_link_distribution.at(the_count) + 1;
      }
    }
  }


  for (auto const & counter_pair:beacons_per_link_distribution) {
    std::cout << counter_pair.first << "\t" << counter_pair.second << std::endl;
  }

  Time t2 = Seconds(0.0);
  while (t2 < Time(argv[3])) { 
    uint64_t counter_per_period = 0;
    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
      counter_per_period += DynamicCast<myNode> (nodes.Get(i))->beacons_sent_per_period.front();
      DynamicCast<myNode> (nodes.Get(i))->beacons_sent_per_period.pop_front();
    }

    std::cout << t2 << "\t" << counter_per_period << std::endl;

    t2 = t2 + beaconing_period;
  }




  /*  for (uint32_t i = 0; i < nodes.GetN(); ++i) {       
      for (uint32_t j = 0; j < nodes.GetN(); ++j) {
      if (DynamicCast<myNode> (nodes.Get(i))->beacon_store.find(j) == DynamicCast<myNode> (nodes.Get(i))->beacon_store.end()) {
      std::cout << "0" << "\t";
      continue;
      }
      std::cout << DynamicCast<myNode> (nodes.Get(i))->beacon_store.at(j)->at(0)->size() << "\t";
      }

      std::cout << std::endl;
      }*/

  Simulator::Destroy ();
  return 0;
}
