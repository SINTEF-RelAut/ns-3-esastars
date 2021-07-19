## -*- Mode: python; py-indent-offset: 4; indent-tabs-mode: nil; coding: utf-8; -*-

def build(bld):


    env = bld.env
    sim = bld.create_ns3_module('SCION', ['core', 'network'])

    sim.source = [ 'src/beaconing/baseline.cpp',
                   'src/beaconing/beacon.cpp',
                   'src/beaconing/beacon_server.cpp',
                   'src/beaconing/criteria_matching.cpp',
                   'src/beaconing/latency_optimized_beaconing.cpp',
                   'src/post_simulation_evaluations.cpp',
                   'src/local_scheduler.cpp',
                   'src/global_scheduling.cpp',
                   'src/path_server.cpp',
                   'src/scion_as.cpp',
                   'src/scion_core_as.cpp',
                   'src/utils.cpp',
                   'src/beaconing/scionlab_algo.cpp']

    headers = bld(features='ns3header')
    headers.module = 'SCION'

    headers.source = ['headers/beaconing/baseline.h',
                     'headers/beaconing/beacon.h',
                     'headers/beaconing/beacon_server.h',
                     'headers/beaconing/criteria_matching.h',
                     'headers/beaconing/latency_optimized_beaconing.h',
                     'headers/post_simulation_evaluations.h',
                     'headers/local_scheduler.h',
                     'headers/global_scheduling.h',
                     'headers/path_server.h',
                     'headers/scion_as.h',
                     'headers/scion_core_as.h',
                     'headers/utils.h',
                     'headers/beaconing/scionlab_algo.h']

    obj = bld.create_ns3_program('main',
                                ['SCION', 'point-to-point'])

    obj.source = 'main.cpp'




