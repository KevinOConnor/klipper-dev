#!/usr/bin/env python
# Tool to graph queue_step mcu commands
#
# Copyright (C) 2021  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU GPLv3 license.
import optparse, datetime
import matplotlib

def parse_log(logname, oid, skip, step_count):
    oidstr = "oid=%d" % (oid,)
    step_time = 0
    out = []
    f = open(logname, 'r')
    for line in f:
        parts = line.split()
        if not parts or parts[0] != 'queue_step' or parts[1] != oidstr:
            continue
        keyparts = {k: int(v) for k, v in [p.split('=', 1) for p in parts[2:]]}
        interval = keyparts['interval']
        add = keyparts['add']
        count = keyparts['count']
        if skip >= count:
            skip -= count
            addfactor = count * (count - 1) // 2
            step_time += interval * count + add * addfactor
            continue
        # Determine step time for each step in queue_step command
        if skip:
            addfactor = skip * (skip - 1) // 2
            step_time += interval * skip + add * addfactor
            interval += add * skip
            count -= skip
            skip = 0
        for i in range(count):
            step_time += interval
            out.append(step_time)
            interval += add
        if len(out) >= step_count:
            del out[step_count:]
            break
    return out


######################################################################
# Plotting and startup
######################################################################

def plot_steps(data, oid, skip, diff_level):
    first_steps = first_intervals = first_adds = None
    # Build plot
    fig, (ax1, ax2, ax3) = matplotlib.pyplot.subplots(nrows=3, sharex=True)
    for logname, steps in data:
        count = len(steps)
        xaxis = list(range(skip+1, skip+1+count))
        intervals = [steps[i+1] - steps[i] for i in range(count-1)]
        adds = [intervals[i+1] - intervals[i] for i in range(count-2)]
        if first_steps is None:
            first_steps = steps
            first_intervals = intervals
            first_adds = adds
        if diff_level > 0:
            diff = [s - fs for s, fs in zip(steps, first_steps)]
            ax1.step(xaxis[:len(diff)], diff, label=logname, alpha=0.8)
        else:
            ax1.step(xaxis, steps, label=logname, alpha=0.8)
        if diff_level > 1:
            diff = [s - fs for s, fs in zip(intervals, first_intervals)]
            ax2.step(xaxis[1:len(diff)+1], diff, label=logname, alpha=0.8)
        else:
            ax2.step(xaxis[1:], intervals, label=logname, alpha=0.8)
        if diff_level > 2:
            diff = [s - fs for s, fs in zip(adds, first_adds)]
            ax3.step(xaxis[2:len(diff)+2], diff, label=logname, alpha=0.8)
        else:
            ax3.step(xaxis[2:], adds, label=logname, alpha=0.8)
    if ax2.get_ylim()[1] > 60000: ax2.set_ylim((0, 60000))
    if ax3.get_ylim()[0] < -10000: ax3.set_ylim((-10000, ax3.get_ylim()[1]))
    if ax3.get_ylim()[1] > 10000: ax3.set_ylim((ax3.get_ylim()[0], 10000))
    fontP = matplotlib.font_manager.FontProperties()
    fontP.set_size('x-small')
    ax1.set_title("Steps (oid=%d)" % (oid,))
    ax1.set_ylabel('Clock')
    ax1.legend(loc='best', prop=fontP)
    ax1.grid(True)
    ax2.set_ylabel('Interval')
    ax2.grid(True)
    ax3.set_ylabel('Add')
    ax3.grid(True)
    ax3.set_xlabel('Step')
    return fig

def setup_matplotlib(output_to_file):
    global matplotlib
    if output_to_file:
        matplotlib.use('Agg')
    import matplotlib.pyplot, matplotlib.dates, matplotlib.font_manager
    import matplotlib.ticker

def main():
    # Parse command-line arguments
    usage = "%prog [options] <mculogs>"
    opts = optparse.OptionParser(usage)
    opts.add_option("-O", "--oid", type="int", dest="oid",
                    help="stepper oid to graph")
    opts.add_option("-s", "--skip", type="int", dest="skip", default=0,
                    help="seek to given step number in log")
    opts.add_option("-c", "--count", type="int", dest="count", default=100,
                    help="number of steps to graph")
    opts.add_option("-d", "--diff", type="int", default=0,
                    help="graph difference from first log (level)")
    opts.add_option("-o", "--output", type="string", dest="output",
                    default=None, help="filename of output graph")
    options, args = opts.parse_args()
    if len(args) < 1:
        opts.error("Incorrect number of arguments")
    if options.oid is None:
        opts.error("Must specify oid")
    lognames = args

    # Parse data
    data = []
    for logname in lognames:
        steps = parse_log(logname, options.oid, options.skip, options.count)
        if len(steps) < 2:
            raise Exception("Log %s has too few steps" % (logname,))
        data.append((logname, steps))

    # Draw graph
    setup_matplotlib(options.output is not None)
    fig = plot_steps(data, options.oid, options.skip, options.diff)

    # Show graph
    if options.output is None:
        matplotlib.pyplot.show()
    else:
        fig.set_size_inches(8, 6)
        fig.savefig(options.output)

if __name__ == '__main__':
    main()
