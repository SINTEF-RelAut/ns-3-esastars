## -*- Mode: python; py-indent-offset: 4; indent-tabs-mode: nil; coding: utf-8; -*-

def build(bld):


    env = bld.env
    sim = bld.create_ns3_module('SCION', ['core', 'network'])

    sim.source = [ 'model/beaconing/baseline.cpp',
                   'model/beaconing/beacon.cpp',
                   'model/beaconing/beacon_server.cpp',
                   'model/beaconing/diversity_age_based.cpp',
                   'model/beaconing/latency_optimized_beaconing.cpp',
                   'model/beaconing/green_beaconing.cpp',
                   'model/beaconing/on_demand_optimization.cpp',
                   'model/post_simulation_evaluations.cpp',
                   'model/schedule_periodic_events.cpp',
                   'model/user_defined_events.cpp',
                   'model/pre_simulation_setup.cpp',
                   'model/path_server.cpp',
                   'model/path_segment.cpp',
                   'model/scion_packet.cpp',
                   'model/scion_host.cpp',
                   'model/scion_as.cpp',
                   'model/scion_core_as.cpp',
                   'model/utils.cpp',
                   'model/scion_capable_node.cpp',
                   'model/border_router.cpp',
                   'model/beaconing/scionlab_algo.cpp',
                   'model/time_server.cpp',
                   'model/externs.cpp']

    headers = bld(features='ns3header')
    headers.module = 'SCION'

    model.source = ['model/beaconing/baseline.h',
                         'model/beaconing/beacon.h',
                         'model/beaconing/beacon_server.h',
                         'model/beaconing/diversity_age_based.h',
                         'model/beaconing/latency_optimized_beaconing.h',
                         'model/beaconing/green_beaconing.h',
                         'model/beaconing/on_demand_optimization.h',
                         'model/post_simulation_evaluations.h',
                         'model/schedule_periodic_events.h',
                         'model/run_parallel_events.h',
                         'model/user_defined_events.h',
                         'model/pre_simulation_setup.h',
                         'model/path_server.h',
                         'model/path_segment.h',
                         'model/scion_packet.h',
                         'model/scion_host.h',
                         'model/scion_as.h',
                         'model/scion_core_as.h',
                         'model/scion_capable_node.h',
                         'model/border_router.h',
                         'model/utils.h',
                         'model/beaconing/scionlab_algo.h',
                         'model/time_server.h',
                         'model/externs.h',
                         'model/json.hpp']

    obj = bld.create_ns3_program('main',
                                ['SCION', 'point-to-point'])

    obj.source = 'main.cpp'




