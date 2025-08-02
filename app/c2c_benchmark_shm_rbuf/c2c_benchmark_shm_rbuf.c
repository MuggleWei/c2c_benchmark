#include "c2c_benchmark.h"

typedef struct {
	int32_t rounds;
	int32_t record_per_round;
	int32_t round_interval_ns;
	int32_t producer_core;
	int32_t consumer_core;
} args_t;

typedef struct {
	args_t *sys_args;
	muggle_shm_ringbuf_t *shm_rbuf;
	cache_line_data_t *datas;
} thread_args_t;

void parse_args(int argc, char **argv, args_t *args)
{
	memset(args, 0, sizeof(*args));
	args->rounds = 1000;
	args->record_per_round = 1;
	args->round_interval_ns = 1000;
	args->producer_core = -1;
	args->consumer_core = -1;

	int opt;
	while ((opt = getopt(argc, argv, "r:m:i:p:c:h")) != -1) {
		switch (opt) {
		case 'r': {
			args->rounds = atoi(optarg);
		} break;
		case 'm': {
			args->record_per_round = atoi(optarg);
		} break;
		case 'i': {
			args->round_interval_ns = atoi(optarg);
		} break;
		case 'p': {
			args->producer_core = atoi(optarg);
		} break;
		case 'c': {
			args->consumer_core = atoi(optarg);
		} break;
		case 'h': {
			printf("Usage of %s:\n"
				   "  -r int\n"
				   "    rounds\n"
				   "  -m int\n"
				   "    record per round\n"
				   "  -i int\n"
				   "    round interval (nanoseconds)\n"
				   "  -p int\n"
				   "    producer bind core\n"
				   "  -c int\n"
				   "    consumer bind core\n"
				   "",
				   argv[0]);
			exit(EXIT_SUCCESS);
		} break;
		}
	}
}

muggle_shm_ringbuf_t *init_shm_ringbuf(muggle_shm_t *shm)
{
	const char *k_name = "/dev/shm/benchmark_c2c_benchmark";
	const int k_num = 5;
#if MUGGLE_PLATFORM_WINDOWS
#else
	if (!muggle_path_exists(k_name)) {
		FILE *fp = muggle_os_fopen(k_name, "w");
		if (fp == NULL) {
			LOG_ERROR("failed open k_name: %s", k_name);
			exit(EXIT_FAILURE);
		}
		fclose(fp);
	}
#endif

	uint32_t n_bytes = 4 * 1024 * 1024;
	int flag = MUGGLE_SHM_FLAG_CREAT;
	muggle_shm_ringbuf_t *shm_rbuf =
		muggle_shm_ringbuf_open(shm, k_name, k_num, flag, n_bytes);
	if (shm_rbuf == NULL) {
		LOG_ERROR("failed create shm_ringbuf");
		return NULL;
	}

	LOG_INFO("success create shm_ringbuf: %s, %d", k_name, k_num);
	LOG_INFO("shm_ringbuf.n_cacheline: %u", shm_rbuf->n_cacheline);

	return shm_rbuf;
}

void clear_shm_ringbuf(muggle_shm_t *shm)
{
	if (muggle_shm_detach(shm) != 0) {
		LOG_ERROR("failed shm detach");
		return;
	}
	LOG_INFO("success shm detach");

	if (muggle_shm_rm(shm) != 0) {
		LOG_ERROR("failed shm remove");
		return;
	}
	LOG_INFO("success shm remove");
}

muggle_thread_ret_t proc_consumer(void *p)
{
	thread_args_t *p_args = (thread_args_t *)p;
	args_t *args = p_args->sys_args;
	muggle_shm_ringbuf_t *shm_rbuf = p_args->shm_rbuf;
	cache_line_data_t *datas = p_args->datas;

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
	LOG_INFO("run consumer");
	size_t total_cnt = (size_t)args->rounds * (size_t)args->record_per_round;
	size_t n = 0;
	while (1) {
		uint32_t n_bytes = 0;
		cache_line_data_t *ptr =
			(cache_line_data_t *)muggle_shm_ringbuf_r_fetch(shm_rbuf, &n_bytes);
		if (ptr) {
			muggle_time_counter_end(&ptr->tc);
			memcpy(&datas[n], ptr, sizeof(*ptr));

			muggle_shm_ringbuf_r_move(shm_rbuf);

			if (++n == total_cnt) {
				break;
			}
		}
	}
	LOG_INFO("consumer completed");

	return 0;
}

void proc_producer(thread_args_t *p_args)
{
	args_t *args = p_args->sys_args;
	muggle_shm_ringbuf_t *shm_rbuf = p_args->shm_rbuf;

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
	LOG_INFO("run producer");
	for (int r = 0; r < args->rounds; ++r) {
		for (int i = 0; i < args->record_per_round; ++i) {
			cache_line_data_t *ptr = muggle_shm_ringbuf_w_alloc_bytes(
				shm_rbuf, sizeof(cache_line_data_t));
			if (ptr == NULL) {
				LOG_ERROR("failed alloc bytes for write");
				muggle_msleep(1000);
				--i;
				continue;
			}

			muggle_time_counter_init(&ptr->tc);
			muggle_time_counter_start(&ptr->tc);
			muggle_shm_ringbuf_w_move(shm_rbuf);
		}

		c2c_benchmark_wait_ns(args->round_interval_ns);
	}
	LOG_INFO("producer completed");
}

int64_t run_shm_rbuf(args_t *args)
{
	// prepare datas
	size_t total_cnt = (size_t)args->rounds * (size_t)args->record_per_round;
	cache_line_data_t *datas =
		(cache_line_data_t *)malloc(sizeof(cache_line_data_t) * total_cnt);
	if (datas == NULL) {
		return -1;
	}

	// init share ring buffer
	muggle_shm_t shm;
	muggle_shm_ringbuf_t *shm_rbuf = init_shm_ringbuf(&shm);
	if (shm_rbuf == NULL) {
		return -1;
	}

	thread_args_t th_args;
	th_args.sys_args = args;
	th_args.shm_rbuf = shm_rbuf;
	th_args.datas = datas;

	// run consumer
	muggle_thread_t th_consumer;
	muggle_thread_create(&th_consumer, proc_consumer, &th_args);

	// run producer
	LOG_INFO("wait consumer run");
	muggle_msleep(5);
	proc_producer(&th_args);

	// cleanup thread
	muggle_thread_join(&th_consumer);

	// cleanup share ring buffer
	clear_shm_ringbuf(&shm);

	// output report
	int64_t middle_val =
		c2c_benchmark_gen_report("shm_rbuf", args->producer_core,
								 args->consumer_core, datas, total_cnt, 0);

	free(datas);
	return middle_val;
}

int main(int argc, char *argv[])
{
	// initialize log
	if (muggle_log_complicated_init(MUGGLE_LOG_LEVEL_INFO,
									MUGGLE_LOG_LEVEL_INFO,
									"logs/c2c_benchmark_shm_rbuf.log") != 0) {
		fprintf(stderr, "failed init log\n");
		exit(EXIT_FAILURE);
	}

	args_t args;
	parse_args(argc, argv, &args);
	LOG_INFO("----------------");
	LOG_INFO("rounds: %d", args.rounds);
	LOG_INFO("record_per_round: %d", args.record_per_round);
	LOG_INFO("round_interval_ns: %d", args.round_interval_ns);
	LOG_INFO("producer_core: %d", args.producer_core);
	LOG_INFO("consumer_core: %d", args.consumer_core);
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
				int64_t middle_val = run_shm_rbuf(&args);
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
		int64_t middle_val = run_shm_rbuf(&args);
		fprintf(stdout, "%d -> %d: %lld\n", args.producer_core,
				args.consumer_core, (long long)middle_val);
	}

	return 0;
}
