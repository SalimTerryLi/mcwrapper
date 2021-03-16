//
// Created by salimterryli on 2021/3/15.
//

#ifndef MCWRAPPER_PERF_MONITOR_H
#define MCWRAPPER_PERF_MONITOR_H

#include <unistd.h>

int start_perf_monitor(pid_t pid);
void stop_perf_monitor();
float get_cpu_usage_norm();
long get_cpu_cores();
long get_mem_usage_m();

#endif//MCWRAPPER_PERF_MONITOR_H
