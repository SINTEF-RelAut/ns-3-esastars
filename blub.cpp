void BeaconingStrategy::GenerateBeaconAndSend(beacon *old_beacon, uint16_t self_egress_if_no, uint16_t remote_ingress_if_no, SCION_Node* node,
                                                SCION_Node* remote_as, ld latency, ld bwd, bool immediate, ld latency_for_immediate) {
    uint16_t beacon_origin_as_no;
    std::string key;
    uint16_t remote_as_no = remote_as->as_number;
    bool immediate_src = false;
    bool immediate_non_src = false;

    // Even if the remote AS ends up ignoring the beacon later on, we update the interface values anyways since, in the real deployment,
    // we need to send the beacon before the remote AS can decide if it will be ignored.
    if (old_beacon == NULL) {
        beacon_origin_as_no = node->as_number;
        int64_t t = node->now - node->now % node->beaconing_period.GetInteger(); // 600s? ~ 10min, Equivalent to int(node->now / 600 000 000 000) but divisions are expensive.
        node->bytes_sent_per_interface_per_period.at(t).at(self_egress_if_no) += (BEACON_HEADER_SIZE + BEACON_HOP_SIZE);
        // *** For immediately disseminating beacons received from neighbor source as
        if(remote_as->valid_beacons_count_per_src_as.find(beacon_origin_as_no) == remote_as->valid_beacons_count_per_src_as.end()
           && remote_as->next_round_valid_beacons_count_per_src_as.find(beacon_origin_as_no) == remote_as->next_round_valid_beacons_count_per_src_as.end()){
            immediate_src = true;
        }
    } else {
        key = old_beacon->key;
        beacon_origin_as_no = *old_beacon->the_path->at(0);
        int64_t t = node->now - node->now % node->beaconing_period.GetInteger(); // Equivalent to int(node->now / 600 000 000 000) but divisions are expensive.
        node->bytes_sent_per_interface_per_period.at(t).at(self_egress_if_no) += (BEACON_HEADER_SIZE + BEACON_HOP_SIZE + BEACON_HOP_SIZE * old_beacon->the_path->size());
    }

    if (immediate) { // Indicates that this is part of an immediate beacon dissemination (only set in processImmediateReceive)
        // src_AS_no not found in next_round beacon store. Or less than 5 beacons in next round store from this AS.
        if (remote_as->next_round_valid_beacons_count_per_src_as.find(beacon_origin_as_no) == remote_as->next_round_valid_beacons_count_per_src_as.end() ||
            remote_as->next_round_valid_beacons_count_per_src_as.at(beacon_origin_as_no) < MAX_IMMEDIATE_BEACONS) {
            immediate_non_src = true;
        }
    }
    // ***

    key = key + std::string((char *) &node->as_number, 2) + std::string((char *) &self_egress_if_no, 2);

    // If the beacon is already in the remote_ases beacon store
    if (remote_as->path_map_to_beacon.find(key) != remote_as->path_map_to_beacon.end()) {
        if (old_beacon == NULL) {
            remote_as->path_map_to_beacon.at(key)->next_initiation_time = node->now;
            remote_as->path_map_to_beacon.at(key)->next_expiration_time = node->now + node->expiration_period;
        } else {
            remote_as->path_map_to_beacon.at(key)->next_initiation_time = old_beacon->initiation_time;
            remote_as->path_map_to_beacon.at(key)->next_expiration_time = old_beacon->expiration_time;
        }
        remote_as->path_map_to_beacon.at(key)->is_new = true;
        return;
    }

    // Update statistics & check if you are sending too many beacons
    if (remote_as->next_round_valid_beacons_count_per_src_as.find(beacon_origin_as_no) != // This check makes sure that the old beacon could not have been null. If this was the case the remote AS would not have already seen this as
        remote_as->next_round_valid_beacons_count_per_src_as.end()) {
        if (remote_as->next_round_valid_beacons_count_per_src_as.at(beacon_origin_as_no) >= FIXED_BEACONS_NUMBER_TO_STORE) {
            // Call via remote ASes node since this is the strategy that matters
            auto remote_as_no = remote_as->as_number;
            ld score = CalculateBeaconScore(remote_as, latency, bwd);
            CriteriaMatching* remote_as_strategy = dynamic_cast<CriteriaMatching*>(remote_as->strategy);
            // Check against the lowest score beacons if we need to replace one
            std::multimap <ld, beacon* >::iterator it = remote_as_strategy->beacons_sorted_by_score.at(src_as)->begin();
            if (it->first < score) {
                beacon* lower_score_beacon = it->second;
                uint16_t path_len = lower_score_beacon->the_path->size();
                remote_as_strategy->beacons_sorted_by_score.at(src_as)->erase(it);
                remote_as->path_map_to_beacon.erase(lower_score_beacon->key);
                remote_as->beacon_store.at(src_as)->at(path_len)->erase(lower_score_beacon);
                if (remote_as->beacon_store.at(src_as)->at(path_len)->empty()) {
                    remote_as->beacon_store.at(src_as)->erase(path_len);
                }

                if (lower_score_beacon->is_valid) {
                    remote_as->valid_beacons_count_per_src_as.at(src_as)--;
                }

                // Old beacon may be null
                std::vector<link_information>* the_path;
                int64_t next_initiation_time;
                int64_t next_expiration_time;
                if(old_beacon == NULL){
                    the_path = new std::vector<link_information>();
                    next_initiation_time = node->now;
                    next_expiration_time = next_initiation_time + node->expiration_period;
                } else {
                    the_path = old_beacon->the_path;
                    next_initiation_time = old_beacon->initiation_time;
                    next_expiration_time = old_beacon->expiration_time;
                }

                *lower_score_beacon->the_path = *the_path;

                uint16_t *link_info = new uint16_t[4];
                link_info[0] = node->as_number;
                link_info[1] = self_egress_if_no;
                link_info[2] = remote_as_no;
                link_info[3] = remote_ingress_if_no;

                lower_score_beacon->the_path->push_back(link_info);
                lower_score_beacon->key = key;
                lower_score_beacon->initiation_time = -1;
                lower_score_beacon->expiration_time = -1;
                lower_score_beacon->next_initiation_time = next_initiation_time;
                lower_score_beacon->next_expiration_time = next_expiration_time;
                lower_score_beacon->is_new = true;
                lower_score_beacon->is_valid = false;
                lower_score_beacon->bwd_stat = bwd;
                lower_score_beacon->latency_stat = latency;
                path_len = lower_score_beacon->the_path->size();

                remote_as_strategy->beacons_sorted_by_score.at(src_as)->insert(std::make_pair(score, lower_score_beacon));
                remote_as->path_map_to_beacon.insert(std::make_pair(key, lower_score_beacon));

                if (remote_as->beacon_store.at(src_as)->find(path_len) != remote_as->beacon_store.at(src_as)->end()) {
                    remote_as->beacon_store.at(src_as)->at(path_len)->insert(lower_score_beacon);
                } else {
                    remote_as->beacon_store.at(src_as)->insert(std::make_pair(path_len, new beacons_with_equal_length()));
                    remote_as->beacon_store.at(src_as)->at(path_len)->insert(lower_score_beacon);
                }
                return;
            }
            return; // If the beacon store was full, we are done after this call.
        }
        remote_as->next_round_valid_beacons_count_per_src_as.at(beacon_origin_as_no)++;
    } else {
        remote_as->next_round_valid_beacons_count_per_src_as.insert(std::make_pair(beacon_origin_as_no, 1));
    }

    beacon *new_beacon = new beacon;
    path *new_path = new path;
    new_beacon->the_path = new_path;
    new_beacon->bwd_stat = bwd;
    new_beacon->latency_stat = latency;

    uint16_t *link_info = new uint16_t[4];
    link_info[0] = node->as_number;
    link_info[1] = self_egress_if_no;
    link_info[2] = remote_as_no;
    link_info[3] = remote_ingress_if_no;

    new_beacon->initiation_time = -1;
    new_beacon->expiration_time = -1;
    new_beacon->key = key;
    new_beacon->is_new = true;
    new_beacon->is_valid = false;

    if (old_beacon == NULL) {
        new_beacon->next_initiation_time = node->now;
        new_beacon->next_expiration_time = node->now + node->expiration_period;
    } else {
        new_beacon->next_initiation_time = old_beacon->initiation_time;
        new_beacon->next_expiration_time = old_beacon->expiration_time;

        *new_path = *(old_beacon->the_path);
    }

    new_path->push_back(link_info);
    uint16_t path_len = new_beacon->the_path->size();
    // Here we can be sure, that the beacon is not in the path map yet (checked before).
    remote_as->path_map_to_beacon.insert(std::make_pair(key, new_beacon));

    if (remote_as->beacon_store.find(beacon_origin_as_no) != remote_as->beacon_store.end()){
        if (remote_as->beacon_store.at(beacon_origin_as_no)->find(path_len) != remote_as->beacon_store.at(beacon_origin_as_no)->end()){
            remote_as->beacon_store.at(beacon_origin_as_no)->at(path_len)->insert(new_beacon);
        } else{
            remote_as->beacon_store.at(beacon_origin_as_no)->insert(std::make_pair(path_len, new beacons_with_equal_length ({new_beacon})));
        }
    } else {
        remote_as->beacon_store.insert(std::make_pair(beacon_origin_as_no, new equal_as_beacons_sorted_by_length));
        remote_as->beacon_store.at(beacon_origin_as_no)->insert(std::make_pair(path_len, new beacons_with_equal_length({new_beacon})));
    }

    ld score = CalculateBeaconScore(remote_as, latency, bwd);
    CriteriaMatching* remote_as_strategy = dynamic_cast<CriteriaMatching*>(remote_as->strategy);
    if (remote_as_strategy->beacons_sorted_by_score.find(src_as_no) != remote_as_strategy->beacons_sorted_by_score.end()) {
        remote_as_strategy->beacons_sorted_by_score.at(src_as_no)->insert(std::make_pair(score, new_beacon));
    } else {
        remote_as_strategy->beacons_sorted_by_score.insert(std::make_pair(src_as_no, new std::multimap<ld, beacon*> ()));
        remote_as_strategy->beacons_sorted_by_score.at(src_as_no)->insert(std::make_pair(score, new_beacon));
    }

    if (immediate_src) {
        // This is the processing delay of the receiving BR. Since this beacon can be generated at whichever border router (immediate_src)
        // We don't need to consider the intra_as_latencies
        ns3::Simulator::Schedule(PROCESSING_DELAY, &SCION_Node::ProcessReceivedBeacons, remote_as, beacon_origin_as_no, remote_ingress_if_no, new_beacon);
    }

    if (immediate_non_src) {
        uint64_t delay = (uint64_t) (latency_for_immediate * 1000000);
        // Here the intra_as_latency is relevant and added to the processing delay.
        ns3::Simulator::Schedule(ns3::NanoSeconds(delay) + PROCESSING_DELAY, &SCION_Node::ProcessReceivedBeacons, remote_as, beacon_origin_as_no, remote_ingress_if_no, new_beacon);
    }
}