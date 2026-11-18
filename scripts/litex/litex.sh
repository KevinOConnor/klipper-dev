#!/bin/bash
# This is an example script for running the litex docker container.
#
# Prior to running this script, the docker image should have been
# created with something like:
#  docker build -t litex -f litex.docker

# Disable network
DPARAM="--network none"

# Obtain directory of the software (based on the location of this
# script) and map the local firmware directory to /project .
SRCDIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )"/../.. && pwd )"
DPARAM="${DPARAM} -v ${SRCDIR}:/project:ro"
DPARAM="${DPARAM} -w /project"

# Create build directory
mkdir -p "${SRCDIR}/build"
DPARAM="${DPARAM} -v ${SRCDIR}/build:/build"

# Run container
docker run -it --rm ${DPARAM} localhost/litex "$@"
