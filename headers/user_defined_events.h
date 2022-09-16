//
// Created by Seyedali Tabaeiaghdaei on 21.03.22.
//

#ifndef SCION_SIMULATOR_USER_DEFINED_EVENTS_H
#define SCION_SIMULATOR_USER_DEFINED_EVENTS_H

#include <any>
#include <fstream>
#include <functional>
#include <unordered_map>
#include <yaml-cpp/yaml.h>

#include "src/core/model/simulator.h"

#include "src/SCION/headers/json.hpp"
#include "src/SCION/headers/path_segment.h"
#include "src/SCION/headers/scion_as.h"
#include "src/SCION/headers/scion_packet.h"

template <typename Ret>
struct AnyCallable
{
  AnyCallable ()
  {
  }

  template <typename... Args>
  AnyCallable (std::function<Ret (Args...)> fun) : m_any (fun)
  {
  }

  template <typename... Args>
  Ret
  operator() (Args... args)
  {
    return std::invoke (std::any_cast<std::function<Ret (Args...)>> (m_any),
                        std::forward<Args> (args)...);
  }

  template <std::size_t... S, typename T>
  Ret
  operator() (const std::vector<T> &vec, std::index_sequence<S...>)
  {
    return operator() (vec[S]...);
  }

  template <std::size_t size, typename T>
  Ret
  operator() (const std::vector<T> &vec)
  {
    return operator() (vec, std::make_index_sequence<size> ());
  }

  std::any m_any;
};

template <int N>
struct my_placeholder
{
  static my_placeholder ph;
};

template <int N>
my_placeholder<N> my_placeholder<N>::ph;

namespace std {
template <int N>
struct is_placeholder<::my_placeholder<N>> : std::integral_constant<int, N>
{
};
} // namespace std

template <class R, class... Types, class U, int... indices>
std::function<R (Types...)>
bind_factory (R (U::*f) (Types...), U *val, std::integer_sequence<int, indices...> /*seq*/)
{
  return std::bind (f, val, my_placeholder<indices + 1>::ph...);
}

template <class R, class... Types, class U>
std::function<R (Types...)>
function_factory (R (U::*f) (Types...), U *val)
{
  return bind_factory (f, val, std::make_integer_sequence<int, sizeof...(Types)> ());
}

namespace ns3 {

class UserDefinedEvents
{
public:
  UserDefinedEvents (YAML::Node &config, NodeContainer &AS_nodes,
                     std::map<int32_t, uint16_t> &real_to_alias_as_no,
                     std::map<uint16_t, int32_t> &alias_to_real_as_no)
      : config (config),
        AS_nodes (AS_nodes),
        real_to_alias_as_no (real_to_alias_as_no),
        alias_to_real_as_no (alias_to_real_as_no)
  {
    if (!config["events_file"])
      {
        this->~UserDefinedEvents ();
        return;
      }

    construct_func_map ();
    read_and_schedule_user_defined_events (config["events_file"].as<std::string> ());
  }

private:
  YAML::Node &config;
  NodeContainer &AS_nodes;
  std::map<int32_t, uint16_t> &real_to_alias_as_no;
  std::map<uint16_t, int32_t> &alias_to_real_as_no;

  std::unordered_map<std::string, AnyCallable<void>> function_name_to_function;

  void construct_func_map ();

  void read_and_schedule_user_defined_events (const std::string &events_file_str);

  void run_user_specified_event (const std::string &func_name, std::vector<std::string> vec);

  void add_a_host (std::string isd_number, std::string real_as_no, std::string local_address);

  void link_down (std::string isd_number, std::string real_as_no, std::string if_id);
  void link_up (std::string isd_number, std::string real_as_no, std::string if_id);

  void send_a_packet (std::string src_isd_number, std::string real_src_as_no,
                      std::string src_local_address, std::string dst_isd_number,
                      std::string real_dst_as_no, std::string dst_local_address,
                      std::string pyload_size);

  void send_packet_batch (std::string src_isd_number, std::string real_src_as_no,
                          std::string src_local_address, std::string dst_isd_number,
                          std::string real_dst_as_no, std::string dst_local_address,
                          std::string pyload_size, std::string no_pkts);

  void time_references_down ();
  void time_references_up ();
};
} // namespace ns3
#endif //SCION_SIMULATOR_USER_DEFINED_EVENTS_H
