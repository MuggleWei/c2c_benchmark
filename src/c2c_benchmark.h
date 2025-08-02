/******************************************************************************
 *  @file         c2c_benchmark.h
 *  @author       Muggle Wei
 *  @email        mugglewei@gmail.com
 *  @date         2025-08-02
 *  @copyright    Copyright 2025 Muggle Wei
 *  @license      MIT License
 *  @brief        c2c benchmark
 *****************************************************************************/

#ifndef C2C_BENCHMARK_H_
#define C2C_BENCHMARK_H_

#define MUGGLE_HOLD_LOG_MACRO 1
#include "muggle/c/muggle_c.h"
#include <assert.h>

EXTERN_C_BEGIN


typedef union {
	char placeholder[64];
	struct {
		muggle_time_counter_t tc;
	};
} cache_line_data_t;
static_assert(sizeof(cache_line_data_t) <= 64, "cache line data need <= 64");

/**
 * @brief generate report
 *
 * @param name           benchmark name
 * @param producer_core  producer bind core
 * @param consumer_core  consumer bind core
 * @param tc_arr         time counter array
 * @param total_cnt      total count
 * @param is_rtt         is rtt
 *
 * @RETURN middle value of 1/2 rtt elapsed
 */
int64_t c2c_benchmark_gen_report(const char *name, int32_t producer_core,
							   int32_t consumer_core,
							   cache_line_data_t *datas, size_t total_cnt,
							   int32_t is_rtt);

/**
 * @brief bind core
 *
 * @param core  core number
 *
 * @return
 *     0 - success
 *     otherwise - errno
 */
int c2c_benchmark_bind_core(int32_t core);

/**
 * @brief warmup
 *
 * @param ms  milliseconds
 */
void c2c_benchmark_warmup(int32_t ms);

/**
 * @brief wait nanoseconds
 *
 * @param ns  nanoseconds
 */
void c2c_benchmark_wait_ns(int32_t ns);

EXTERN_C_END

#endif // !C2C_BENCHMARK_H_
