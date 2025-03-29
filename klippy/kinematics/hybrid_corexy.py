# Code for handling the kinematics of hybrid-corexy robots
#
# Copyright (C) 2021  Fabrice Gallet <tircown@gmail.com>
#
# This file may be distributed under the terms of the GNU GPLv3 license.
import logging
import stepper
from . import idex_modes, cartesian

# The hybrid-corexy kinematic is also known as Markforged kinematics
class HybridCoreXYKinematics:
    def __init__(self, config):
        # itersolve parameters
        self.rails = [ stepper.PrinterRail(config.getsection('stepper_x')),
                       stepper.LookupMultiRail(config.getsection('stepper_y')),
                       stepper.LookupMultiRail(config.getsection('stepper_z'))]
        self.rails[1].get_endstops()[0][0].add_stepper(
            self.rails[0].get_steppers()[0])
        self.rails[0].setup_itersolve('corexy_stepper_alloc', b'-')
        self.rails[1].setup_itersolve('cartesian_stepper_alloc', b'y')
        self.rails[2].setup_itersolve('cartesian_stepper_alloc', b'z')
        self.dc_module = None
        if config.has_section('dual_carriage'):
            dc_config = config.getsection('dual_carriage')
            # dummy for cartesian config users
            dc_config.getchoice('axis', ['x'], default='x')
            # setup second dual carriage rail
            self.rails.append(stepper.PrinterRail(dc_config))
            self.rails[1].get_endstops()[0][0].add_stepper(
                self.rails[3].get_steppers()[0])
            self.rails[3].setup_itersolve('corexy_stepper_alloc', b'+')
            dc_rail_0 = idex_modes.DualCarriagesRail(
                    self.rails[0], axis=0, active=True)
            dc_rail_1 = idex_modes.DualCarriagesRail(
                    self.rails[3], axis=0, active=False)
            self.dc_module = idex_modes.DualCarriages(
                    dc_config, dc_rail_0, dc_rail_1, axis=0)
    def get_rails(self):
        return self.rails, self.dc_module
    def calc_position(self, stepper_positions):
        pos = [stepper_positions[rail.get_name()] for rail in self.rails]
        if (self.dc_module is not None and 'PRIMARY' == \
                    self.dc_module.get_status()['carriage_1']):
            return [pos[3] - pos[1], pos[1], pos[2]]
        else:
            return [pos[0] + pos[1], pos[1], pos[2]]

def load_kinematics(toolhead, config):
    cpclass = HybridCoreXYKinematics(config)
    return cartesian.CartesianKinHelper(toolhead, config, cpclass)
