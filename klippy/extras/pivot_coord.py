# Support coordinate transforms on 6-axis machines
#
# Copyright (C) 2025  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU GPLv3 license.
import math, logging

class PivotCoord:
    def __init__(self, config):
        self.printer = config.get_printer()
        self.next_transform = None
        self.last_pos = []
        self.last_pivots_d2 = []
        self.check_axes = {}
        resolution = config.getfloat('resolution', 0.1)
        self.inv_resolution = 1. / resolution
        full_rotation = config.getfloat('axis_full_rotation', 360.)
        self.axis_to_radians = 2. * math.pi / full_rotation
        # Read config
        self.transform_axes = []
        for i in range(99):
            if config.get('control_%d' % (i,), None) is None:
                break
            cls_name = config.get('control_%d' % (i,))
            cls = self.printer.load_object(config, cls_name)
            rot_axis = config.getchoice('rotate_axis_%d' % (i,),
                                        ['x', 'y', 'z'], None)
            offset_x = config.getfloat('offset_x_%d' % (i,), 0.)
            offset_y = config.getfloat('offset_y_%d' % (i,), 0.)
            offset_z = config.getfloat('offset_z_%d' % (i,), 0.)
            offset = (offset_x, offset_y, offset_z)
            ea_index = None
            self.transform_axes.append([cls, rot_axis, offset, ea_index])
        # register callbacks
        self.printer.register_event_handler("toolhead:update_extra_axes",
                                            self._update_extra_axes)
        # register gcode
        gcode = self.printer.lookup_object('gcode')
        gcode.register_command("PIVOT_COORD",
                               self.cmd_PIVOT_COORD,
                               desc=self.cmd_PIVOT_COORD_help)
    def _update_extra_axes(self):
        toolhead = self.printer.lookup_object('toolhead')
        extra_axes = toolhead.get_extra_axes()
        for pivot_info in self.transform_axes:
            cls, rot_axis, offset, ea_index = pivot_info
            new_index = None
            if cls in extra_axes:
                new_index = extra_axes.index(cls)
            pivot_info[3] = new_index
    def pivot_coords(self, coord, reverse=False):
        coord = list(coord)
        ta = self.transform_axes
        if reverse:
            ta = reversed(ta)
        pivots_d2 = [0.] * len(coord)
        for cls, rot_axis, offset, ea_index in ta:
            if ea_index is None:
                continue
            # offset
            oc = (coord[0]-offset[0], coord[1]-offset[1], coord[2]-offset[2])
            # select axes to rotate
            c = {'x': (oc[1], oc[2]), 'y': (oc[2], oc[0]),
                 'z': (oc[0], oc[1])}[rot_axis]
            # rotate
            pmult = self.axis_to_radians
            if reverse:
                pmult = -pmult
            pos = coord[ea_index] * pmult
            cos = math.cos(pos)
            sin = math.sin(pos)
            cn = (c[0] * cos - c[1] * sin, c[0] * sin + c[1] * cos)
            # return to original coordinate system
            cd = {'x': [oc[0], cn[0], cn[1]], 'y': [cn[1], oc[1], cn[0]],
                  'z': [cn[0], cn[1], oc[2]]}[rot_axis]
            coord[:3] = [cd[0]+offset[0], cd[1]+offset[1], cd[2]+offset[2]]
            # Track distance from pivot point for each rotating axis
            pivots_d2[ea_index] = c[0]*c[0] + c[1]*c[1]
        return coord, pivots_d2
    def get_position(self):
        coord = self.next_transform.get_position()
        tran_coord, pivots_d2 = self.pivot_coords(coord, reverse=True)
        self.last_pos = list(tran_coord)
        self.last_pivots_d2 = pivots_d2
        return tran_coord
    def move(self, newpos, speed):
        last_pos = self.last_pos
        a2r = self.axis_to_radians
        # Calculate final coordinates
        next_pos, next_pivots_d2 = self.pivot_coords(newpos)
        # Check if any pivot axes move and estimate angular travel if so
        angular_travel = 0.
        for cls, rot_axis, offset, ea_index in self.transform_axes:
            if ea_index is None or newpos[ea_index] == last_pos[ea_index]:
                continue
            last_pivot_d2 = self.last_pivots_d2[ea_index]
            next_pivot_d2 = next_pivots_d2[ea_index]
            pdist = math.sqrt(max(last_pivot_d2, next_pivot_d2))
            # track max angular_travel
            at = abs(newpos[ea_index] - last_pos[ea_index]) * a2r * pdist
            angular_travel = max(angular_travel, at)
        # Segment into many small moves if needed
        segments = int(math.ceil(angular_travel * self.inv_resolution))
        if segments >= 2:
            # Determine adjustment for each segment
            inv_segment = 1. / segments
            adj = [(np - lp) * inv_segment for np, lp in zip(newpos, last_pos)]
            # Transmit segmented moves (all but last)
            for i in range(1, segments):
                spos = [lp + i*a for lp, a in zip(last_pos, adj)]
                npos, p_d2 = self.pivot_coords(spos)
                self.next_transform.move(npos, speed)
        # Move to final position
        self.next_transform.move(next_pos, speed)
        self.last_pos = list(newpos)
        self.last_pivots_d2 = next_pivots_d2
    def _activate(self):
        gcode_move = self.printer.lookup_object('gcode_move')
        self.next_transform = gcode_move.set_move_transform(self, force=True)
        gcode_move.reset_last_position()
    def _deactivate(self):
        if self.next_transform is None:
            # Already deactivated
            return
        gcode_move = self.printer.lookup_object('gcode_move')
        gcode_move.set_move_transform(self.next_transform, force=True)
        self.next_transform = None
    cmd_PIVOT_COORD_help = "Set PIVOT_COORD settings"
    def cmd_PIVOT_COORD(self, gcmd):
        activate = gcmd.get_int('ACTIVATE')
        if activate:
            if self.next_transform is not None:
                raise gcmd.error("PIVOT_COORD already activated")
            self._activate()
            return
        self._deactivate()

def load_config(config):
    return PivotCoord(config)
