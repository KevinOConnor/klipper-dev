# Code for handling the kinematics of corexz robots
#
# Copyright (C) 2020  Maks Zolin <mzolin@vorondesign.com>
#
# This file may be distributed under the terms of the GNU GPLv3 license.
import logging, math
import stepper
from . import cartesian

class CoreXZKinematics:
    def __init__(self, config):
        # Setup axis rails
        self.rails = [stepper.LookupMultiRail(config.getsection('stepper_' + n))
                      for n in 'xyz']
        for s in self.rails[0].get_steppers():
            self.rails[2].get_endstops()[0][0].add_stepper(s)
        for s in self.rails[2].get_steppers():
            self.rails[0].get_endstops()[0][0].add_stepper(s)
        self.rails[0].setup_itersolve('corexz_stepper_alloc', b'+')
        self.rails[1].setup_itersolve('cartesian_stepper_alloc', b'y')
        self.rails[2].setup_itersolve('corexz_stepper_alloc', b'-')
    def get_rails(self):
        return self.rails, None
    def calc_position(self, stepper_positions):
        pos = [stepper_positions[rail.get_name()] for rail in self.rails]
        return [0.5 * (pos[0] + pos[2]), pos[1], 0.5 * (pos[0] - pos[2])]

def load_kinematics(toolhead, config):
    cpclass = CoreXZKinematics(config)
    return cartesian.CartesianKinHelper(toolhead, config, cpclass)
