//
// Created by salimterryli on 2021/3/15.
//

#include "perf_monitor.h"
#include "utils.h"

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>

pthread_t perfmon_thread = {};
int notify_pipe[2] = {-1, -1};// poll based timer + exiting notify

FILE *proc_stat_fd = nullptr;
FILE *proc_pid_stat_fd = nullptr;

uint64_t last_cpu_sum_time = 0;
uint64_t last_cpu_pid_time = 0;

float cpu_usage = 0.0f;
uint64_t mem_size_mb = 0;
pthread_mutex_t perfload_mutex = {};

void *perfmon_thread_func(void *arg);

void update_perf();

int start_perf_monitor(pid_t pid) {
	int ret = 0;
	ret = pipe2(notify_pipe, O_NONBLOCK);
	if (ret != 0) {
		eprintf("pipe2(notify_pipe) error: %d\n", errno);
		goto exit_notify_pipe;
	}
	ret = pthread_mutex_init(&perfload_mutex, nullptr);
	if (ret != 0) {
		eprintf("pthread_mutex_init(perfload_mutex) error: %d\n", errno);
		goto exit_perfload_mutex;
	}
	proc_stat_fd = fopen("/proc/stat", "r");
	if (proc_stat_fd == nullptr) {
		eprintf("fopen(proc_stat_fd) error: %d\n", errno);
		goto exit_proc_stat_fd;
	}
	char pathbuf[32];
	sprintf(pathbuf, "/proc/%d/stat", pid);
	proc_pid_stat_fd = fopen(pathbuf, "r");
	if (proc_pid_stat_fd == nullptr) {
		eprintf("fopen(proc_pid_stat_fd) error: %d\n", errno);
		goto exit_proc_pid_stat_fd;
	}
	ret = pthread_create(&perfmon_thread, nullptr, &perfmon_thread_func, nullptr);
	if (ret != 0) {
		eprintf("perfmon_thread start failed: %d\n", ret);
		goto exit_perfmon_thread;
	}
	return ret;
exit_perfmon_thread:
	fclose(proc_pid_stat_fd);
exit_proc_pid_stat_fd:
	fclose(proc_stat_fd);
exit_proc_stat_fd:
	pthread_mutex_destroy(&perfload_mutex);
exit_perfload_mutex:
	close(notify_pipe[1]);
	close(notify_pipe[0]);
exit_notify_pipe:
	notify_pipe[0] = -1;
	return ret;
}

void stop_perf_monitor() {
	if (notify_pipe[0] != -1) {// if not, thread is not running
		close(notify_pipe[1]); // only close write end
		pthread_join(perfmon_thread, nullptr);
		fclose(proc_stat_fd);
		fclose(proc_pid_stat_fd);
		pthread_mutex_destroy(&perfload_mutex);
		printf("perf monitor stopped\n");
	}
}

float get_cpu_usage_norm() {
	float ret = 0.0f;
	pthread_mutex_lock(&perfload_mutex);
	ret = cpu_usage;
	pthread_mutex_unlock(&perfload_mutex);
	return ret;
}

long get_cpu_cores() {
	return sysconf(_SC_NPROCESSORS_CONF);
}

long get_mem_usage_m() {
	uint64_t ret = 0;
	pthread_mutex_lock(&perfload_mutex);
	ret = mem_size_mb;
	pthread_mutex_unlock(&perfload_mutex);
	return ret;
}

void *perfmon_thread_func(void *arg) {
	struct pollfd fd[1] = {};
	fd->fd = notify_pipe[0];
	fd->events = POLLIN;
	while (true) {
		int pollret = poll(fd, 1, 2000);// 2s timeout
		if (pollret == -1) {
			eprintf("perfmon_thread_func poll failed: %d\n", errno);
			break;
		} else if (pollret == 0) {// timeout, do update
			update_perf();
		} else {
			if (fd->revents & POLLHUP) {// parent closed write end
				break;
			}
		}
	}
	close(notify_pipe[0]);
	pthread_exit(nullptr);
}

void update_perf() {
	char tempbuf[64];
	uint64_t raw_cpu_user, raw_cpu_nice, raw_cpu_system, raw_cpu_idle;
	if (fscanf(proc_stat_fd, "cpu %ld %ld %ld %ld", &raw_cpu_user, &raw_cpu_nice, &raw_cpu_system, &raw_cpu_idle) != 4) {
		eprintf("proc_stat_fd err\n");
	}
	while (fgets(tempbuf, sizeof(tempbuf), proc_stat_fd) != nullptr)
		;// dump out all data
	fseek(proc_stat_fd, 0, SEEK_SET);

	while (fgetc(proc_pid_stat_fd) != ')')
		;
	for (int i = 0; i < 12; ++i)
		while (fgetc(proc_pid_stat_fd) != ' ')
			;// skipping
	uint64_t raw_proc_pid_utime, raw_proc_pid_stime, raw_proc_pid_cutime, raw_proc_pid_cstime;
	if (fscanf(proc_pid_stat_fd, "%lu %lu %lu %lu",
	           &raw_proc_pid_utime, &raw_proc_pid_stime, &raw_proc_pid_cutime, &raw_proc_pid_cstime) != 4) {
		eprintf("proc_pid_stat_fd err\n");
	}

	for (int i = 0; i < 6; ++i)
		while (fgetc(proc_pid_stat_fd) != ' ')
			;// skipping
	uint64_t raw_mem_byte;
	if (fscanf(proc_pid_stat_fd, "%lu", &raw_mem_byte) != 1) {
		eprintf("proc_pid_stat_fd err\n");
	}
	while (fgets(tempbuf, sizeof(tempbuf), proc_pid_stat_fd) != nullptr)
		;// dump out all data
	fseek(proc_pid_stat_fd, 0, SEEK_SET);

	uint64_t cpu_sum_time = raw_cpu_user + raw_cpu_nice + raw_cpu_system + raw_cpu_idle;
	uint64_t cpu_pid_time = raw_proc_pid_utime + raw_proc_pid_stime + raw_proc_pid_cutime + raw_proc_pid_cstime;

	if (last_cpu_sum_time != 0) {
		pthread_mutex_lock(&perfload_mutex);
		cpu_usage = 1.0f * (cpu_pid_time - last_cpu_pid_time) / (cpu_sum_time - last_cpu_sum_time) * sysconf(_SC_NPROCESSORS_CONF);
		mem_size_mb = raw_mem_byte / 1024 / 1024;
		pthread_mutex_unlock(&perfload_mutex);
	}
	last_cpu_sum_time = cpu_sum_time;
	last_cpu_pid_time = cpu_pid_time;
}