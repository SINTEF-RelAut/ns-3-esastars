## -*- Mode: python; py-indent-offset: 4; indent-tabs-mode: nil; coding: utf-8; -*-

def build(bld):
    env = bld.env
    sim = bld.create_ns3_module('SCION', ['core', 'network'])
    headers = bld(features='ns3header')
    headers.module = 'SCION'

    obj = bld.create_ns3_program('scion-distributed',
                                 ['point-to-point', 'mpi'])
    obj.cxxflags = ['-fopenmp', '-std=c++17']
    obj.linkflags = ['-fopenmp']
    obj.source = 'scion-distributed.cc'

    obj = bld.create_ns3_program('scion-baseline',
                                 ['point-to-point'])
    obj.cxxflags = ['-fopenmp', '-std=c++17']
    obj.linkflags = ['-fopenmp']
    obj.source = 'scion-baseline.cc'

    obj = bld.create_ns3_program('scion-baseline-multiple-links',
                                     ['point-to-point'])
    obj.cxxflags = ['-fopenmp', '-std=c++17']
    obj.linkflags = ['-fopenmp']
    obj.source = 'scion-baseline-multiple-links.cc'

    obj = bld.create_ns3_program('scion-criteria-matching',
                                 ['point-to-point'])
    obj.cxxflags = ['-fopenmp', '-std=c++17']
    obj.linkflags = ['-fopenmp']
    obj.source = 'scion-criteria-matching.cc'

    obj = bld.create_ns3_program('scion-core-sending-immediate-new-as',
                                 ['point-to-point'])
    obj.cxxflags = ['-fopenmp', '-std=c++17']
    obj.linkflags = ['-fopenmp']
    obj.source = 'scion-core-sending-immediate-new-as.cc'


    obj = bld.create_ns3_program('scion-intra-isd-baseline',
                                 ['point-to-point'])
    obj.cxxflags = ['-fopenmp', '-std=c++17']
    obj.linkflags = ['-fopenmp']
    obj.source = 'scion-intra-isd-baseline.cc'

    obj = bld.create_ns3_program('scion-intra-isd-criteria-matching',
                                 ['point-to-point'])
    obj.cxxflags = ['-fopenmp', '-std=c++17']
    obj.linkflags = ['-fopenmp']
    obj.source = 'scion-intra-isd-criteria-matching.cc'
