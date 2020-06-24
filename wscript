## -*- Mode: python; py-indent-offset: 4; indent-tabs-mode: nil; coding: utf-8; -*-

def build(bld):


    env = bld.env
    sim = bld.create_ns3_module('SCION', ['core', 'network'])

    sim.source = [ 'src/baseline.cpp',
                   'src/beacon.cpp',
                   'src/beaconing_strategy.cpp',
                   'src/criteria_matching.cpp',
                   'src/scion_as.cpp',
                   'src/scion_core_as.cpp',
                   'src/scion_node.cpp',
                   'src/utils.cpp']

    headers = bld(features='ns3header')
    headers.module = 'SCION'

    headers.source = ['headers/baseline.h',
                     'headers/beacon.h',
                     'headers/beaconing_strategy.h',
                     'headers/criteria_matching.h',
                     'headers/scion_as.h',
                     'headers/scion_core_as.h',
                     'headers/scion_node.h',
                     'headers/utils.h']

    obj = bld.create_ns3_program('scion-distributed',
                                 ['point-to-point', 'mpi'])

    obj.source = 'scion-distributed.cc'

    obj = bld.create_ns3_program('scion-baseline',
                                 ['point-to-point'])

    obj.source = 'scion-baseline.cc'

    obj = bld.create_ns3_program('scion-baseline-multiple-links',
                                     ['point-to-point'])

    obj.source = 'scion-baseline-multiple-links.cc'

    obj = bld.create_ns3_program('scion-criteria-matching',
                                 ['point-to-point'])

    obj.source = 'scion-criteria-matching.cc'


    obj = bld.create_ns3_program('BGPSec',
                                 ['point-to-point'])

    obj.source = 'BGPSec.cc'

    obj = bld.create_ns3_program('scion-core-sending-immediate-new-as',
                                 ['point-to-point'])

    obj.source = 'scion-core-sending-immediate-new-as.cc'


    obj = bld.create_ns3_program('main',
                                ['SCION', 'point-to-point'])

    obj.source = 'main.cpp'


