#!/usr/bin/env python
# Debugging script for graphing results of eddy "tap" analysis
import optparse, math, logging, ast
import numpy, matplotlib


######################################################################
# Parsing
######################################################################

def parse_log(logname):
    f = open(logname, 'r')
    out = []
    samples = []
    z = 0.
    coeffs = [0., 0., 0., 0.]
    for line in f:
        parts = line.split(None, 3)
        if not parts:
            continue
        if parts[0] == 'sample:':
            #sample: freq=3250083.357 z=1.950469
            samples.append((float(parts[1][5:]), float(parts[2][2:])))
            continue
        if parts[0] == 'best:':
            #best: z=1.502794 err=685956.104611 coeffs=[1.0, 2.0, 3.0, 4.0]
            z = float(parts[1][2:])
            coeffs = ast.literal_eval(parts[3][7:].strip())
            out.append((z, coeffs, samples))
            samples = []
    f.close()
    return out


######################################################################
# Graphing
######################################################################

def plot_cal(data):
    # Determine best "fit" lines
    def formula(z):
        f = coeffs[0] + coeffs[1]*z
        zdiff = max(0., z - z_contact)
        return f + coeffs[2]*zdiff + coeffs[3]*(zdiff*zdiff)
    lines = []
    for z_contact, coeffs, samples in data:
        # Generate fit line
        min_z = samples[0][1]
        max_z = samples[-1][1]
        line_zs = list(numpy.arange(min_z, max_z, 0.001))
        line_freqs = list([formula(z) for z in line_zs])
        lines.append((z_contact, samples, line_zs, line_freqs))
    # Determine sample positions
    # Build plot
    fig, ax1 = matplotlib.pyplot.subplots()
    ax1.set_title("Eddy tap results")
    ax1.set_xlabel('Z (mm)')
    ax1.set_ylabel('Freq (hz)')
    for i, (z_contact, samples, line_zs, line_freqs) in enumerate(lines):
        freqs = [f for f, z in samples]
        zpos = [z for f, z in samples]
        a1 = ax1.plot(zpos, freqs, '.', alpha=0.4)
        color = a1[0].get_color()
        title = 'z=%.6f' % (z_contact,)
        if len(lines) > 1:
            title = '%d: %s' % (i, title)
        ax1.plot(line_zs, line_freqs, '-', label=title, color=color, alpha=0.4)
    fontP = matplotlib.font_manager.FontProperties()
    fontP.set_size('x-small')
    ax1.legend(loc='best', prop=fontP)
    ax1.grid(True)
    return fig


######################################################################
# Startup
######################################################################

def setup_matplotlib(output_to_file):
    global matplotlib
    if output_to_file:
        matplotlib.use('Agg')
    import matplotlib.pyplot, matplotlib.dates, matplotlib.font_manager
    import matplotlib.ticker

def main():
    usage = "%prog [options] <cfgfile>"
    opts = optparse.OptionParser(usage)
    opts.add_option("-n", "--num", type="int", dest="graph_num", default=None,
                    help="graph instance to show")
    opts.add_option("-o", "--output", type="string", dest="output",
                    default=None, help="filename of output graph")
    options, args = opts.parse_args()
    if len(args) != 1:
        opts.error("Incorrect number of arguments")
    logname = args[0]
    data = parse_log(logname)
    if not data:
        print("No data to show")
        return
    if options.graph_num is not None:
        if options.graph_num < 0 or options.graph_num >= len(data):
            print("Graph num %d not available" % (options.graph_num,))
            return
        data = [data[options.graph_num]]

    setup_matplotlib(options.output is not None)
    fig = plot_cal(data)
    # Show graph
    if options.output is None:
        matplotlib.pyplot.show()
    else:
        fig.set_size_inches(8, 6)
        fig.savefig(options.output)

if __name__ == '__main__':
    main()
