#define SIXTYHERTZ  1666667 // fallback if vtime doesn't work
#define THIRTYHERTZ 3333333 // lowest refresh rate we consider valid
#define NINETYHERTZ 1111111 // highest refresh rate we consider valid
void autofire_timer();

// global
extern uint64_t new_frame;

// from video.h

//static bool timer_started = false;
//static int vtimerfd = -1;
//static uint64_t vtimer_start_ns;