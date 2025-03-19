// Second Order sections Filter implementation using Fixed Point math
//
// Copyright (C) 2020-2025 Gareth Farrington <gareth@waves.ky>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "basecmd.h" // oid_alloc
#include "command.h" // DECL_COMMAND
#include "sched.h" // shutdown
#include "sos_filter.h" // sos_filter

// filter structure sizes
#define SECTION_WIDTH 5
#define STATE_WIDTH 2

struct sos_section {
    int32_t coeff[SECTION_WIDTH];  // aka sos
    int32_t state[STATE_WIDTH];    // aka zi
};

struct sos_filter {
    uint32_t max_sections;
    int32_t num_sections;
    uint32_t shift_right;
    // filter composed of second order sections
    struct sos_section filter[0];
};

static inline uint8_t
overflows_int32(int64_t value)
{
    return value > (int64_t)INT32_MAX || value < (int64_t)INT32_MIN;
}

// Multiply a coefficient
static int32_t
fixed_mul(struct sos_filter *sf, int32_t coeff, int32_t value)
{
    // This optimizes to single cycle SMULL on Arm Coretex M0+
    int64_t product = (int64_t)coeff * (int64_t)value;
    // round up at the last bit to be shifted away
    product += 1 << (sf->shift_right - 1);
    // shift down
    int64_t result = product >> sf->shift_right;
    // check for overflow of int32_t
    if (overflows_int32(result)) {
        shutdown("fixed_mul: overflow");
    }
    // truncate significant 32 bits
    return (int32_t)result;
}

// Apply the sosfilt algorithm to a new datapoint
// returns the filtered value
int32_t
sos_filter_update(struct sos_filter *sf, int32_t unfiltered_value)
{
    if (sf->num_sections < 0)
        shutdown("sos_filter not initialized");

    int32_t cur_val = unfiltered_value;
    // foreach section
    for (int section = 0; section < sf->num_sections; section++) {
        // apply section's filter coefficients to input
        int32_t next_val = (fixed_mul(sf, sf->filter[section].coeff[0], cur_val)
                            + sf->filter[section].state[0]);
        sf->filter[section].state[0] =
            fixed_mul(sf, sf->filter[section].coeff[1], cur_val)
            - fixed_mul(sf, sf->filter[section].coeff[3], next_val)
            + (sf->filter[section].state[1]);
        sf->filter[section].state[1] =
            fixed_mul(sf, sf->filter[section].coeff[2], cur_val)
            - fixed_mul(sf, sf->filter[section].coeff[4], next_val);
        cur_val = next_val;
    }

    return cur_val;
}

// Create an sos_filter
void
command_config_sos_filter(uint32_t *args)
{
    uint32_t max_sections = args[1];
    uint32_t size = offsetof(struct sos_filter, filter[max_sections]);
    struct sos_filter *sf = oid_alloc(args[0]
                            , command_config_sos_filter, size);
    sf->max_sections = max_sections;
    sf->num_sections = -1;
}
DECL_COMMAND(command_config_sos_filter,
             "config_sos_filter oid=%c max_sections=%u");

// Lookup an sos_filter
struct sos_filter *
sos_filter_oid_lookup(uint8_t oid)
{
    return oid_lookup(oid, command_config_sos_filter);
}

// Set one section of the filter
void
command_sos_filter_set_section(uint32_t *args)
{
    struct sos_filter *sf = sos_filter_oid_lookup(args[0]);
    uint32_t section_idx = args[1];

    if (section_idx >= sf->max_sections)
        shutdown("sos_filter invalid section_idx");

    // copy section data
    const uint8_t arg_base = 2;
    for (uint8_t i = 0; i < SECTION_WIDTH; i++)
        sf->filter[section_idx].coeff[i] = args[i + arg_base];
}
DECL_COMMAND(command_sos_filter_set_section
    , "sos_filter_set_section oid=%c"
    " section_idx=%u sos0=%i sos1=%i sos2=%i sos3=%i sos4=%i");

// Set the state of one section
void
command_sos_filter_set_state(uint32_t *args)
{
    struct sos_filter *sf = sos_filter_oid_lookup(args[0]);
    uint32_t section_idx = args[1];

    if (section_idx >= sf->max_sections)
        shutdown("sos_filter invalid section_idx");

    // copy section data
    const uint8_t arg_base = 2;
    for (uint8_t i = 0; i < STATE_WIDTH; i++)
        sf->filter[section_idx].state[i] = args[i + arg_base];
}
DECL_COMMAND(command_sos_filter_set_state
    , "sos_filter_set_state oid=%c section_idx=%u state0=%i state1=%i");

// Activate the filter
void
command_sos_filter_set_active(uint32_t *args)
{
    struct sos_filter *sf = sos_filter_oid_lookup(args[0]);
    int32_t num_sections = args[1];

    if (num_sections >= sf->max_sections)
        shutdown("sos_filter invalid section_idx");

    sf->num_sections = num_sections;
    sf->shift_right = args[2];
}
DECL_COMMAND(command_sos_filter_set_active
    , "sos_filter_set_active oid=%c num_sections=%i shift_right=%u");
