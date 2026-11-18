# Klipper on LiteX

This document provides information on building and deploying Klipper
on an FPGA running the [LiteX](https://github.com/enjoy-digital/litex)
System on Chip (SoC) infrastructure.

This document provides examples that assume one has access to a Linux
machine with
[Docker](https://en.wikipedia.org/wiki/Docker_(software)).

## Installing a container to build the micro-controller code

Building Klipper for LiteX requires a riscv toolchain that may not be
available in a standard Linux operating system. The
`scripts/litex/mcubuild.docker` file provides an example docker file
that one may use to build a docker container with the required
toolchain. One would typically create the container by running:
`docker build -t mcubuild -f scripts/litex/mcubuild.docker`

## Installing the LiteX framework

In order to deploy Klipper on a LiteX SoC it will be necessary to
build an FPGA bitstream using LiteX. The full steps needed to build
LiteX are outside the scope of this document, and one should consult
the LiteX documentation for details. However, to aid as an example,
the Klipper repository contains an example docker file
(`scripts/litex/litex.docker`) that one may use to build LiteX for
popular "ECP5" boards (such as the "Colorlight 5A-75B" board). The
resulting docker container contains both the LiteX and the Yosys ECP5
toolchain.

To build the example LiteX container use a command like the following:
`docker build -t litex -f scripts/litex/litex.docker`

## Creating the LiteX image and csr.json file

The Klipper MCU software build requires definitions from the
`csr.json` file that is produced during a LiteX build.

Klipper also requires that the LiteX build have several options
defined:
* `--timer-uptime` so that the standard LiteX timer includes the
  "uptime" counter.
* `--with-watchdog` to enable the watchdog module.
* `--csr-ordering little` to ensure the embedded CSR interface uses
  little-endian addresses.
* `--uart-baudrate 250000` is recommended. If any other baud is used
  then one must ensure the same baud is specified in the Klipper "make
  menuconfig", Klipper printer.cfg, and in LiteX. If the baud values
  are not synchronized in all three locations then hard to debug
  errors may result.

If one is using the example `litex.docker` container, then one may
also use the example `scripts/litex/litex.sh` shell script to run the
container. One can create both the LiteX bitstream and the `csr.json`
file with a command like the following:
`./scripts/litex/litex.sh bash -c 'source /litex-venv/bin/activate ; ~/litex-boards/litex_boards/targets/colorlight_5a_75x.py --revision=8.2 --ecppack-compress --output-dir /build --build --timer-uptime --with-watchdog --watchdog-reset-delay 1 --csr-ordering little --uart-baudrate 250000'`

The above example `litex.sh` script should create a `build/` directory
in the main Klipper source directory.  The `csr.json` file can then be
found in that `build/` directory.

## Running Klipper "make menuconfig"

Once the `csr.json` file is available one may configure the Klipper
MCU code. When invoking the Klipper Makefile for LiteX builds one
should always pass a `CSRFILE` parameter to the `make` command with a
path to the `csr.json` file (eg, `make CSRFILE=/path/to/csr.json`).

If one is using the example mcubuild docker container then one may
also use the example `scripts/litex/mcubuild.sh` shell script to run
that container. That shell script will automatically call `make` and
pass the `CSRFILE=build/csr.json` parameter to it.  For example:
`./scripts/litex/mcubuild.sh menuconfig`

The Klipper menuconfig system will prompt for two LiteX memory
regions - one for the memory region that will store the code and one
for the memory region for general purpose data storage. Both of these
are specific to the FPGA board one is using. If the names of these
regions are not known one may look at the `csr.json` file or the LiteX
log (eg, `build/litex.log`) to see what memory regions are available.

## Building the Klipper mcu code

Finally, the Klipper code is built using the normal `make` command. If
using the example mcubuild docker container then one may build the
code with something like:
`./scripts/litex/mcubuild.sh`

The example mcubuild docker container places the final Klipper build
in the file: `build/klipper_out/klipper.bin`

The example litex docker container places the final FPGA gateware in
the directory: `build/gateware/`

After the FPGA bitstream and Klipper mcu are built the next step is to
load them on to the target FPGA. The steps needed to perform that are
specific to the type of FPGA and programming hardware; the steps are
outside the scope of this document. See the LiteX documentation for
details.
