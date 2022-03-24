## -*- Mode: python; py-indent-offset: 4; indent-tabs-mode: nil; coding: utf-8; -*-

def build(bld):


    env = bld.env
    sim = bld.create_ns3_module('SCION', ['core', 'network'])

    sim.source = [ 'src/beaconing/baseline.cpp',
                   'src/beaconing/beacon.cpp',
                   'src/beaconing/beacon_server.cpp',
                   'src/beaconing/diversity_age_based.cpp',
                   'src/beaconing/latency_optimized_beaconing.cpp',
                   'src/beaconing/green_beaconing.cpp',
                   'src/post_simulation_evaluations.cpp',
                   'src/schedule_periodic_events.cpp',
                   'src/user_defined_events.cpp'
                   'src/path_server.cpp',
                   'src/path_segment.cpp',
                   'src/scion_packet.cpp',
                   'src/scion_host.cpp',
                   'src/scion_as.cpp',
                   'src/scion_core_as.cpp',
                   'src/utils.cpp',
                   'src/scion_capable_node.cpp',
                   'src/border_router.cpp',
                   'src/beaconing/scionlab_algo.cpp',
                   'src/time_server.cpp',
                   'src/externs.cpp']

    headers = bld(features='ns3header')
    headers.module = 'SCION'

    headers.source = ['headers/beaconing/baseline.h',
                     'headers/beaconing/beacon.h',
                     'headers/beaconing/beacon_server.h',
                     'headers/beaconing/diversity_age_based.h',
                     'headers/beaconing/latency_optimized_beaconing.h',
                     'headers/beaconing/green_beaconing.h',
                     'headers/post_simulation_evaluations.h',
                     'headers/schedule_periodic_events.h',
                     'headers/run_parallel_events.h',
                     'headers/user_defined_events.h',
                     'headers/path_server.h',
                     'headers/path_segment.h',
                     'headers/scion_packet.h',
                     'headers/scion_host.h',
                     'headers/scion_as.h',
                     'headers/scion_core_as.h',
                     'headers/scion_capable_node.h',
                     'headers/border_router.h',
                     'headers/utils.h',
                     'headers/beaconing/scionlab_algo.h',
                     'headers/time_server.h',
                     'headers/externs.h',
                     'headers/json.hpp']

    obj = bld.create_ns3_program('main',
                                ['SCION', 'point-to-point'])

    obj.source = 'main.cpp'




