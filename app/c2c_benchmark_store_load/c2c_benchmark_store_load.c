#include "c2c_benchmark.h"

typedef struct {
	union {
		MUGGLE_STRUCT_CACHE_LINE_PADDING(0);
		struct {
			int32_t producer_core;
			int32_t consumer_core;
			int32_t total_cnt;
			int32_t n_samples;
		};
	};
	union {
		MUGGLE_STRUCT_CACHE_LINE_PADDING(1);
		muggle_atomic_int v1;
	};
	union {
		MUGGLE_STRUCT_CACHE_LINE_PADDING(2);
		muggle_atomic_int v2;
	};
} args_t;

void parse_args(int argc, char **argv, args_t *args)
{
	memset(args, 0, sizeof(*args));
	args->producer_core = -1;
	args->consumer_core = -1;
	args->total_cnt = 10000;
	args->n_samples = 1;

	int opt;
	while ((opt = getopt(argc, argv, "p:c:n:s:h")) != -1) {
		switch (opt) {
		case 'p': {
			args->producer_core = atoi(optarg);
		} break;
		case 'c': {
			args->consumer_core = atoi(optarg);
		} break;
		case 'n': {
			args->total_cnt = atoi(optarg);
		} break;
		case 's': {
			args->n_samples = atoi(optarg);
			if (args->n_samples < 1) {
				args->n_samples = 1;
			}
		} break;
		case 'h': {
			printf("Usage of %s:\n"
				   "  -p int\n"
				   "    producer bind core\n"
				   "  -c int\n"
				   "    consumer bind core\n"
				   "  -n int\n"
				   "    total count\n"
				   "  -s int\n"
				   "    number of samples per round"
				   "",
				   argv[0]);
			exit(EXIT_SUCCESS);
		} break;
		}
	}
}

muggle_thread_ret_t proc_consumer(void *p)
{
	args_t *args = (args_t *)p;

	// bind core
	int ret = c2c_benchmark_bind_core(args->consumer_core);
	if (ret != 0) {
		char errmsg[256];
		muggle_sys_strerror(ret, errmsg, sizeof(errmsg));
		LOG_ERROR("failed consumer bind CPU core, err=%s", errmsg);
	} else {
		LOG_INFO("consumer bind CPU core #%d", args->consumer_core);
	}

	// warmup
	c2c_benchmark_warmup(2);

	// run consumer
	if (args->n_samples == 1) {
		for (int32_t i = 0; i < args->total_cnt; ++i) {
			while (muggle_atomic_load(&args->v1, muggle_memory_order_acquire) !=
				   i)
				;
			muggle_atomic_store(&args->v2, i, muggle_memory_order_release);
		}
	} else {
		for (int32_t i = 0; i < args->total_cnt; ++i) {
			for (int32_t n = 0; n < args->n_samples; ++n) {
				while (muggle_atomic_load(&args->v1,
										  muggle_memory_order_acquire) != n)
					;
				muggle_atomic_store(&args->v2, n, muggle_memory_order_release);
			}
		}
	}

	return 0;
}

void proc_producer(args_t *args, cache_line_data_t *datas)
{
	// bind core
	int ret = c2c_benchmark_bind_core(args->producer_core);
	if (ret != 0) {
		char errmsg[256];
		muggle_sys_strerror(ret, errmsg, sizeof(errmsg));
		LOG_ERROR("failed producer bind CPU core, err=%s", errmsg);
	} else {
		LOG_INFO("producer bind CPU core #%d", args->producer_core);
	}

	// warmup
	c2c_benchmark_warmup(2);

	// run producer
	if (args->n_samples == 1) {
		for (int32_t i = 0; i < args->total_cnt; ++i) {
			muggle_time_counter_start(&datas[i].tc);
			muggle_atomic_store(&args->v1, i, muggle_memory_order_release);
			while (muggle_atomic_load(&args->v2, muggle_memory_order_acquire) !=
				   i)
				;
			muggle_time_counter_end(&datas[i].tc);
		}
	} else {
		for (int32_t i = 0; i < args->total_cnt; ++i) {
			muggle_time_counter_start(&datas[i].tc);
			for (int32_t n = 0; n < args->n_samples; ++n) {
				muggle_atomic_store(&args->v1, n, muggle_memory_order_release);
				while (muggle_atomic_load(&args->v2,
										  muggle_memory_order_acquire) != n)
					;
			}
			muggle_time_counter_end(&datas[i].tc);
		}
	}
}

int64_t run_store_load(args_t *args)
{
	// prepare datas
	args->v1 = -1;
	args->v2 = -1;

	cache_line_data_t *datas = (cache_line_data_t *)malloc(
		sizeof(cache_line_data_t) * args->total_cnt);
	if (datas == NULL) {
		return -1;
	}
	for (int32_t i = 0; i < args->total_cnt; ++i) {
		muggle_time_counter_init(&datas[i].tc);
	}

	// run consumer
	muggle_thread_t th_consumer;
	muggle_thread_create(&th_consumer, proc_consumer, args);

	// run producer
	proc_producer(args, datas);

	// cleanup consumer
	muggle_thread_join(&th_consumer);

	int64_t middle_val = c2c_benchmark_gen_report("store_load",
												  args->producer_core,
												  args->consumer_core, datas,
												  args->total_cnt, 1);
	free(datas);
	return middle_val / args->n_samples;
}

int main(int argc, char *argv[])
{
	// initialize log
	if (muggle_log_complicated_init(MUGGLE_LOG_LEVEL_INFO,
									MUGGLE_LOG_LEVEL_INFO,
									"logs/c2c_benchmark_store_load.log") != 0) {
		fprintf(stderr, "failed init log\n");
		exit(EXIT_FAILURE);
	}

	args_t args;
	parse_args(argc, argv, &args);
	LOG_INFO("----------------");
	LOG_INFO("producer_core: %d", args.producer_core);
	LOG_INFO("consumer_core: %d", args.consumer_core);
	LOG_INFO("total_cnt: %d", args.total_cnt);
	LOG_INFO("----------------");

	if (args.producer_core == -1 || args.producer_core == -1) {
		long num_cores = sysconf(_SC_NPROCESSORS_ONLN);
		if (num_cores == -1) {
			LOG_ERROR("failed get core numbers: %d", MUGGLE_EVENT_LAST_ERRNO);
			exit(EXIT_FAILURE);
		}

		int64_t *arr =
			(int64_t *)malloc(sizeof(int64_t) * num_cores * num_cores);
		memset(arr, 0, sizeof(int64_t) * num_cores * num_cores);

		for (int i = 0; i < num_cores; ++i) {
			for (int j = i + 1; j < num_cores; ++j) {
				args.producer_core = i;
				args.consumer_core = j;
				int64_t middle_val = run_store_load(&args);
				arr[num_cores * i + j] = middle_val;
				arr[num_cores * j + i] = middle_val;
			}
		}

		fprintf(stdout, "      ");
		for (int i = 0; i < num_cores; ++i) {
			fprintf(stdout, "%6d", i);
		}
		fprintf(stdout, "\n");
		for (int i = 0; i < num_cores; ++i) {
			fprintf(stdout, "%6d", i);
			for (int j = 0; j < num_cores; ++j) {
				fprintf(stdout, "%6lld", (long long)arr[num_cores * i + j]);
			}
			fprintf(stdout, "\n");
		}
	} else {
		int64_t middle_val = run_store_load(&args);
		fprintf(stdout, "%d -> %d: %lld\n", args.producer_core,
				args.consumer_core, (long long)middle_val);
	}

	return 0;
}
