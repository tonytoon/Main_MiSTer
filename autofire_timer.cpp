#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <sys/timerfd.h>
#include <sys/poll.h>
#include <unistd.h>
#include <math.h>

#include "autofire_timer.h"
#include "video.h"

#define BENCHMARK_AUTOFIRE 1

/************* AUTOFIRE TIMER STUFF ********************/
// autofire timing
// use vtime rather than assuming 16.67ms per frame
// in theory this will reduce drift over time since most games
// don't run at exactly 60hz but we're still at the mercy of
// cpu timers so don't expect magic

// global
uint64_t new_frame;

extern VideoInfo *pcurrent_video_info; // from video.cpp
static bool timer_started = false;
static int vtimerfd = -1;
static uint64_t vtimer_start_ns;

// clamp calculated vrefresh to 30-90hz to avoid obviously garbage values
inline uint32_t get_vtime() {
	uint32_t current_vtime = pcurrent_video_info->vtime;
	if (current_vtime <= THIRTYHERTZ && current_vtime >= NINETYHERTZ) {
		return current_vtime;
	}
	else {
		return SIXTYHERTZ;
	}
}

// return true if a core's vtime has changed
// this might happen if resolution changes or some cores
// allow the user to adjust vrefresh for display compatibility

bool vtime_changed()
{
	static uint32_t prev_vtime;
	uint32_t current_vtime = get_vtime();
	if (prev_vtime != current_vtime) {
		if (vtimerfd >= 0) {
			close(vtimerfd);	// recycle timerfd
			vtimerfd = -1;
		}
		prev_vtime = current_vtime;
		return true;
	}
	return false;
}

// initialize timerfd based timer
// return 1 on failure
int start_vtimer(uint64_t interval_ns, uint64_t &vtimer_start_ns) {
	vtimerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (vtimerfd < 0) {
        perror("timerfd_create");
        return 1;
    };

    // get current time to start absolute time
	// not to be confused with absolute batman
    struct timespec now_ts;
    clock_gettime(CLOCK_MONOTONIC, &now_ts);
	
	uint64_t now_ns = (uint64_t)now_ts.tv_sec * 1000000000ULL + now_ts.tv_nsec;
	uint64_t first_expiration_ns = now_ns + interval_ns;

	struct itimerspec its = {
		.it_interval = { .tv_sec = 0, .tv_nsec = 0 },
		.it_value    = { .tv_sec = 0, .tv_nsec = 0 }
	};

    // first expiration = now + interval
    its.it_value = now_ts;
    its.it_value.tv_nsec += interval_ns;
    if (its.it_value.tv_nsec >= 1000000000L) {
        its.it_value.tv_nsec -= 1000000000L;
        its.it_value.tv_sec++;
    }

    // future expirations
    its.it_interval.tv_sec  = 0;
    its.it_interval.tv_nsec = interval_ns;

    // absolute time
    if (timerfd_settime(vtimerfd, TFD_TIMER_ABSTIME, &its, NULL) < 0) {
        perror("timerfd_settime");
		printf("interval_ns: %llu\n", interval_ns);
        return 1;
    }
	vtimer_start_ns = first_expiration_ns;
	float hz = 1e9f / interval_ns;
    printf("%.2fhz timer started.\n", hz);
	return 0;
}

// attempt to start timerfd based timer
// returns false on failure or if vrefresh appears to be 0
bool init_timer(uint32_t interval)
{
	uint64_t interval_ns = interval * 10ull;
		if (!timer_started && interval_ns) {
			if (start_vtimer(interval_ns, vtimer_start_ns) == 0)
				return true;
			else
				return false;
		}
	return false;
}

// check if timer has fired yet or not.
// return nanoseconds since timer expired
// return zero otherwise
uint64_t check_vtimer(uint64_t interval_ns) {
	uint64_t late = 0;
	uint64_t expirations;
	struct timespec now;

	struct pollfd pfd = { vtimerfd, POLLIN, 0 };

	if (poll(&pfd, 1, 0) <= 0)
		return 0; // timer not ready

	read(vtimerfd, &expirations, sizeof(expirations));

	clock_gettime(CLOCK_MONOTONIC, &now);
	uint64_t now_ns = (uint64_t)now.tv_sec * 1000000000ULL + now.tv_nsec;

	// Track expected timing
	static uint64_t tick_count = 0;
	static uint64_t start_ns = 0;

	if (tick_count == 0) {
		// First activation establishes start time
		start_ns = vtimer_start_ns;
	}
	
	uint64_t expected_ns = start_ns + tick_count * interval_ns;
	tick_count += expirations;
	late = now_ns - expected_ns;
	
	return late;
}

void run_benchmark(); // forward declare

void autofire_timer() {
    if (vtime_changed())
		timer_started = false; // restart timers if vrefresh has changed;
	if (!timer_started)
        timer_started = init_timer(get_vtime());
	if (timer_started)
		new_frame = check_vtimer(pcurrent_video_info->vtime * 10ull);
    if (timer_started && BENCHMARK_AUTOFIRE)
		run_benchmark();
}

double compute_stddev(const uint64_t *arr, int N)
{
    if (N <= 1) return 0.0;

	// mean
	double sum = 0.0;
    for (int i = 0; i < N; i++)
        sum += (double)arr[i];

    double mean = sum / N;

	// variance
	double var = 0.0;
    for (int i = 0; i < N; i++) {
        double diff = (double)arr[i] - mean;
        var += diff * diff;
    }
    var /= N;

	// standard deviation
	return sqrt(var);
}

void run_benchmark() {

		static uint64_t late_times[1024];
		static int num_times = 0;

		if (new_frame)
			late_times[num_times++] = new_frame;

		if (num_times >= 1024) {
			uint64_t sum = 0;
			for (int i = 0; i < 1024; i++)
				sum += late_times[i];
			//double average = sum / num_times;
			uint64_t interval_ns = pcurrent_video_info->vtime * 10ull;
			float hz = 1e9f / interval_ns;
			double sd_ns = compute_stddev(late_times, num_times);
			double sd_us = sd_ns / 1000.0;
			double sd_ms = sd_ns / 1e6;
			double frame_ns = 1e9f / hz;
			double pct = (sd_ns / frame_ns) * 100.0;

			printf("jitter stddev over %d frames: %.0f ns (%.2f us, %.4f ms, %.3f%% of %.2fhz frame)\n",
       			num_times, sd_ns, sd_us, sd_ms, pct, hz);
			num_times = 0;
		}
	}