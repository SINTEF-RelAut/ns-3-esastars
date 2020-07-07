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

    obj = bld.create_ns3_program('baseline_sim',
                                ['SCION', 'point-to-point'])

    obj.source = 'baseline_sim.cpp'

    obj = bld.create_ns3_program('criteria_matching_sim',
                                    ['SCION', 'point-to-point'])

    obj.source = 'criteria_matching_sim.cpp'


