//
// Created by chrissy on 10.06.20.
//

#ifndef SCION_BEACONING_SIMMULATOR_SCION_CORE_AS_H
#define SCION_BEACONING_SIMMULATOR_SCION_CORE_AS_H
#include "scion_node.h"

class SCION_Core_As : public SCION_Node{
public:
    void CoreBeaconing() override;

    void IntraISDBeaconing() override;

    void ProcessReceivedBeacons() override;

protected:
    std::unordered_map<uint16_t, std::vector<uint16_t>> select_valid_interfaces() override;
};
#endif //SCION_BEACONING_SIMMULATOR_SCION_CORE_AS_H
