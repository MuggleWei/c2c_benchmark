#include "c2c_benchmark.h"

#define MAX_N_PRODUCER 32

typedef struct {
	int32_t rounds;
	int32_t record_per_round;
	int32_t round_interval_ns;
	int32_t n_producer;
	int32_t producer_cores[MAX_N_PRODUCER];
	int32_t consumer_core;
	int32_t measure_wr;
} args_t;

typedef struct {
	int32_t idx;
	args_t *sys_args;
	muggle_channel_t *chan;
	cache_line_data_t *datas;
} thread_args_t;

void parse_args(int argc, char **argv, args_t *args)
{
	memset(args, 0, sizeof(*args));
	args->rounds = 1000;
	args->record_per_round = 1;
	args->round_interval_ns = 1000;
	args->n_producer = 0;
	for (int i = 0; i < MAX_N_PRODUCER; ++i) {
		args->producer_cores[i] = -1;
	}
	args->consumer_core = -1;
	args->measure_wr = 1;

	int opt;
	while ((opt = getopt(argc, argv, "r:m:i:p:c:t:h")) != -1) {
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
			char *token;
			token = strtok(optarg, ","); // Get the first token

			while (token != NULL) {
				args->producer_cores[args->n_producer++] = atoi(token);
				token = strtok(NULL, ","); // Get the next token
				if (args->n_producer >= MAX_N_PRODUCER) {
					break;
				}
			}
		} break;
		case 'c': {
			args->consumer_core = atoi(optarg);
		} break;
		case 't': {
			if (strcmp(optarg, "w") == 0) {
				args->measure_wr = 0;
			} else if (strcmp(optarg, "wr") == 0) {
				args->measure_wr = 1;
			} else {
				LOG_ERROR("invalid measure type");
			}
		} break;
		case 'h': {
			printf("Usage of %s:\n"
				   "  -r int\n"
				   "    rounds\n"
				   "  -m int\n"
				   "    record per round\n"
				   "  -i int\n"
				   "    round interval (nanoseconds)\n"
				   "  -p int array split with comma\n"
				   "    producer bind cores\n"
				   "  -c int\n"
				   "    consumer bind core\n"
				   "  -t string\n"
				   "    measure type; 'w' or 'wr'\n"
				   "\n"
				   "e.g.\n"
				   "  %s -p 0,1,2,3 -c 4\n"
				   "",
				   argv[0], argv[0]);
			exit(EXIT_SUCCESS);
		} break;
		}
	}
}

muggle_thread_ret_t proc_producer(void *p)
{
	thread_args_t *p_args = (thread_args_t *)p;
	args_t *args = p_args->sys_args;
	muggle_channel_t *chan = p_args->chan;
	int32_t bind_core = args->producer_cores[p_args->idx];

	// wait consumer launch
	muggle_msleep(500);

	// bind core
	int ret = c2c_benchmark_bind_core(bind_core);
	if (ret != 0) {
		char errmsg[256];
		muggle_sys_strerror(ret, errmsg, sizeof(errmsg));
		LOG_ERROR("failed producer bind CPU core, err=%s", errmsg);
	} else {
		LOG_INFO("producer bind CPU core #%d", bind_core);
	}

	// warmup
	c2c_benchmark_warmup(2);

	// run producer
	LOG_INFO("run producer %d", p_args->idx);
	cache_line_data_t *data = p_args->datas;

	if (args->measure_wr == 1) {
		// measure w start -> r end
		for (int r = 0; r < args->rounds; ++r) {
			for (int i = 0; i < args->record_per_round; ++i) {
				do {
					muggle_time_counter_init(&data->tc);
					muggle_time_counter_start(&data->tc);
					if (muggle_channel_write(chan, data) == 0) {
						break;
					}
				} while (1);
				data += 1;
			}

			c2c_benchmark_wait_ns(args->round_interval_ns);
		}
	} else {
		// measure w start -> w end
		for (int r = 0; r < args->rounds; ++r) {
			for (int i = 0; i < args->record_per_round; ++i) {
				do {
					muggle_time_counter_init(&data->tc);
					muggle_time_counter_start(&data->tc);
					if (muggle_channel_write(chan, data) == 0) {
						muggle_time_counter_end(&data->tc);
						break;
					}
				} while (1);
				data += 1;
			}

			c2c_benchmark_wait_ns(args->round_interval_ns);
		}
	}
	LOG_INFO("producer %d completed", p_args->idx);

	return 0;
}

void proc_consumer(args_t *args, muggle_channel_t *chan)
{
	size_t total_cnt = (size_t)args->rounds * (size_t)args->record_per_round *
					   (size_t)args->n_producer;

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
	size_t rcv_cnt = 0;
	if (args->measure_wr) {
		// measure w start -> r end
		while (true) {
			cache_line_data_t *data =
				(cache_line_data_t *)muggle_channel_read(chan);
			if (data) {
				muggle_time_counter_end(&data->tc);
				if (++rcv_cnt == total_cnt) {
					break;
				}
			}
		}
	} else {
		// measure w start -> w end
		while (true) {
			cache_line_data_t *data =
				(cache_line_data_t *)muggle_channel_read(chan);
			if (data) {
				if (++rcv_cnt == total_cnt) {
					break;
				}
			}
		}
	}
	LOG_INFO("consumer completed");
}

void run_chan(args_t *args)
{
	// prepare datas
	size_t total_cnt = (size_t)args->rounds * (size_t)args->record_per_round *
					   (size_t)args->n_producer;
	cache_line_data_t *datas =
		(cache_line_data_t *)malloc(sizeof(cache_line_data_t) * total_cnt);
	if (datas == NULL) {
		LOG_ERROR("failed allocate datas");
		return;
	}

	// init channel
	muggle_channel_t chan;
	int flags = MUGGLE_CHANNEL_FLAG_WRITE_SPIN | MUGGLE_CHANNEL_FLAG_READ_BUSY;
	if (muggle_channel_init(&chan, 1024 * 16, flags) != 0) {
		LOG_ERROR("failed init channel");
		return;
	}

	// run producer
	thread_args_t th_args[MAX_N_PRODUCER];
	muggle_thread_t th_producer[MAX_N_PRODUCER];
	for (int32_t i = 0; i < args->n_producer; ++i) {
		th_args[i].idx = i;
		th_args[i].sys_args = args;
		th_args[i].chan = &chan;
		th_args[i].datas =
			datas + i * (size_t)args->rounds * (size_t)args->record_per_round;
		muggle_thread_create(&th_producer[i], proc_producer, &th_args[i]);
	}

	// run consumer
	proc_consumer(args, &chan);

	// cleanup producer
	for (int32_t i = 0; i < args->n_producer; ++i) {
		muggle_thread_join(&th_producer[i]);
	}

	// cleanup channel
	muggle_channel_destroy(&chan);

	// output report
	char name[128];
	snprintf(name, sizeof(name), "chan_%s", args->measure_wr ? "wr" : "w");
	c2c_benchmark_gen_report(name, args->n_producer, args->consumer_core, datas,
							 total_cnt, 0);

	// cleanup datas
	free(datas);
}

int main(int argc, char *argv[])
{
	// initialize log
	if (muggle_log_complicated_init(MUGGLE_LOG_LEVEL_INFO,
									MUGGLE_LOG_LEVEL_INFO,
									"logs/c2c_benchmark_chan.log") != 0) {
		fprintf(stderr, "failed init log\n");
		exit(EXIT_FAILURE);
	}

	args_t args;
	parse_args(argc, argv, &args);
	LOG_INFO("----------------");
	LOG_INFO("rounds: %d", args.rounds);
	LOG_INFO("record_per_round: %d", args.record_per_round);
	LOG_INFO("round_interval_ns: %d", args.round_interval_ns);
	LOG_INFO("n_producer: %d", args.n_producer);
	for (int32_t i = 0; i < args.n_producer; ++i) {
		LOG_INFO("producer_core[%d]: %d", i, args.producer_cores[i]);
	}
	LOG_INFO("consumer_core: %d", args.consumer_core);
	LOG_INFO("measure type: %s",
			 args.measure_wr ? "w start -> r end" : "w start -> w end");
	LOG_INFO("----------------");

	if (args.n_producer == 0) {
		LOG_ERROR("run without producer");
		exit(EXIT_FAILURE);
	}

	run_chan(&args);

	return 0;
}
