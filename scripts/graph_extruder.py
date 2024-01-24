#!/usr/bin/env python
# Generate extruder pressure advance motion graphs
#
# Copyright (C) 2019-2021  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU GPLv3 license.
import math, optparse, datetime
import matplotlib

#SEG_TIME = .000100
SEG_TIME = .000025
INV_SEG_TIME = 1. / SEG_TIME


######################################################################
# Basic trapezoid motion
######################################################################

# List of moves: [(start_v, end_v, move_t), ...]
Moves = [
    (0., 0., .100),
    (0., 100., None), (100., 100., .200), (100., 5., None),
    (5., 100., None), (100., 100., .200), (100., 0., None),
    (0., 0., .300)
]
EXTRUDE_R = (.4 * .4 * .75) / (math.pi * (1.75 / 2.)**2)
ACCEL = 3000. * EXTRUDE_R

def gen_positions():
    out = []
    start_d = start_t = t = 0.
    for start_v, end_v, move_t in Moves:
        start_v *= EXTRUDE_R
        end_v *= EXTRUDE_R
        if move_t is None:
            move_t = abs(end_v - start_v) / ACCEL
        half_accel = 0.
        if end_v > start_v:
            half_accel = .5 * ACCEL
        elif start_v > end_v:
            half_accel = -.5 * ACCEL
        end_t = start_t + move_t
        while t <= end_t:
            rel_t = t - start_t
            out.append(start_d + (start_v + half_accel * rel_t) * rel_t)
            t += SEG_TIME
        start_d += (start_v + half_accel * move_t) * move_t
        start_t = end_t
    return out


######################################################################
# List helper functions
######################################################################

MARGIN_TIME = 0.050

def time_to_index(t):
    return int(t * INV_SEG_TIME + .5)

def indexes(positions):
    drop = time_to_index(MARGIN_TIME)
    return range(drop, len(positions)-drop)

def trim_lists(*lists):
    keep = len(lists[0]) - time_to_index(2. * MARGIN_TIME)
    for l in lists:
        del l[keep:]


######################################################################
# Common data filters
######################################################################

# Generate estimated first order derivative
def gen_deriv(data):
    return [0.] + [(data[i+1] - data[i]) * INV_SEG_TIME
                   for i in range(len(data)-1)]

# Simple average between two points smooth_time away
def calc_average(positions, smooth_time):
    offset = time_to_index(smooth_time * .5)
    out = [0.] * len(positions)
    for i in indexes(positions):
        out[i] = .5 * (positions[i-offset] + positions[i+offset])
    return out

# Average (via integration) of smooth_time range
def calc_smooth(positions, smooth_time):
    offset = time_to_index(smooth_time * .5)
    weight = 1. / (2*offset - 1)
    out = [0.] * len(positions)
    for i in indexes(positions):
        out[i] = sum(positions[i-offset+1:i+offset]) * weight
    return out

# Time weighted average (via integration) of smooth_time range
def calc_weighted(positions, smooth_time):
    offset = time_to_index(smooth_time * .5)
    weight = 1. / offset**2
    out = [0.] * len(positions)
    for i in indexes(positions):
        weighted_data = [positions[j] * (offset - abs(j-i))
                         for j in range(i-offset, i+offset)]
        out[i] = sum(weighted_data) * weight
    return out


######################################################################
# Pressure advance
######################################################################

SMOOTH_TIME = .040
PRESSURE_ADVANCE = .045

# Calculate raw pressure advance positions
def calc_pa_raw(positions, pressure_advance):
    pa = pressure_advance * INV_SEG_TIME
    out = [0.] * len(positions)
    for i in indexes(positions):
        out[i] = positions[i] + pa * (positions[i+1] - positions[i])
    return out

# Pressure advance after smoothing
def calc_pa(positions, pressure_advance):
    return calc_weighted(calc_pa_raw(positions, pressure_advance), SMOOTH_TIME)

# Nozzle flow assuming nozzle follows simple pressure advance model
def calc_nozzle_flow(positions, pressure_advance):
    pa = pressure_advance * INV_SEG_TIME
    last = 0.
    out = [0.] * len(positions)
    for i in range(1, len(positions)):
        out[i] = last = (positions[i] - last)/pa + last
    return out

TH_POSITION_BUCKET = 0.200
TH_OFFSET = 50

# Map toolhead locations
def calc_toolhead_positions(e_positions):
    inv_extrude_r = 1. / EXTRUDE_R
    th_positions = [e * inv_extrude_r for e in e_positions]
    th_max = int(max(th_positions) / TH_POSITION_BUCKET)
    th_index = [i * TH_POSITION_BUCKET
                for i in range(-TH_OFFSET, th_max + TH_OFFSET)]
    return th_index, th_positions

# Determing extrusion amount at each toolhead position
def calc_extruded(th_index, th_positions, e_velocities):
    e_adjust = SEG_TIME / TH_POSITION_BUCKET
    inv_extrude_r = 1. / EXTRUDE_R
    out = [0.] * len(th_index)
    for th_pos, e in zip(th_positions, e_velocities):
        bucket = TH_OFFSET + int(th_pos / TH_POSITION_BUCKET)
        if e > 0.:
            out[bucket] += e * e_adjust
    return out


######################################################################
# Plotting and startup
######################################################################

def plot_motion():
    # Nominal motion
    positions = gen_positions()
    velocities = gen_deriv(positions)
    accels = gen_deriv(velocities)
    # Motion with pressure advance
    pa_positions = calc_pa_raw(positions, PRESSURE_ADVANCE)
    pa_velocities = gen_deriv(pa_positions)
    # Smoothed motion
    sm_positions = calc_pa(positions, PRESSURE_ADVANCE)
    sm_velocities = gen_deriv(sm_positions)
    # Extruded filament
    ex_positions = calc_nozzle_flow(sm_positions, PRESSURE_ADVANCE)
    ex_velocities = gen_deriv(ex_positions)
    # Altered Extruded filament
    sm2_positions = calc_pa(positions, PRESSURE_ADVANCE + 0.010)
    sm2_velocities = gen_deriv(sm2_positions)
    ex2_positions = calc_nozzle_flow(sm2_positions, PRESSURE_ADVANCE)
    ex2_velocities = gen_deriv(ex2_positions)
    # Build plot
    times = [SEG_TIME * i for i in range(len(positions))]
    trim_lists(times, velocities, accels,
               pa_positions, pa_velocities,
               sm_positions, sm_velocities,
               ex_positions, ex_velocities,
               sm2_positions, sm2_velocities,
               ex2_positions, ex2_velocities)
    #fig, ax1 = matplotlib.pyplot.subplots(nrows=1, sharex=True)
    #fig, (ax1, ax2) = matplotlib.pyplot.subplots(nrows=2, sharex=True)
    fig, (ax1, ax2) = matplotlib.pyplot.subplots(nrows=2)
    ax1.set_title("Extruder Velocity (100mm/s to 5mm/s, 0.4 x 0.3 extrusion)")
    ax1.set_ylabel('Velocity (mm/s)')
    pa_plot, = ax1.plot(times, pa_velocities, 'r',
                        label='Extruder ST=0.000 PA=0.045', alpha=0.3)
    nom_plot, = ax1.plot(times, velocities, 'black', label='Nominal')
    sm_plot, = ax1.plot(times, sm_velocities,
                        'g', label='Extruder ST=0.040 PA=0.045', alpha=0.3)
    ex_plot, = ax1.plot(times, ex_velocities,
                        'y', label='Flow ST=0.040 PA=0.045')
    sm2_plot, = ax1.plot(times, sm2_velocities,
                         'grey', label='Extruder ST=0.040 PA=0.055', alpha=0.3)
    ex2_plot, = ax1.plot(times, ex2_velocities,
                         'b', label='Flow ST=0.040 PA=0.055', alpha=0.5)
    fontP = matplotlib.font_manager.FontProperties()
    fontP.set_size('x-small')
    ax1.legend(handles=[nom_plot, pa_plot, sm_plot, ex_plot,
                        sm2_plot, ex2_plot],
               loc='best', prop=fontP)
    ax1.set_xlabel('Time (s)')
    ax1.grid(True)

    th_index, th_positions = calc_toolhead_positions(positions)
    ext_nom = calc_extruded(th_index, th_positions, velocities)
    ext_ex = calc_extruded(th_index, th_positions, ex_velocities)
    ext_ex2 = calc_extruded(th_index, th_positions, ex2_velocities)

    ax2.set_title("Extrusion Amount")
    ax2.set_ylabel('Filament extruded (mm)')
    nom2_plot, = ax2.plot(th_index, ext_nom, 'black', label='Nominal')
    ex2_plot, = ax2.plot(th_index, ext_ex,
                        'y', label='Flow ST=0.040 PA=0.045', alpha=0.7)
    ex22_plot, = ax2.plot(th_index, ext_ex2,
                          'b', label='Flow ST=0.040 PA=0.055', alpha=0.5)
    ax2.legend(handles=[nom2_plot, ex2_plot, ex22_plot],
               loc='best', prop=fontP)
    ax2.set_xlabel('Toolhead Position (mm)')
    ax2.grid(True)

    fig.tight_layout()
    return fig

def setup_matplotlib(output_to_file):
    global matplotlib
    if output_to_file:
        matplotlib.rcParams.update({'figure.autolayout': True})
        matplotlib.use('Agg')
    import matplotlib.pyplot, matplotlib.dates, matplotlib.font_manager
    import matplotlib.ticker

def main():
    # Parse command-line arguments
    usage = "%prog [options]"
    opts = optparse.OptionParser(usage)
    opts.add_option("-o", "--output", type="string", dest="output",
                    default=None, help="filename of output graph")
    options, args = opts.parse_args()
    if len(args) != 0:
        opts.error("Incorrect number of arguments")

    # Draw graph
    setup_matplotlib(options.output is not None)
    fig = plot_motion()

    # Show graph
    if options.output is None:
        matplotlib.pyplot.show()
    else:
        fig.set_size_inches(8, 6)
        fig.savefig(options.output)

if __name__ == '__main__':
    main()
