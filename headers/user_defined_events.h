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
  AnyCallable (std::function<Ret (Args...)> fun) : mAny (fun)
  {
  }

  template <typename... Args>
  Ret
  operator() (Args... args)
  {
    return std::invoke (std::any_cast<std::function<Ret (Args...)>> (mAny),
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

  std::any mAny;
};

template <int N>
struct MyPlaceholder
{
  static MyPlaceholder ph;
};

template <int N>
MyPlaceholder<N> MyPlaceholder<N>::ph;

namespace std {
template <int N>
struct IsPlaceholder<::my_placeholder<N>> : std::integral_constant<int, N>
{
};
} // namespace std

template <class R, class... Types, class U, int... indices>
std::function<R (Types...)>
BindFactory (R (U::*f) (Types...), U *val, std::integer_sequence<int, indices...> /*seq*/)
{
  return std::bind (f, val, MyPlaceholder<indices + 1>::ph...);
}

template <class R, class... Types, class U>
std::function<R (Types...)>
FunctionFactory (R (U::*f) (Types...), U *val)
{
  return BindFactory (f, val, std::make_integer_sequence<int, sizeof...(Types)> ());
}

namespace ns3 {

class UserDefinedEvents
{
public:
  UserDefinedEvents (YAML::Node &config, NodeContainer &asNodes,
                     std::map<int32_t, uint16_t> &realToAliasAsNo,
                     std::map<uint16_t, int32_t> &aliasToRealAsNo)
      : config (config),
        asNodes (asNodes),
        realToAliasAsNo (realToAliasAsNo),
        aliasToRealAsNo (aliasToRealAsNo)
  {
    if (!config["events_file"])
      {
        this->~UserDefinedEvents ();
        return;
      }

    ConstructFuncMap ();
    ReadAndScheduleUserDefinedEvents (config["events_file"].as<std::string> ());
  }

private:
  YAML::Node &config;
  NodeContainer &asNodes;
  std::map<int32_t, uint16_t> &realToAliasAsNo;
  std::map<uint16_t, int32_t> &aliasToRealAsNo;

  std::unordered_map<std::string, AnyCallable<void>> functionToFunctionName;

  void ConstructFuncMap ();

  void ReadAndScheduleUserDefinedEvents (const std::string &eventsFileStr);

  void RunUserSpecifiedEvent (const std::string &funcName, std::vector<std::string> vec);

  void AddAHost (std::string isdNumber, std::string realAsNo, std::string localAddress);

  void LinkDown (std::string isdNumber, std::string realAsNo, std::string ifId);
  void LinkUp (std::string isdNumber, std::string realAsNo, std::string ifId);

  void SendAPacket (std::string srcIsdNumber, std::string realSrcAsNo,
                      std::string srcLocalAddress, std::string dstIsdNumber,
                      std::string realDstAsNo, std::string dstLocalAddress,
                      std::string pyloadSize);

  void SendPacketBatch (std::string srcIsdNumber, std::string realSrcAsNo,
                          std::string srcLocalAddress, std::string dstIsdNumber,
                          std::string realDstAsNo, std::string dstLocalAddress,
                          std::string pyloadSize, std::string noPkts);

  void TimeReferencesDown ();
  void TimeReferencesUp ();
};
} // namespace ns3
#endif //SCION_SIMULATOR_USER_DEFINED_EVENTS_H
