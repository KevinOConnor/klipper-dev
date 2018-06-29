// Stepper pulse schedule compression
//
// Copyright (C) 2016-2021  Kevin O'Connor <kevin@koconnor.net>
//
// This file may be distributed under the terms of the GNU GPLv3 license.

// The goal of this code is to take a series of scheduled stepper
// pulse times and compress them into a handful of commands that can
// be efficiently transmitted and executed on a microcontroller (mcu).
// The mcu accepts step pulse commands that take interval, count, and
// add parameters such that 'count' pulses occur, with each step event
// calculating the next step event time using:
//  next_wake_time = last_wake_time + interval; interval += add
// This code is written in C (instead of python) for processing
// efficiency - the repetitive integer math is vastly faster in C.

#include <float.h> // DBL_MAX
#include <math.h> // sqrt
#include <stddef.h> // offsetof
#include <stdint.h> // uint32_t
#include <stdio.h> // fprintf
#include <stdlib.h> // malloc
#include <string.h> // memset
#include "compiler.h" // DIV_ROUND_UP
#include "pyhelper.h" // errorf
#include "serialqueue.h" // struct queue_message
#include "stepcompress.h" // stepcompress_alloc

#define CHECK_LINES 1
#define QUEUE_START_SIZE 1024

struct stepcompress {
    // Buffer management
    uint32_t *queue, *queue_end, *queue_pos, *queue_next;
    // Internal tracking
    uint32_t max_error;
    double mcu_time_offset, mcu_freq, last_step_print_time;
    uint32_t last_interval;
    uint64_t last_ideal_step_clock;
    // Message generation
    uint64_t last_step_clock;
    struct list_head msg_queue;
    uint32_t oid;
    int32_t queue_step_msgtag, set_next_step_dir_msgtag;
    int sdir, invert_sdir;
    // Step+dir+step filter
    uint64_t next_step_clock;
    int next_step_dir;
    // History tracking
    int64_t last_position;
    struct list_head history_list;
};

struct step_move {
    uint32_t interval;
    uint16_t count;
    int16_t add;
};

#define HISTORY_EXPIRE (30.0)

struct history_steps {
    struct list_node node;
    uint64_t first_clock, last_clock;
    int64_t start_position;
    int step_count, interval, add;
};


/****************************************************************
 * Step compression
 ****************************************************************/

// Helper function returning n/d while rounding up and supporting signed n
static inline int32_t
idiv_up(int32_t n, int32_t d)
{
    return (n>=0) ? DIV_ROUND_UP(n,d) : (n/d);
}

// Helper function returning n/d while rounding down and supporting signed n
static inline int32_t
idiv_down(int32_t n, int32_t d)
{
    return (n>=0) ? (n/d) : (n - d + 1) / d;
}

// Store a limited 'queue_step' schedule based on just 'add' and 'count'
struct add_move {
    int32_t add;
    int32_t count;
};

// Store a mutable reference to the stepcompress step queue
struct queue_ref {
    struct stepcompress *sc;
    uint32_t *queue_pos, *queue_end;
    uint64_t last_step_clock, last_ideal_step_clock;
    uint32_t last_interval;
};

// Initialize a 'struct queue_ref'
static void
qr_init(struct queue_ref *qr, struct stepcompress *sc, uint32_t max_count)
{
    qr->sc = sc;
    qr->queue_pos = sc->queue_pos;
    qr->queue_end = sc->queue_next;
    if (qr->queue_end > &qr->queue_pos[max_count])
        qr->queue_end = &qr->queue_pos[max_count];
    qr->last_step_clock = sc->last_step_clock;
    qr->last_ideal_step_clock = sc->last_ideal_step_clock;
    qr->last_interval = sc->last_interval;
}

// Generate a 'struct queue_ref' state after a 'struct add_move' is scheduled
static void
qr_after_move(struct queue_ref *nqr, struct queue_ref *oqr, struct add_move *am)
{
    memcpy(nqr, oqr, sizeof(*nqr));
    int32_t add = am->add, count = am->count, addfactor = count*(count+1)/2;
    nqr->last_ideal_step_clock = ((nqr->queue_pos[count - 1]
                                   - (uint32_t)nqr->last_step_clock)
                                  + nqr->last_step_clock);
    nqr->queue_pos += count;
    nqr->last_step_clock += nqr->last_interval * count + addfactor * add;
    nqr->last_interval += count * add;
}

// Storage for the maximum and minimum range a step may be scheduled at
struct points {
    int32_t minp, maxp;
};

// Given a requested step time, return the minimum and maximum
// acceptable times
static inline struct points
minmax_point(struct queue_ref *qr, uint32_t *pos)
{
    uint32_t lsc = qr->last_step_clock, point = *pos - lsc;
    uint32_t prevpoint = pos > qr->queue_pos ? *(pos-1) - lsc : 0;
    uint32_t max_error = (point - prevpoint) / 2;
    if (max_error > qr->sc->max_error)
        max_error = qr->sc->max_error;
    return (struct points){ point - max_error, point };
}

// Store the minimum and maximum "add" a queue_step may schedule
struct add_range {
    int32_t minadd, maxadd, count;
};

// Initialize a 'struct add_range'
static void
add_range_init(struct add_range *ar)
{
    ar->minadd = -0x8000;
    ar->maxadd = 0x7fff;
    ar->count = 0;
}

// Add a step to a 'struct add_range' if possible
static int
add_range_update(struct add_range *ar, struct queue_ref *qr)
{
    if (&qr->queue_pos[ar->count] >= qr->queue_end)
        return -1;
    struct points nextpoint = minmax_point(qr, &qr->queue_pos[ar->count]);

    // Check if can extend sequence
    int32_t nextcount = ar->count + 1;
    int32_t nextaddfactor = nextcount*(nextcount+1)/2;
    int32_t interval = qr->last_interval;
    int32_t minadd = ar->minadd, maxadd = ar->maxadd;
    int32_t nextminadd = minadd, nextmaxadd = maxadd;
    if (interval*nextcount + minadd*nextaddfactor < nextpoint.minp)
        nextminadd = idiv_up(nextpoint.minp - interval*nextcount
                             , nextaddfactor);
    if (interval*nextcount + maxadd*nextaddfactor > nextpoint.maxp)
        nextmaxadd = idiv_down(nextpoint.maxp - interval*nextcount
                               , nextaddfactor);
    if (nextminadd > nextmaxadd)
        return -1;
    ar->minadd = nextminadd;
    ar->maxadd = nextmaxadd;
    ar->count = nextcount;
    return 0;
}

// Find the longest valid 'struct add_range' schedule
static void
add_range_scan(struct add_range *ar, struct queue_ref *qr)
{
    add_range_init(ar);
    for (;;) {
        int ret = add_range_update(ar, qr);
        if (ret)
            return;
    }
}

// Calculate the "ideal interval" - the ticks since the last ideal step time
static int32_t
ideal_interval(struct queue_ref *qr, uint32_t *pos)
{
    if (pos > qr->queue_pos)
        return *pos - *(pos - 1);
    return *pos - (uint32_t)qr->last_ideal_step_clock;
}

// Calculate the step time after an add1,count1 and add2,count2 schedule
static int32_t
calc_seq(struct queue_ref *qr, int32_t add1, int32_t add2
         , int32_t c1, int32_t tc)
{
    int32_t ad = add1 - add2;
    int32_t addfactor = tc*(tc+1)/2, paddfactor = c1*(c1-1)/2;
    return qr->last_interval*tc + add2*addfactor + ad*(c1*tc - paddfactor);
}

// The "leastsquares" compression code attempts to find a valid
// add1,count1 sequence that maximizes the "total reach" of a
// subsequent add2,count2 sequence (maximize count1+count2).  The code
// finds the simultaneous solution to a set of equations (one per
// step) of the following form:
//   add1*ac1 + add2*ac2 = adjusted_ideal_interval
// Where ac1, ac2, and adjusted_ideal_interval are constants for a
// given step time.

// Estimate best add1,count1 using leastsquares on totalcount steps
static struct add_move
calc_leastsquares(struct queue_ref *qr, int32_t totalcount)
{
    // Setup initial least squares variance and covariance values
    double var_ac1 = 0., var_ac2 = 0., cov_ac1_ac2 = 0.;
    double cov_ac1_aii = 0., cov_ac2_aii = 0., sum_aii = 0.;
    //double var_aii = 0.;
    int32_t step;
    for (step=1; step<=totalcount; step++) {
        int32_t want_interval = ideal_interval(qr, qr->queue_pos + step - 1);
        int32_t aii = want_interval - qr->last_interval;
        double dac2 = step, daii = aii;
        cov_ac2_aii += dac2 * daii;
        var_ac2 += dac2 * dac2;
        //var_aii += daii * daii;
        sum_aii += daii;
    }
    double condsum_aii = sum_aii;

    // Calc least squares on all possible count1 to find overall best solution
    struct add_range ar;
    add_range_init(&ar);
    double best_e2 = DBL_MAX;
    struct add_move best = {0, 0};
    for (;;) {
        int ret = add_range_update(&ar, qr);
        if (ret)
            // Can not further increase count1 - return best result found
            return best;
        int32_t count1 = ar.count;

        // Update leastsquares with new count1
        int32_t want_interval = ideal_interval(qr, qr->queue_pos + count1 - 1);
        int32_t aii = want_interval - qr->last_interval;
        cov_ac2_aii -= condsum_aii;
        cov_ac1_aii += condsum_aii;
        condsum_aii -= aii;
        int32_t pc2 = totalcount - count1 + 1, paf = pc2*(pc2+1)/2;
        int32_t va_diff = pc2 * pc2, caa_diff = paf - count1*pc2;
        cov_ac1_ac2 += caa_diff;
        var_ac2 -= va_diff;
        var_ac1 += va_diff - 2 * caa_diff;

        // Calculate add1 and constrain to valid range
        double dadd2 = 0.;
        if (count1 < totalcount) {
            double determinant = var_ac1*var_ac2 - cov_ac1_ac2*cov_ac1_ac2;
            double v = var_ac1*cov_ac2_aii - cov_ac1_ac2*cov_ac1_aii;
            dadd2 = round(v / determinant);
        }
        double dadd1 = round((cov_ac1_aii - dadd2*cov_ac1_ac2) / var_ac1);
        int32_t add1 = dadd1;
        add1 = add1 > ar.maxadd ? ar.maxadd : add1;
        add1 = add1 < ar.minadd ? ar.minadd : add1;
        dadd1 = add1;

        // Recalculate add2 and make sure fits in last step range
        if (count1 < totalcount)
            dadd2 = round((cov_ac2_aii - dadd1*cov_ac1_ac2) / var_ac2);
        int add2 = dadd2;
        struct points lastr = minmax_point(qr, qr->queue_pos + totalcount - 1);
        int32_t lastp = calc_seq(qr, add1, add2, count1, totalcount);
        int32_t count2 = totalcount - count1, af = count2*(count2+1)/2;
        if (lastp < lastr.minp) {
            if (lastp + af > lastr.maxp)
                continue;
            add2 += DIV_ROUND_UP(lastr.minp - lastp, af);
        } else if (lastp > lastr.maxp) {
            if (lastp - af < lastr.minp)
                continue;
            add2 -= DIV_ROUND_UP(lastp - lastr.maxp, af);
        }
        dadd2 = add2;

        // Estimate relative squared error (add var_aii for absolute error)
        double rel_error2 = (dadd1*dadd1*var_ac1 + dadd2*dadd2*var_ac2
                             + 2*dadd1*dadd2*cov_ac1_ac2
                             - 2*dadd1*cov_ac1_aii - 2*dadd2*cov_ac2_aii);
        if (rel_error2 <= best_e2) {
            best.add = add1;
            best.count = count1;
            best_e2 = rel_error2;
        }
    }
}

// Compress a step schedule using leastsquares method
static struct add_move
compress_leastsquares(struct queue_ref *qr)
{
    // Find longest valid count1
    struct add_range outer_ar1;
    add_range_scan(&outer_ar1, qr);
    int32_t outer_count1 = outer_ar1.count;
    if (!outer_count1) {
        uint32_t interval = qr->queue_pos[0] - qr->last_step_clock;
        uint32_t st = interval - qr->last_interval - qr->sc->max_error / 2;
        return (struct add_move){ st, 1 };
    }

    // Try finding longest valid "totalcount" by repeatedly running leastsquares
    int32_t outer_add1 = (outer_ar1.minadd + outer_ar1.maxadd) / 2;
    struct add_move prev = {outer_add1, outer_count1}, next = prev;
    int32_t prev_totalcount = 0;
    for (;;) {
        // Determine maximum reachable totalcount given count1,add1
        struct queue_ref qr2;
        qr_after_move(&qr2, qr, &next);
        struct add_range ar;
        add_range_scan(&ar, &qr2);
        int32_t totalcount = next.count + ar.count;

        // Calculate new add1,count1 using least squares (if needed)
        if (prev_totalcount >= totalcount)
            return prev;
        prev = next;
        prev_totalcount = totalcount;
        next = calc_leastsquares(qr, totalcount);
    }
}

// Convert a 'struct add_move' to a 'struct step_move'
static struct step_move
wrap_compress(struct stepcompress *sc)
{
    struct queue_ref qref, *qr = &qref;
    qr_init(qr, sc, 46000);

    struct add_move am1 = compress_leastsquares(qr);
    if (am1.count == 1 && qr->queue_pos + 1 < qr->queue_end) {
        // Check if two 'struct add_move' can be sent in one 'struct step_move'
        struct queue_ref qr2;
        qr_after_move(&qr2, qr, &am1);
        struct add_move am2 = compress_leastsquares(&qr2);
        if (am2.add >= -0x8000 && am2.add <= 0x7fff)
            return (struct step_move){ qr->last_interval + am1.add,
                                       am2.count + 1, am2.add };
    }

    return (struct step_move){ qr->last_interval + am1.add, am1.count,
                               am1.count > 1 ? am1.add : 0 };
}


/****************************************************************
 * Step compress checking
 ****************************************************************/

// Verify that a given 'step_move' matches the actual step times
static int
check_line(struct stepcompress *sc, struct step_move move)
{
    if (!CHECK_LINES)
        return 0;
    if (!move.count || (!move.interval && !move.add && move.count > 1)
        || move.interval >= 0x80000000) {
        errorf("stepcompress o=%d i=%d c=%d a=%d: Invalid sequence"
               , sc->oid, move.interval, move.count, move.add);
        return ERROR_RET;
    }
    struct queue_ref qr;
    qr_init(&qr, sc, 65535);
    uint32_t interval = move.interval, p = 0;
    uint16_t i;
    for (i=0; i<move.count; i++) {
        struct points point = minmax_point(&qr, qr.queue_pos + i);
        p += interval;
        if (p < point.minp || p > point.maxp) {
            errorf("stepcompress o=%d i=%d c=%d a=%d: Point %d: %d not in %d:%d"
                   , sc->oid, move.interval, move.count, move.add
                   , i+1, p, point.minp, point.maxp);
            return ERROR_RET;
        }
        if (interval >= 0x80000000) {
            errorf("stepcompress o=%d i=%d c=%d a=%d:"
                   " Point %d: interval overflow %d"
                   , sc->oid, move.interval, move.count, move.add
                   , i+1, interval);
            return ERROR_RET;
        }
        interval += move.add;
    }
    return 0;
}


/****************************************************************
 * Step compress interface
 ****************************************************************/

// Allocate a new 'stepcompress' object
struct stepcompress * __visible
stepcompress_alloc(uint32_t oid)
{
    struct stepcompress *sc = malloc(sizeof(*sc));
    memset(sc, 0, sizeof(*sc));
    list_init(&sc->msg_queue);
    list_init(&sc->history_list);
    sc->oid = oid;
    sc->sdir = -1;
    return sc;
}

// Fill message id information
void __visible
stepcompress_fill(struct stepcompress *sc, uint32_t max_error
                  , int32_t queue_step_msgtag, int32_t set_next_step_dir_msgtag)
{
    sc->max_error = max_error;
    sc->queue_step_msgtag = queue_step_msgtag;
    sc->set_next_step_dir_msgtag = set_next_step_dir_msgtag;
}

// Set the inverted stepper direction flag
void __visible
stepcompress_set_invert_sdir(struct stepcompress *sc, uint32_t invert_sdir)
{
    invert_sdir = !!invert_sdir;
    if (invert_sdir != sc->invert_sdir) {
        sc->invert_sdir = invert_sdir;
        if (sc->sdir >= 0)
            sc->sdir ^= 1;
    }
}

// Helper to free items from the history_list
static void
free_history(struct stepcompress *sc, uint64_t end_clock)
{
    while (!list_empty(&sc->history_list)) {
        struct history_steps *hs = list_last_entry(
            &sc->history_list, struct history_steps, node);
        if (hs->last_clock > end_clock)
            break;
        list_del(&hs->node);
        free(hs);
    }
}

// Free memory associated with a 'stepcompress' object
void __visible
stepcompress_free(struct stepcompress *sc)
{
    if (!sc)
        return;
    free(sc->queue);
    message_queue_free(&sc->msg_queue);
    free_history(sc, UINT64_MAX);
    free(sc);
}

uint32_t
stepcompress_get_oid(struct stepcompress *sc)
{
    return sc->oid;
}

int
stepcompress_get_step_dir(struct stepcompress *sc)
{
    return sc->next_step_dir;
}

// Determine the "print time" of the last_step_clock
static void
calc_last_step_print_time(struct stepcompress *sc)
{
    double lsc = sc->last_step_clock;
    sc->last_step_print_time = sc->mcu_time_offset + (lsc - .5) / sc->mcu_freq;

    if (lsc > sc->mcu_freq * HISTORY_EXPIRE)
        free_history(sc, lsc - sc->mcu_freq * HISTORY_EXPIRE);
}

// Set the conversion rate of 'print_time' to mcu clock
static void
stepcompress_set_time(struct stepcompress *sc
                      , double time_offset, double mcu_freq)
{
    sc->mcu_time_offset = time_offset;
    sc->mcu_freq = mcu_freq;
    calc_last_step_print_time(sc);
}

// Maximium clock delta between messages in the queue
#define CLOCK_DIFF_MAX (3<<28)

// Helper to create a queue_step command from a 'struct step_move'
static void
add_move(struct stepcompress *sc, uint64_t first_clock, struct step_move *move)
{
    int32_t addfactor = move->count*(move->count-1)/2;
    uint32_t ticks = move->add*addfactor + move->interval*(move->count-1);
    uint64_t last_clock = first_clock + ticks;
    sc->last_interval = move->interval + move->add*(move->count-1);

    // Create and queue a queue_step command
    uint32_t msg[5] = {
        sc->queue_step_msgtag, sc->oid, move->interval, move->count, move->add
    };
    struct queue_message *qm = message_alloc_and_encode(msg, 5);
    qm->min_clock = qm->req_clock = sc->last_step_clock;
    if (move->count == 1 && first_clock >= sc->last_step_clock + CLOCK_DIFF_MAX)
        qm->req_clock = first_clock;
    list_add_tail(&qm->node, &sc->msg_queue);
    sc->last_step_clock = last_clock;

    // Create and store move in history tracking
    struct history_steps *hs = malloc(sizeof(*hs));
    hs->first_clock = first_clock;
    hs->last_clock = last_clock;
    hs->start_position = sc->last_position;
    hs->interval = move->interval;
    hs->add = move->add;
    hs->step_count = sc->sdir ? move->count : -move->count;
    sc->last_position += hs->step_count;
    list_add_head(&hs->node, &sc->history_list);
}

// Convert previously scheduled steps into commands for the mcu
static int
queue_flush(struct stepcompress *sc, uint64_t move_clock)
{
    if (sc->queue_pos >= sc->queue_next)
        return 0;
    while (sc->last_step_clock < move_clock) {
        struct step_move move = wrap_compress(sc);
        int ret = check_line(sc, move);
        if (ret)
            return ret;

        sc->last_ideal_step_clock = ((sc->queue_pos[move.count - 1]
                                      - (uint32_t)sc->last_step_clock)
                                     + sc->last_step_clock);
        add_move(sc, sc->last_step_clock + move.interval, &move);

        if (sc->queue_pos + move.count >= sc->queue_next) {
            sc->queue_pos = sc->queue_next = sc->queue;
            break;
        }
        sc->queue_pos += move.count;
    }
    calc_last_step_print_time(sc);
    return 0;
}

// Generate a queue_step for a step far in the future from the last step
static int
stepcompress_flush_far(struct stepcompress *sc, uint64_t abs_step_clock)
{
    struct step_move move = { abs_step_clock - sc->last_step_clock, 1, 0 };
    sc->last_ideal_step_clock = abs_step_clock;
    add_move(sc, abs_step_clock, &move);
    calc_last_step_print_time(sc);
    return 0;
}

// Send the set_next_step_dir command
static int
set_next_step_dir(struct stepcompress *sc, int sdir)
{
    if (sc->sdir == sdir)
        return 0;
    int ret = queue_flush(sc, UINT64_MAX);
    if (ret)
        return ret;
    sc->sdir = sdir;
    uint32_t msg[3] = {
        sc->set_next_step_dir_msgtag, sc->oid, sdir ^ sc->invert_sdir
    };
    struct queue_message *qm = message_alloc_and_encode(msg, 3);
    qm->req_clock = sc->last_step_clock;
    list_add_tail(&qm->node, &sc->msg_queue);
    return 0;
}

// Slow path for queue_append() - handle next step far in future
static int
queue_append_far(struct stepcompress *sc)
{
    uint64_t step_clock = sc->next_step_clock;
    sc->next_step_clock = 0;
    int ret = queue_flush(sc, step_clock - CLOCK_DIFF_MAX + 1);
    if (ret)
        return ret;
    if (step_clock >= sc->last_step_clock + CLOCK_DIFF_MAX)
        return stepcompress_flush_far(sc, step_clock);
    *sc->queue_next++ = step_clock;
    return 0;
}

// Slow path for queue_append() - expand the internal queue storage
static int
queue_append_extend(struct stepcompress *sc)
{
    if (sc->queue_next - sc->queue_pos > 65535 + 2000) {
        // No point in keeping more than 64K steps in memory
        uint32_t flush = (*(sc->queue_next-65535)
                          - (uint32_t)sc->last_step_clock);
        int ret = queue_flush(sc, sc->last_step_clock + flush);
        if (ret)
            return ret;
    }

    if (sc->queue_next >= sc->queue_end) {
        // Make room in the queue
        int in_use = sc->queue_next - sc->queue_pos;
        if (sc->queue_pos > sc->queue) {
            // Shuffle the internal queue to avoid having to allocate more ram
            memmove(sc->queue, sc->queue_pos, in_use * sizeof(*sc->queue));
        } else {
            // Expand the internal queue of step times
            int alloc = sc->queue_end - sc->queue;
            if (!alloc)
                alloc = QUEUE_START_SIZE;
            while (in_use >= alloc)
                alloc *= 2;
            sc->queue = realloc(sc->queue, alloc * sizeof(*sc->queue));
            sc->queue_end = sc->queue + alloc;
        }
        sc->queue_pos = sc->queue;
        sc->queue_next = sc->queue + in_use;
    }

    *sc->queue_next++ = sc->next_step_clock;
    sc->next_step_clock = 0;
    return 0;
}

// Add a step time to the queue (flushing the queue if needed)
static int
queue_append(struct stepcompress *sc)
{
    if (unlikely(sc->next_step_dir != sc->sdir)) {
        int ret = set_next_step_dir(sc, sc->next_step_dir);
        if (ret)
            return ret;
    }
    if (unlikely(sc->next_step_clock >= sc->last_step_clock + CLOCK_DIFF_MAX))
        return queue_append_far(sc);
    if (unlikely(sc->queue_next >= sc->queue_end))
        return queue_append_extend(sc);
    *sc->queue_next++ = sc->next_step_clock;
    sc->next_step_clock = 0;
    return 0;
}

#define SDS_FILTER_TIME .000750

// Add next step time
int
stepcompress_append(struct stepcompress *sc, int sdir
                    , double print_time, double step_time)
{
    // Calculate step clock
    double offset = print_time - sc->last_step_print_time;
    double rel_sc = (step_time + offset) * sc->mcu_freq;
    uint64_t step_clock = sc->last_step_clock + (uint64_t)rel_sc;
    // Flush previous pending step (if any)
    if (sc->next_step_clock) {
        if (unlikely(sdir != sc->next_step_dir)) {
            double diff = (int64_t)(step_clock - sc->next_step_clock);
            if (diff < SDS_FILTER_TIME * sc->mcu_freq) {
                // Rollback last step to avoid rapid step+dir+step
                sc->next_step_clock = 0;
                sc->next_step_dir = sdir;
                return 0;
            }
        }
        int ret = queue_append(sc);
        if (ret)
            return ret;
    }
    // Store this step as the next pending step
    sc->next_step_clock = step_clock;
    sc->next_step_dir = sdir;
    return 0;
}

// Commit next pending step (ie, do not allow a rollback)
int
stepcompress_commit(struct stepcompress *sc)
{
    if (sc->next_step_clock)
        return queue_append(sc);
    return 0;
}

// Flush pending steps
static int
stepcompress_flush(struct stepcompress *sc, uint64_t move_clock)
{
    if (sc->next_step_clock && move_clock >= sc->next_step_clock) {
        int ret = queue_append(sc);
        if (ret)
            return ret;
    }
    return queue_flush(sc, move_clock);
}

// Reset the internal state of the stepcompress object
int __visible
stepcompress_reset(struct stepcompress *sc, uint64_t last_step_clock)
{
    int ret = stepcompress_flush(sc, UINT64_MAX);
    if (ret)
        return ret;
    sc->last_step_clock = last_step_clock;
    sc->last_interval = 0;
    sc->sdir = -1;
    calc_last_step_print_time(sc);
    return 0;
}

// Set last_position in the stepcompress object
int __visible
stepcompress_set_last_position(struct stepcompress *sc, uint64_t clock
                               , int64_t last_position)
{
    int ret = stepcompress_flush(sc, UINT64_MAX);
    if (ret)
        return ret;
    sc->last_position = last_position;

    // Add a marker to the history list
    struct history_steps *hs = malloc(sizeof(*hs));
    memset(hs, 0, sizeof(*hs));
    hs->first_clock = hs->last_clock = clock;
    hs->start_position = last_position;
    list_add_head(&hs->node, &sc->history_list);
    return 0;
}

// Search history of moves to find a past position at a given clock
int64_t __visible
stepcompress_find_past_position(struct stepcompress *sc, uint64_t clock)
{
    int64_t last_position = sc->last_position;
    struct history_steps *hs;
    list_for_each_entry(hs, &sc->history_list, node) {
        if (clock < hs->first_clock) {
            last_position = hs->start_position;
            continue;
        }
        if (clock >= hs->last_clock)
            return hs->start_position + hs->step_count;
        int32_t interval = hs->interval, add = hs->add;
        int32_t ticks = (int32_t)(clock - hs->first_clock) + interval, offset;
        if (!add) {
            offset = ticks / interval;
        } else {
            // Solve for "count" using quadratic formula
            double a = .5 * add, b = interval - .5 * add, c = -ticks;
            offset = (sqrt(b*b - 4*a*c) - b) / (2. * a);
        }
        if (hs->step_count < 0)
            return hs->start_position - offset;
        return hs->start_position + offset;
    }
    return last_position;
}

// Queue an mcu command to go out in order with stepper commands
int __visible
stepcompress_queue_msg(struct stepcompress *sc, uint32_t *data, int len)
{
    int ret = stepcompress_flush(sc, UINT64_MAX);
    if (ret)
        return ret;

    struct queue_message *qm = message_alloc_and_encode(data, len);
    qm->req_clock = sc->last_step_clock;
    list_add_tail(&qm->node, &sc->msg_queue);
    return 0;
}

// Return history of queue_step commands
int __visible
stepcompress_extract_old(struct stepcompress *sc, struct pull_history_steps *p
                         , int max, uint64_t start_clock, uint64_t end_clock)
{
    int res = 0;
    struct history_steps *hs;
    list_for_each_entry(hs, &sc->history_list, node) {
        if (start_clock >= hs->last_clock || res >= max)
            break;
        if (end_clock <= hs->first_clock)
            continue;
        p->first_clock = hs->first_clock;
        p->last_clock = hs->last_clock;
        p->start_position = hs->start_position;
        p->step_count = hs->step_count;
        p->interval = hs->interval;
        p->add = hs->add;
        p++;
        res++;
    }
    return res;
}


/****************************************************************
 * Step compress synchronization
 ****************************************************************/

// The steppersync object is used to synchronize the output of mcu
// step commands.  The mcu can only queue a limited number of step
// commands - this code tracks when items on the mcu step queue become
// free so that new commands can be transmitted.  It also ensures the
// mcu step queue is ordered between steppers so that no stepper
// starves the other steppers of space in the mcu step queue.

struct steppersync {
    // Serial port
    struct serialqueue *sq;
    struct command_queue *cq;
    // Storage for associated stepcompress objects
    struct stepcompress **sc_list;
    int sc_num;
    // Storage for list of pending move clocks
    uint64_t *move_clocks;
    int num_move_clocks;
};

// Allocate a new 'steppersync' object
struct steppersync * __visible
steppersync_alloc(struct serialqueue *sq, struct stepcompress **sc_list
                  , int sc_num, int move_num)
{
    struct steppersync *ss = malloc(sizeof(*ss));
    memset(ss, 0, sizeof(*ss));
    ss->sq = sq;
    ss->cq = serialqueue_alloc_commandqueue();

    ss->sc_list = malloc(sizeof(*sc_list)*sc_num);
    memcpy(ss->sc_list, sc_list, sizeof(*sc_list)*sc_num);
    ss->sc_num = sc_num;

    ss->move_clocks = malloc(sizeof(*ss->move_clocks)*move_num);
    memset(ss->move_clocks, 0, sizeof(*ss->move_clocks)*move_num);
    ss->num_move_clocks = move_num;

    return ss;
}

// Free memory associated with a 'steppersync' object
void __visible
steppersync_free(struct steppersync *ss)
{
    if (!ss)
        return;
    free(ss->sc_list);
    free(ss->move_clocks);
    serialqueue_free_commandqueue(ss->cq);
    free(ss);
}

// Set the conversion rate of 'print_time' to mcu clock
void __visible
steppersync_set_time(struct steppersync *ss, double time_offset
                     , double mcu_freq)
{
    int i;
    for (i=0; i<ss->sc_num; i++) {
        struct stepcompress *sc = ss->sc_list[i];
        stepcompress_set_time(sc, time_offset, mcu_freq);
    }
}

// Implement a binary heap algorithm to track when the next available
// 'struct move' in the mcu will be available
static void
heap_replace(struct steppersync *ss, uint64_t req_clock)
{
    uint64_t *mc = ss->move_clocks;
    int nmc = ss->num_move_clocks, pos = 0;
    for (;;) {
        int child1_pos = 2*pos+1, child2_pos = 2*pos+2;
        uint64_t child2_clock = child2_pos < nmc ? mc[child2_pos] : UINT64_MAX;
        uint64_t child1_clock = child1_pos < nmc ? mc[child1_pos] : UINT64_MAX;
        if (req_clock <= child1_clock && req_clock <= child2_clock) {
            mc[pos] = req_clock;
            break;
        }
        if (child1_clock < child2_clock) {
            mc[pos] = child1_clock;
            pos = child1_pos;
        } else {
            mc[pos] = child2_clock;
            pos = child2_pos;
        }
    }
}

// Find and transmit any scheduled steps prior to the given 'move_clock'
int __visible
steppersync_flush(struct steppersync *ss, uint64_t move_clock)
{
    // Flush each stepcompress to the specified move_clock
    int i;
    for (i=0; i<ss->sc_num; i++) {
        int ret = stepcompress_flush(ss->sc_list[i], move_clock);
        if (ret)
            return ret;
    }

    // Order commands by the reqclock of each pending command
    struct list_head msgs;
    list_init(&msgs);
    for (;;) {
        // Find message with lowest reqclock
        uint64_t req_clock = MAX_CLOCK;
        struct queue_message *qm = NULL;
        for (i=0; i<ss->sc_num; i++) {
            struct stepcompress *sc = ss->sc_list[i];
            if (!list_empty(&sc->msg_queue)) {
                struct queue_message *m = list_first_entry(
                    &sc->msg_queue, struct queue_message, node);
                if (m->req_clock < req_clock) {
                    qm = m;
                    req_clock = m->req_clock;
                }
            }
        }
        if (!qm || (qm->min_clock && req_clock > move_clock))
            break;

        uint64_t next_avail = ss->move_clocks[0];
        if (qm->min_clock)
            // The qm->min_clock field is overloaded to indicate that
            // the command uses the 'move queue' and to store the time
            // that move queue item becomes available.
            heap_replace(ss, qm->min_clock);
        // Reset the min_clock to its normal meaning (minimum transmit time)
        qm->min_clock = next_avail;

        // Batch this command
        list_del(&qm->node);
        list_add_tail(&qm->node, &msgs);
    }

    // Transmit commands
    if (!list_empty(&msgs))
        serialqueue_send_batch(ss->sq, ss->cq, &msgs);
    return 0;
}
