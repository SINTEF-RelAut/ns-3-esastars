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
RunParallelEvents (HostAddr_t hostAddr, MEM memPtr)
{
  omp_set_num_threads (g_numCore);
#pragma omp parallel for schedule(dynamic)
  for (uint32_t i = 0; i < g_nodes.GetN (); ++i)
    {
      ScionAs *node = dynamic_cast<ScionAs *> (PeekPointer (g_nodes.Get (i)));
      ((dynamic_cast<OBJ> (node->GetHost (hostAddr)))->*memPtr) ();
    }
}

template <typename MEM>
void
RunParallelEvents (MEM memPtr)
{
  omp_set_num_threads (g_numCore);
#pragma omp parallel for schedule(dynamic)
  for (uint32_t i = 0; i < g_nodes.GetN (); ++i)
    {
      ScionAs *node = dynamic_cast<ScionAs *> (PeekPointer (g_nodes.Get (i)));
      ((node->GetBeaconServer ())->*memPtr) ();
    }
}
} // namespace ns3
#endif //SCION_SIMULATOR_RUN_PARALLEL_EVENTS_H
