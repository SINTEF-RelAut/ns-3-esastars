//
// Created by Seyedali Tabaeiaghdaei on 21.03.22.
//

#ifndef SCION_SIMULATOR_RUN_PARALLEL_EVENTS_H
#define SCION_SIMULATOR_RUN_PARALLEL_EVENTS_H

#include "src/SCION/headers/externs.h"
#include "src/SCION/headers/scion_as.h"
#include "src/SCION/headers/scion_packet.h"
#include <omp.h>
#include <yaml-cpp/yaml.h>

namespace ns3 {

template <typename MEM, typename OBJ>
void
RunParallelEvents (host_addr_t host_addr, MEM mem_ptr)
{
  omp_set_num_threads (NUM_CORE);
#pragma omp parallel for schedule(dynamic)
  for (uint32_t i = 0; i < nodes.GetN (); ++i)
    {
      SCION_AS *node = dynamic_cast<SCION_AS *> (PeekPointer (nodes.Get (i)));
      ((dynamic_cast<OBJ> (node->GetHost (host_addr)))->*mem_ptr) ();
    }
}

template <typename MEM>
void
RunParallelEvents (MEM mem_ptr)
{
  omp_set_num_threads (NUM_CORE);
#pragma omp parallel for schedule(dynamic)
  for (uint32_t i = 0; i < nodes.GetN (); ++i)
    {
      SCION_AS *node = dynamic_cast<SCION_AS *> (PeekPointer (nodes.Get (i)));
      ((node->GetBeaconServer ())->*mem_ptr) ();
    }
}
} // namespace ns3
#endif //SCION_SIMULATOR_RUN_PARALLEL_EVENTS_H
