#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mpi-interface.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/point-to-point-remote-channel.h"
#include "ns3/point-to-point-net-device.h"
#include "ns3/rapidxml.hpp"
#include <vector>
#include <map>
#include <assert.h>
#include <fstream>
#include <istream>
#include <sstream>
#ifdef NS3_MPI
#include <mpi.h>
#endif

using namespace ns3;

typedef uint16_t* link_information;
typedef std::vector<link_information> path;
typedef std::tuple<Ptr<PointToPointNetDevice>, int64_t*, int64_t*, path*> beacon;
typedef std::vector<beacon*> beacons_with_same_length;
typedef std::map<uint16_t, beacons_with_same_length*> beacons_with_same_src_as; //map length of paths to apths

namespace ns3 {

class myNode : public Node {

public:

   uint16_t as_number; 
   std::map <int16_t, beacons_with_same_src_as* > beacon_store;
   std::vector<beacon*> selected_beacons = std::vector<beacon*> (5, NULL);

   myNode(uint32_t as_number, uint32_t system_id) : Node(system_id), as_number (as_number) {}

bool
NonPromiscReceiveFromDevice (Ptr<NetDevice> device, Ptr<const Packet> packet, uint16_t protocol, const Address &from)
{

   uint16_t packet_size = (uint16_t) packet->GetSize();
   uint16_t path_length = (packet_size - 16) >> 3;

   uint8_t* buff = new uint8_t[packet_size];
   packet->CopyData(buff, packet_size);


   beacon* the_beacon = new beacon;
   std::get<0>(*the_beacon) = DynamicCast<PointToPointNetDevice> (device);
   std::get<1>(*the_beacon) = ((int64_t*) buff);
   std::get<2>(*the_beacon) = ((int64_t*) (buff + 8));


   path* the_path = new path;

   for (uint16_t i = 16; i < packet_size; i += 8) {
   	the_path->push_back((uint16_t*) (buff + i));
   }

   std::get<3>(*the_beacon) = the_path;
   uint16_t src_as = *the_path->front();

   if (beacon_store.find(src_as) != beacon_store.end()) {
	beacons_with_same_src_as* equal_src_as_beacons = beacon_store.at(src_as);
	if (equal_src_as_beacons->find(path_length) != equal_src_as_beacons->end()) {
		beacons_with_same_length* equal_length_beacons = equal_src_as_beacons->at(path_length);
		
		for (uint32_t i = 0; i < equal_length_beacons->size(); ++i) {
			uint16_t j = 0;
			path* tmp_path = std::get<3> (*equal_length_beacons->at(i));
			for (; j < path_length; ++j) {
				if (*tmp_path->at(j) != *the_path->at(j)) { // if all ases in the path are equal, paths are equal, since
					break;								// there is only one link between two ases now
				}	
			}

			if (j == path_length) {
			       if (*std::get<2>(*the_beacon) > *std::get<2> (*(equal_length_beacons->at(i)))) {
					equal_length_beacons->at(i) = the_beacon;
				}
				return false;
			}
		}


		equal_length_beacons->push_back(the_beacon);
	} else {
		beacons_with_same_length* equal_length_beacons = new beacons_with_same_length;
		equal_length_beacons->push_back(the_beacon);
		equal_src_as_beacons->insert(std::make_pair(path_length,  equal_length_beacons));	
	}
   } else {
	beacons_with_same_length* equal_length_beacons = new beacons_with_same_length;
        equal_length_beacons->push_back(the_beacon);
	beacons_with_same_src_as* equal_src_as_beacons = new beacons_with_same_src_as;
	equal_src_as_beacons->insert(std::make_pair(path_length, equal_length_beacons));
	beacon_store.insert(std::make_pair(src_as, equal_src_as_beacons));
   }

   return false;
}


void
StartSimulation()
{
        Simulator::Schedule (Time("10min"), &myNode::DoBeaconing, this);

}

void
DoBeaconing()
{	

	for (uint32_t i = 0; i < GetNDevices(); ++i) {
		Ptr<PointToPointNetDevice> device_to_send_to =  DynamicCast<PointToPointNetDevice> (GetDevice(i));
		Ptr<PointToPointChannel> channel = DynamicCast<PointToPointChannel> (device_to_send_to->GetChannel());
        	uint32_t wire = device_to_send_to == channel->GetSource (0) ? 0 : 1;
        	Ptr<PointToPointNetDevice> dst = channel->GetDestination (wire);
        	uint16_t dst_if_index = (uint16_t)  dst->GetIfIndex();
        	uint16_t dst_as_number = (DynamicCast<myNode> (dst->GetNode ()))->as_number;

		for (auto const& beacon_store_entry : beacon_store) {
		       	SelectBeacons (beacon_store_entry.second, device_to_send_to, dst_as_number);
			PropagateBeacons (device_to_send_to, dst, dst_as_number, dst_if_index);
		}

		CreatePacket(NULL, device_to_send_to, dst, dst_as_number, dst_if_index);
	}

        ScheduleNextBeaconPeriod();

}




void 
PropagateBeacons (Ptr<PointToPointNetDevice> device_to_send_to, Ptr<PointToPointNetDevice> dst, uint16_t dst_as_number, uint16_t dst_if_index) {
	unsigned int i = 0;
	while (i < selected_beacons.size() && selected_beacons.at(i) != NULL) {
		CreatePacket(selected_beacons.at(i), device_to_send_to, dst, dst_as_number, dst_if_index);
		i++;
	}
}

void 
SelectBeacons (beacons_with_same_src_as* equal_src_as_beacons, Ptr<PointToPointNetDevice> device_to_send_to, uint16_t dst_as_number) {
	unsigned int counter = 0;

	for (auto const& equal_length_beacons : *equal_src_as_beacons) {
		for (auto const& the_beacon : *equal_length_beacons.second) {
			if (counter == selected_beacons.size()) { // Not more than 5 beacons
				return;
			}	

			if (Time(*std::get<2>(*the_beacon)) < Simulator::Now()) { // Do not send expired beacons
				continue;
			}

			bool generates_loop = false;
			for (auto const & link_info : *std::get<3>(*the_beacon)) { // remove loops
				if (*link_info == dst_as_number) {
					generates_loop = true;
					break;
				}
			}

			if (!generates_loop && std::get<0>(*the_beacon) != device_to_send_to) {
				selected_beacons.at(counter) = the_beacon;

				counter++;
			}

		}
	}

	if (counter < selected_beacons.size()) {
                selected_beacons.at(counter) = NULL; 

	}
}

void
ScheduleNextBeaconPeriod() {
/*	if (as_number == 6939) {
		std::cout << Simulator::Now() << std::endl;
		std::cout << "number of source ASes is = " << beacon_store.size() << std::endl;

		for (auto const & src_as_beacons_pair : beacon_store) {
			std::cout << "\tSource AS = " << src_as_beacons_pair.first << std::endl;
			for (auto const & length_beacons_pair : *src_as_beacons_pair.second) {
				std::cout << "\t\tpath length = " << length_beacons_pair.first << std::endl;
				std::cout << "\t\t\tnumber of paths = " << length_beacons_pair.second->size() << std::endl;
				for (auto const & beacon : *length_beacons_pair.second) {
					for (auto const & link_info : *std::get<3>(*beacon)) {
						std::cout << "\t\t\t\t"  <<  *(int16_t*) link_info << " " << (int16_t) link_info[2] << " " << (int16_t) link_info[4] << " " << (int16_t) link_info[6] << " " << Time (*std::get<2>(*beacon))  << std::endl;
					}
					std::cout << std::endl;
				}
			}
		}*/
//	}

        Simulator::Schedule (Time("10min"), &myNode::DoBeaconing, this);

}

void
CreatePacket (beacon* the_beacon, Ptr<PointToPointNetDevice> device_to_send_to, Ptr<PointToPointNetDevice> dst, uint16_t dst_as_number, uint16_t dst_if_index) {
         static uint8_t *buff = new uint8_t[8024]; 

	 int64_t initialization_time;
	 int64_t expiration_time;
         path *the_path;

 	if (the_beacon == NULL) {
		initialization_time = Simulator::Now().ToInteger(Time::NS);
		expiration_time  = (Simulator::Now() + Time("6h")).ToInteger(Time::NS);
	} else {
		initialization_time = *std::get<1>(*the_beacon); //std::get<1>(*the_beacon).ToInteger(Time::NS);
		expiration_time = *std::get<2>(*the_beacon);//std::get<2>(*the_beacon).ToInteger(Time::NS);
		the_path = std::get<3>(*the_beacon);
	}


	buff [0] = ((initialization_time) >> 0) & 0xFF;
  	buff [1] = ((initialization_time) >> 8) & 0xFF;
  	buff [2] = ((initialization_time) >> 16) & 0xFF;
  	buff [3] = ((initialization_time) >> 24) & 0xFF;
  	buff [4] = ((initialization_time) >> 32) & 0xFF;
  	buff [5] = ((initialization_time) >> 40) & 0xFF;
  	buff [6] = ((initialization_time) >> 48) & 0xFF;
  	buff [7] = ((initialization_time) >> 56) & 0xFF;
 
	buff [8] = ((expiration_time) >> 0) & 0xFF;
        buff [9] = ((expiration_time) >> 8) & 0xFF;
        buff [10] = ((expiration_time) >> 16) & 0xFF;
        buff [11] = ((expiration_time) >> 24) & 0xFF;
        buff [12] = ((expiration_time) >> 32) & 0xFF;
        buff [13] = ((expiration_time) >> 40) & 0xFF;
        buff [14] = ((expiration_time) >> 48) & 0xFF;
        buff [15] = ((expiration_time) >> 56) & 0xFF;


	int i = 16;
	if (the_beacon != NULL) {
		
		for (auto const &link_info : *the_path) {
			buff[i] = (link_info[0] >> 0) & 0xFF;
			buff[i + 1] = (link_info[0] >> 8) & 0xFF; 

			buff[i + 2] = (link_info[1] >> 0) & 0xFF; 
                	buff[i + 3] = (link_info[1] >> 8) & 0xFF; 

			buff[i + 4] = (link_info[2] >> 0) & 0xFF; 
                	buff[i + 5] = (link_info[2] >> 8) & 0xFF; 

			buff[i + 6] = (link_info[3] >> 0) & 0xFF; 
                	buff[i + 7] = (link_info[3] >> 8) & 0xFF;

			i += 8;
	
		}
	}


	buff[i] = (as_number >> 0) & 0xFF;
	buff[i + 1] = (as_number >> 8) & 0xFF;

	buff[i + 2] = (((uint16_t) (device_to_send_to->GetIfIndex())) >> 0) & 0xFF;
	buff[i + 3] = (((uint16_t) (device_to_send_to->GetIfIndex())) >> 8) & 0xFF;

	buff[i + 4] = (dst_as_number >> 0) & 0xFF;
	buff[i + 5] = (dst_as_number >> 8) & 0xFF;

	buff[i + 6] = (dst_if_index >> 0) & 0xFF;
	buff[i + 7] = (dst_if_index >> 8) & 0xFF;

	i += 8;

	Ptr<Packet> p = Create<Packet> (buff, i);
        DynamicCast<PointToPointChannel> (device_to_send_to->GetChannel())->TransmitStart(p, device_to_send_to, Simulator::Now());
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
#ifdef NS3_MPI


    int CORES_NUMBER = std::stoi (argv[1]);


  // Distributed simulation setup; by default use granted time window algorithm.
  GlobalValue::Bind ("SimulatorImplementationType",
		      StringValue ("ns3::DistributedSimulatorImpl"));

  // Enable parallel simulator with the command line arguments
  MpiInterface::Enable (&argc, &argv);


  uint32_t systemId = MpiInterface::GetSystemId ();


  std::string file = "/home/seyedali/Documents/beaconing_project/pycaida/caida1000.xml";

  std::ifstream fin(file.c_str());
  std::ostringstream sstr;
  sstr << fin.rdbuf();

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
	nodes.Add(CreateObject <myNode>(node_counter, (node_counter % CORES_NUMBER)));
        
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
  

   for (uint32_t i = 0; i < nodes.GetN(); ++i) {
	if (nodes.Get(i)->GetSystemId () == systemId) {
		(DynamicCast<myNode>(nodes.Get(i)))->StartSimulation();

	}
  }



  Simulator::Stop (Hours (16));
  Simulator::Run ();
  Simulator::Destroy ();
  // Exit the MPI execution environment
  MpiInterface::Disable ();
  return 0;
#else
  NS_FATAL_ERROR ("Can't use distributed simulator without MPI compiled in");
#endif
}
