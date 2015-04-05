#!/usr/bin/env python

# create a ring
# assure records written can be read back

import os,time,sys

from nose import with_setup
from machinekit.nosetests.realtime import setup_module# ,teardown_module
from machinekit import hal

size=4096

def test_create_ring():
    global sr
    sr = hal.Ring("ring1", size=size, type=hal.RINGTYPE_STREAM)

def test_ring_write_read():
    nr = 0
    for n in range(size):
        print "loop",n
        if sr.write("X") < 1:
            print size, n
            assert n == size - 1

    m = sr.read()
    assert len(m) == size -1


(lambda s=__import__('signal'):
     s.signal(s.SIGTERM, s.SIG_IGN))()
