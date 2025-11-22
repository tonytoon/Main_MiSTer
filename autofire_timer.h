#define SIXTYHERTZ  1666667 // fallback if vtime doesn't work
#define THIRTYHERTZ 3333333 // lowest refresh rate we consider valid
#define NINETYHERTZ 1111111 // highest refresh rate we consider valid

// macro to tell if a new vertical refresh has happened since the last time we checked
// requires a local uint64_t to track frame counter
#define FRAME_TICK(last) \
    ((global_frame_counter != (last)) ? ((last) = global_frame_counter, 1) : 0)

void autofire_timer();

// global
extern uint64_t global_frame_counter;