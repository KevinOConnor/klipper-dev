#!/usr/bin/env python
# Script to perform analysis and graphing of stepper motor phases
#
# Copyright (C) 2021  Kevin O'Connor <kevin@koconnor.net>
#
# This file may be distributed under the terms of the GNU GPLv3 license.
import sys, optparse, ast
import matplotlib
import motan_graph, readlog, analyzers

# Todo:
# - separate forward motion buckets from reverse motion buckets
# - support graphs from multiple logs?
# - write testing macros?
#   - impact of microstep setting?
#   - impact of interpolation
#   - spreadcycle tuning

NUM_PHASES = 4096

# Find each step in a time range and interpolate to a set of sub-step times
def get_phase_times(lmanager, stepper_driver, start, end):
    config = lmanager.get_initial_status()['configfile']['settings']
    sd_dataset = lmanager.setup_dataset("step_phase(%s)" % (stepper_driver,))
    microsteps = sd_dataset.get_microsteps()
    expand_count = NUM_PHASES // (microsteps * 4)
    expand_phase = list(range(1, expand_count+1))
    expand_time = [float(i) / expand_count for i in expand_phase]
    phase_mask = NUM_PHASES - 1
    initial_start_time = lmanager.get_initial_start_time()
    last_time = last_phase = None
    times = []
    phases = []
    while 1:
        res = sd_dataset.pull_next(start, end)
        if res is None:
            break
        time, phase = res
        time -= initial_start_time
        phase *= expand_count
        if (phase - expand_count) & phase_mask == last_phase:
            tdelta = time - last_time
            times.extend([last_time + tdelta * e for e in expand_time])
            phases.extend([(1, (last_phase + e) & phase_mask)
                           for e in expand_phase])
        elif (phase + expand_count) & phase_mask == last_phase:
            tdelta = time - last_time
            times.extend([last_time + tdelta * e for e in expand_time])
            phases.extend([(0, (last_phase - e) & phase_mask)
                           for e in expand_phase])
        last_time = time
        last_phase = phase
    return times, phases

def calc_median(buckets):
    out = [0.] * len(buckets)
    for i, bucket in enumerate(buckets):
        bucket.sort()
        l = len(bucket)
        if l & 1:
            res = bucket[l // 2]
        elif not bucket:
            res = 0.
        else:
            res = .5 * (bucket[l // 2] + bucket[l // 2 - 1])
        out[i] = res
    return out

def convert_to_phases(phasesets, run_name, amanager, graphs, phases):
    input_datasets = amanager.get_datasets()
    for graph in graphs:
        for dataset, plot_params in graph:
            ds = input_datasets[dataset]
            bname = (run_name, dataset)
            buckets = phasesets.get(bname)
            if buckets is None:
                buckets = ([[] for i in range(NUM_PHASES)],
                           [[] for i in range(NUM_PHASES)])
                phasesets[bname] = buckets
            for d, (is_fwd, phase) in zip(ds, phases):
                buckets[is_fwd][phase].append(d)


######################################################################
# Graphing
######################################################################

def plot_phases(datasets, amanager, graphs, log_prefix):
    phases = [float(i) / NUM_PHASES * 4. for i in range(NUM_PHASES)]
    run_names = {}
    for run_name, dataset in datasets:
        run_names.setdefault(dataset, []).append(run_name)
    # Build plot
    fontP = matplotlib.font_manager.FontProperties()
    fontP.set_size('x-small')
    fig, rows = matplotlib.pyplot.subplots(nrows=len(graphs), ncols=2,
                                           sharex='all', sharey='row')
    if len(graphs) == 1:
        rows = [rows]
    #rows[0].set_title("Motion Analysis (%s)" % (log_prefix,))
    for graph, (graph_fwd_ax, graph_rev_ax) in zip(graphs, rows):
        for dataset, plot_params in graph:
            label = amanager.get_label(dataset)
            graph_fwd_ax.set_ylabel(label['units'])
            for run_name in run_names.get(dataset, []):
                pparams = {'label': "%s at %s" % (label['label'], run_name),
                           'alpha': 0.8}
                pparams.update(plot_params)
                graph_fwd_ax.plot(phases, datasets[(run_name, dataset)][0],
                                  **pparams)
                graph_rev_ax.plot(phases, datasets[(run_name, dataset)][1],
                                  **pparams)
        graph_rev_ax.legend(loc='best', prop=fontP)
        graph_fwd_ax.grid(True)
        graph_rev_ax.grid(True)
    rows[-1][0].set_xlabel('Full Step')
    rows[-1][1].set_xlabel('Full Step')
    return fig


######################################################################
# Startup
######################################################################

def scan_log_messages(log_prefix):
    lmanager = readlog.LogManager(log_prefix)
    lmanager.setup_index()
    mt = lmanager.get_logmsg_tracker()
    last = {}
    ranges = []
    while 1:
        m = mt.pull_next(0., 9999999999999999.)
        if m is None:
            return last.get('stepper_driver'), ranges
        last.update(m)
        if 'end' in m:
            ranges.append((last.get('start', 0.), last['end'],
                           last.get('name', '?')))

def main():
    # Parse command-line arguments
    usage = "%prog [options] <logname>"
    opts = optparse.OptionParser(usage)
    opts.add_option("-o", "--output", type="string", dest="output",
                    default=None, help="filename of output graph")
    options, args = opts.parse_args()
    if len(args) != 1:
        opts.error("Incorrect number of arguments")
    log_prefix = args[0]

    # Scan the log for start/end ranges embedded in "motan_log" messages
    stepper_driver, scan_ranges = scan_log_messages(log_prefix)
    if not scan_ranges or stepper_driver is None:
        raise Exception("No valid scan ranges found in log file")
    abs_start = scan_ranges[0][0]

    # Open data files
    lmanager = readlog.LogManager(log_prefix)
    lmanager.setup_index()
    lmanager.seek_time(abs_start - lmanager.get_initial_start_time())

    # Default graphs to draw
    graph_descs = [
        ["adxl345(adxl345,x)"],
        ["deviation(angle(angle_x),stepq(stepper_x))"],
    ]
    graphs = [[motan_graph.parse_graph_description(g) for g in graph_row]
              for graph_row in graph_descs]

    # Extract data for each range
    phasesets = {}
    for start, end, run_name in scan_ranges:
        # Setup analyzers
        amanager = analyzers.AnalyzerManager(lmanager, .000100)
        amanager.set_duration(end - start)
        for graph in graphs:
            for dataset, plot_params in graph:
                amanager.setup_dataset(dataset)

        # Extract time of each step for requested stepper driver
        times, phases = get_phase_times(lmanager, stepper_driver, start, end)
        amanager.set_dataset_times(times)
        amanager.generate_datasets()

        # Convert samples to buckets
        convert_to_phases(phasesets, run_name, amanager, graphs, phases)

    # Calculate median value of all phase samples
    datasets = {n: (calc_median(phasesets[n][0]), calc_median(phasesets[n][1]))
                for n in phasesets}

    # Draw graph
    motan_graph.setup_matplotlib(options.output is not None)
    fig = plot_phases(datasets, amanager, graphs, log_prefix)

    # Show graph
    if options.output is None:
        matplotlib.pyplot.show()
    else:
        fig.set_size_inches(8, 6)
        fig.savefig(options.output)

if __name__ == '__main__':
    main()
