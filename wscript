## -*- Mode: python; py-indent-offset: 4; indent-tabs-mode: nil; coding: utf-8; -*-

def build(bld):


    env = bld.env
    sim = bld.create_ns3_module('SCION', ['core', 'network'])

    sim.source = [ 'model/beaconing/baseline.cpp',
                   'model/beaconing/beacon.cpp',
                   'model/beaconing/beacon-server.cpp',
                   'model/beaconing/diversity-age-based.cpp',
                   'model/beaconing/latency-optimized-beaconing.cpp',
                   'model/beaconing/green-beaconing.cpp',
                   'model/beaconing/on-demand-optimization.cpp',
                   'model/post-simulation-evaluations.cpp',
                   'model/schedule-periodic-events.cpp',
                   'model/user-defined-events.cpp',
                   'model/pre-simulation-setup.cpp',
                   'model/path-server.cpp',
                   'model/path-segment.cpp',
                   'model/scion-packet.cpp',
                   'model/scion-host.cpp',
                   'model/scion-as.cpp',
                   'model/scion-core-as.cpp',
                   'model/utils.cpp',
                   'model/scion-capable-node.cpp',
                   'model/border-router.cpp',
                   'model/beaconing/scionlab-algo.cpp',
                   'model/time-server.cpp',
                   'model/externs.cpp']

    headers = bld(features='ns3header')
    headers.module = 'SCION'

    headers.source = ['model/beaconing/baseline.h',
                         'model/beaconing/beacon.h',
                         'model/beaconing/beacon-server.h',
                         'model/beaconing/diversity-age-based.h',
                         'model/beaconing/latency-optimized-beaconing.h',
                         'model/beaconing/green-beaconing.h',
                         'model/beaconing/on-demand-optimization.h',
                         'model/post-simulation-evaluations.h',
                         'model/schedule-periodic-events.h',
                         'model/run-parallel-events.h',
                         'model/user-defined-events.h',
                         'model/pre-simulation-setup.h',
                         'model/path-server.h',
                         'model/path-segment.h',
                         'model/scion-packet.h',
                         'model/scion-host.h',
                         'model/scion-as.h',
                         'model/scion-core-as.h',
                         'model/scion-capable-node.h',
                         'model/border-router.h',
                         'model/utils.h',
                         'model/beaconing/scionlab-algo.h',
                         'model/time-server.h',
                         'model/externs.h',
                         'model/json.hpp']

    obj = bld.create_ns3_program('main',
                                ['SCION', 'point-to-point'])

    obj.source = 'main.cpp'




