#ifndef __SOS_FILTER_H
#define __SOS_FILTER_H

#include <stdint.h> // uint32_t

struct sos_filter *sos_filter_oid_lookup(uint8_t oid);
int32_t sos_filter_update(struct sos_filter *sf, int32_t unfiltered_value);

#endif // sos_filter.h
