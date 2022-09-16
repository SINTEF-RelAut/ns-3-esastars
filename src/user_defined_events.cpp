//
// Created by Seyedali Tabaeiaghdaei on 21.03.22.
//

#include "src/SCION/headers/user_defined_events.h"
#include "src/SCION/headers/time_server.h"

namespace ns3 {
void
UserDefinedEvents::run_user_specified_event (const std::string &func_name,
                                             std::vector<std::string> vec)
{
  switch (vec.size ())
    {
    case 0:
      function_name_to_function[func_name].operator()<0> (vec);
      break;
    case 1:
      function_name_to_function[func_name].operator()<1> (vec);
      break;
    case 2:
      function_name_to_function[func_name].operator()<2> (vec);
      break;
    case 3:
      function_name_to_function[func_name].operator()<3> (vec);
      break;
    case 4:
      function_name_to_function[func_name].operator()<4> (vec);
      break;
    case 5:
      function_name_to_function[func_name].operator()<5> (vec);
      break;
    case 6:
      function_name_to_function[func_name].operator()<6> (vec);
      break;
    case 7:
      function_name_to_function[func_name].operator()<7> (vec);
      break;
    case 8:
      function_name_to_function[func_name].operator()<8> (vec);
      break;
    case 9:
      function_name_to_function[func_name].operator()<9> (vec);
      break;
    case 10:
      function_name_to_function[func_name].operator()<10> (vec);
      break;
    default:
      break;
    }
}

void
UserDefinedEvents::construct_func_map ()
{
  function_name_to_function["add_host"] = function_factory (&UserDefinedEvents::add_a_host, this);
  function_name_to_function["link_down"] = function_factory (&UserDefinedEvents::link_down, this);
  function_name_to_function["link_up"] = function_factory (&UserDefinedEvents::link_up, this);
  function_name_to_function["send_packet"] =
      function_factory (&UserDefinedEvents::send_a_packet, this);
  function_name_to_function["send_packet_batch"] =
      function_factory (&UserDefinedEvents::send_packet_batch, this);
  function_name_to_function["time_references_down"] =
      function_factory (&UserDefinedEvents::time_references_down, this);
  function_name_to_function["time_references_up"] =
      function_factory (&UserDefinedEvents::time_references_up, this);
}

void
UserDefinedEvents::read_and_schedule_user_defined_events (const std::string &events_file_str)
{
  nlohmann::json events_json;
  std::ifstream events_file (events_file_str);
  events_file >> events_json;
  events_file.close ();

  for (auto const &event : events_json.at ("events"))
    {
      Time time = Time ((std::string) event["time"]);
      std::string func_name = (std::string) event["type"];
      std::vector<std::string> args_v;

      for (uint32_t i = 0; i < event["args"].size (); ++i)
        {
          args_v.push_back ((std::string) event["args"][i]);
        }

      Simulator::Schedule (time, &UserDefinedEvents::run_user_specified_event, this, func_name,
                           args_v);
    }
}

void
UserDefinedEvents::add_a_host (std::string isd_number, std::string real_as_no,
                               std::string local_address)
{
}

void
UserDefinedEvents::link_down (std::string isd_number, std::string real_as_no, std::string if_id)
{
}
void
UserDefinedEvents::link_up (std::string isd_number, std::string real_as_no, std::string if_id)
{
}

void
UserDefinedEvents::send_a_packet (std::string src_isd_number, std::string real_src_as_no,
                                  std::string src_local_address, std::string dst_isd_number,
                                  std::string real_dst_as_no, std::string dst_local_address,
                                  std::string pyload_size)
{
}

void
UserDefinedEvents::send_packet_batch (std::string src_isd_number, std::string real_src_as_no,
                                      std::string src_local_address, std::string dst_isd_number,
                                      std::string real_dst_as_no, std::string dst_local_address,
                                      std::string pyload_size, std::string no_pkts)
{
}

void
UserDefinedEvents::time_references_down ()
{
  for (uint32_t i = 0; i < AS_nodes.GetN (); ++i)
    {
      SCION_AS *scion_as = dynamic_cast<SCION_AS *> (PeekPointer (AS_nodes.Get (i)));
      TimeServer *time_server = dynamic_cast<TimeServer *> (scion_as->GetHost (2));

      time_server->reference_time_type = REFERENCE_TIME_TYPE::OFF;
    }
}

void
UserDefinedEvents::time_references_up ()
{
  for (uint32_t i = 0; i < AS_nodes.GetN (); ++i)
    {
      SCION_AS *scion_as = dynamic_cast<SCION_AS *> (PeekPointer (AS_nodes.Get (i)));
      TimeServer *time_server = dynamic_cast<TimeServer *> (scion_as->GetHost (2));

      time_server->reference_time_type = REFERENCE_TIME_TYPE::ON;
    }
}
} // namespace ns3
