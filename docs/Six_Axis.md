# Six Axis support

This document describes a mechanism to control additional axes using
standard `G1` G-Code commands. That is, it allows one to control
additional motors beyond the standard X, Y, Z, and E. This
functionality can be useful to control the orientation of the nozzle
relative to the print - so called "six axis" support, as one can
control both the X, Y, Z position as well as the A, B, C angle of the
nozzle (relative to the print).

Although this document is labeled as "six axis" support, the mechanism
described here is not limited to six axes. One may use fewer or more
axes as desired.

This document is a work in progress.

## Using manual_stepper as an additional axes

Configure an additional axis motor using a
[manual_stepper config section](Config_Reference.md#manual_stepper).
For example:

```
[manual_stepper axis_a]
step_pin: PB13
dir_pin: !PB12
enable_pin: !PB14
microsteps: 16
rotation_distance: 360
endstop_pin: ^PC0
```

This configuration section allows control of the motor using
[MANUAL_STEPPER commands](G-Codes.md#manual_stepper). One may
associate the given stepper with `G1` commands using a command like
the command:

```
MANUAL_STEPPER STEPPER=axis_a GCODE_AXIS=A
```

Once a `manual_stepper` is associated with a `GCODE_AXIS` then it can
be controlled with standard `G1` commands. For example, `G1 A10` would
move the stepper to a position of "10". A command `G1 A20 X20` would
simultaneously move both the stepper and the X carriage.

One can associate a `manual_stepper` with any of the available G-Code
letters (that is, any letter other than `XYZEFN`).

If a `manual_stepper` is associated with a `G1` axis, then it can no
longer be controlled by the `MANUAL_STEPPER` commands. If the stepper
requires homing, that homing should be done prior to associating the
stepper with `G1`. One can disassociate a stepper by assigning it to a
blank `GCODE_AXIS` - for example:
`MANUAL_STEPPER STEPPER=axis_a GCODE_AXIS=`
After a stepper is disassociated, the normal `MANUAL_STEPPER` commands
may be used (including possibly reassociating it with `G1`).

## Using pivot_coord

The [pivot_coord tool](Config_Reference.md#pivot_coord) can be used to
perform G-Code coordinate transformation for printers that can rotate
the orientation of the bed (relative to the orientation of the XYZ
carriages). That is, the tool can be used to facilitate "six-axis"
style printing on printers that can rotate the bed platform.

This document uses examples with a printer containing two additional
motors - an "axis_c" motor that can rotate the bed platform around a
line parallel to the Z axis, and an "axis_a" motor that can rotate the
axis_c assembly around a line parallel to the X axis. For example:

```
[manual_stepper axis_a]
step_pin: PB13
dir_pin: !PB12
enable_pin: !PB14
microsteps: 16
rotation_distance: 360
endstop_pin: ^PC0

[manual_stepper axis_c]
step_pin: PB10
dir_pin: !PB2
enable_pin: !PB11
microsteps: 16
rotation_distance: 360
endstop_pin: ^PC1

[pivot_coord]
control_0: manual_stepper axis_c
rotate_axis_0: z
offset_x_0: 125
offset_y_0: 125
control_1: manual_stepper axis_a
rotate_axis_1: x
offset_y_1: 125
offset_z_1: -10
```

Rotating the bed has the effect of rotating the print, which has the
effect of changing the angle of the nozzle relative to the print. This
can be used to improve the surface quality of printed objects.

The `pivot_coord` module can be used to translate G-Code coordinates.
To see how this is useful, consider an "axis_a" motor that can rotate
the bed around an X axis. In this configuration, if one were to move
the toolhead to a X=125, Y=180, Z=20 position while the bed was at its
starting position perpendicular to the Z axis (A=0) then the toolhead
would be 20mm from the bed. However, if one were to rotate the bed by
10 degrees (`G1 A=10`) then the bed would rotate closer to the
toolhead. The `pivot_coord` module can be used to maintain a
consistent orientation relative to the bed when making these bed
rotations.

One must activate the `pivot_coord` module to enable coordinate
translation. One can use a macro to associate both the
`manual_stepper` and activate the `pivot_coord` module - for example:

```
[gcode_macro ENABLE_EXTRA_AXES]
gcode:
    MANUAL_STEPPER STEPPER=axis_a GCODE_AXIS=A
    MANUAL_STEPPER STEPPER=axis_c GCODE_AXIS=C
    PIVOT_COORD ACTIVATE=1
```

When activated, the example `G1 A10` command above would both rotate
the bed and move the toolhead's XYZ position. It would move the
toolhead position to maintain the same 20mm distance from the bed.
That is, from someone looking at the printer, it would appear that the
bed is rotating and the toolhead is moving in an arc. However, if one
were to imagine themselves sitting on the bed, the move would appear
as the toolhead changing its orientation and otherwise staying
stationary. Future XYZ moves (eg, `G1 X100 Y170`) would maintain a
consistent height from the bed - that is, from the outside it would
appear that the moves are occurring as XYZ movement, but from the
perspective of the bed it would appear as horizontal movement.

### Calibrating pivot_coord offsets

The `pivot_coord` module requires an accurate configuration of the
pivot locations. These are the "offset" parameters in the
configuration file.

It should be possible to tune the configuration using normal 3d
prints. Use a slicer to generate g-code for the large hollow square
found in [docs/prints/square_tower.stl](prints/square_tower.stl). It
is not necessary to use a "six axis" slicer.

For a "axis_c" type rotation axis (an axis that rotates the bed on a
line parallel to the Z axis) consider printing the above object after
running the following tuning tower command:

```
TUNING_TOWER COMMAND=G1 PARAMETER=C START=0 STEP_DELTA=10 STEP_HEIGHT=5
```

Be sure to activate the `pivot_coord` (with `PIVOT_COORD ACTIVATE=1`)
prior to running `TUNING_TOWER` as the order of these two operations
is important.

The above command will rotate the bed 10 degrees after every 5mm of Z
height. If the pivot offsets are accurate, there should be no
noticeable "bands" every 5mm. If at the 5mm marks the new layers do
not fully align with the bottom layers it would indicate that the
pivot offset needs to be adjusted.

For an "axis_a" type rotation axis (or for axes that rotate the bed on
a line parallel to the X or Y axis) consider printing the above object
after running the following tuning tower command:

```
TUNING_TOWER COMMAND=G1 PARAMETER=A START=0 STEP_DELTA=2 STEP_HEIGHT=5
```

The above command will rotate the bed 2 degrees after every 5mm of Z
height. Again, look for "bands" every 5mm in the print and adjust the
pivot offsets if needed.
