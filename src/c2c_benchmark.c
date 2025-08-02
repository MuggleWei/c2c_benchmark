#include "c2c_benchmark.h"

static void write_statistics_head(FILE *fp)
{
	for (int i = 0; i < 100; i += 10) {
		fprintf(fp, "%d,", i);
	}
	fprintf(fp, "100\n");
}
static void write_statistics(FILE *fp, int64_t *elapseds, int n)
{
	for (int i = 0; i < 100; i += 10) {
		uint64_t idx = (uint64_t)((i / 100.0) * n);
		fprintf(fp, "%llu,", (unsigned long long)elapseds[idx]);
	}
	fprintf(fp, "%llu\n", (unsigned long long)elapseds[n - 1]);
}
static int compare_int64(const void *a, const void *b)
{
	const int64_t arg1 = *(const int64_t *)a;
	const int64_t arg2 = *(const int64_t *)b;

	if (arg1 < arg2)
		return -1;
	if (arg1 > arg2)
		return 1;
	return 0;
}

int64_t c2c_benchmark_gen_report(const char *name, int32_t producer_core,
								 int32_t consumer_core,
								 cache_line_data_t *datas, size_t total_cnt,
								 int32_t is_rtt)
{
	// get 1/2 rtt c2c elapsed
	int64_t *elapseds = (int64_t *)malloc(sizeof(int64_t) * total_cnt);
	for (size_t i = 0; i < total_cnt; ++i) {
		elapseds[i] = muggle_time_counter_interval_ns(&datas[i].tc);
		if (is_rtt) {
			elapseds[i] /= 2;
		}
	}

	// dump records
#if MUGGLE_PLATFORM_WINDOWS
	// windows ignore dumpo records
#else
	char records_filepath[MUGGLE_MAX_PATH];
	snprintf(records_filepath, sizeof(records_filepath),
			 "./c2c_benchmark_reports/record_%s_c%d_to_c%d.csv", name,
			 producer_core, consumer_core);
	FILE *fp = muggle_os_fopen(records_filepath, "w");
	fprintf(fp, "idx,start,end,elapsed\n");
	for (size_t i = 0; i < total_cnt; ++i) {
		muggle_time_counter_t *data = &datas[i].tc;
		fprintf(fp, "%lld,%lld.%09lld,%lld.%09lld,%lld\n", (long long)i,
				(long long)data->start_ts.tv_sec,
				(long long)data->start_ts.tv_nsec,
				(long long)data->end_ts.tv_sec, (long long)data->end_ts.tv_nsec,
				(long long)elapseds[i]);
	}
	fclose(fp);
	LOG_INFO("generate records report: %s", records_filepath);
#endif

	char statistics_filepath[MUGGLE_MAX_PATH];
	snprintf(statistics_filepath, sizeof(statistics_filepath),
			 "./c2c_benchmark_reports/statistics_%s_c%d_to_c%d.csv", name,
			 producer_core, consumer_core);
	fp = muggle_os_fopen(statistics_filepath, "w");
	fprintf(fp, "sort_by,");
	write_statistics_head(fp);
	fprintf(fp, "idx,");
	write_statistics(fp, elapseds, total_cnt);
	qsort(elapseds, total_cnt, sizeof(int64_t), compare_int64);
	fprintf(fp, "elapsed,");
	write_statistics(fp, elapseds, total_cnt);
	fclose(fp);
	LOG_INFO("generate statistics report: %s", statistics_filepath);

	int64_t middle_val = elapseds[total_cnt / 2];
	free(elapseds);

	return middle_val;
}

int c2c_benchmark_bind_core(int32_t core)
{
	muggle_cpu_mask_t mask;
	muggle_cpu_mask_zero(&mask);
	muggle_cpu_mask_set(&mask, core);
	return muggle_cpu_set_thread_affinity(0, &mask);
}

void c2c_benchmark_warmup(int32_t ms)
{
	muggle_time_counter_t warmup_tc;
	muggle_time_counter_init(&warmup_tc);
	muggle_time_counter_start(&warmup_tc);
	while (1) {
		muggle_time_counter_end(&warmup_tc);
		if (muggle_time_counter_interval_ms(&warmup_tc) > ms) {
			break;
		}
	}
}
