#include "signal.h"
#include "../drivers/serial.h"
#include "../process.h"

static inline void set_pending(process_t* p, int sig) {
    if (!p || sig <= 0 || sig >= 32) return;
    p->sig_pending |= (1u << sig);
}

void signal_raise(process_t* p, int sig) {
    if (!p) return;
    set_pending(p, sig);
}

void signal_check(process_t* p) {
    if (!p) return;
    uint32_t pend = p->sig_pending & ~p->sig_blocked;
    if (!pend) return;

    //handle fatal signals by default
    int fatal_sig = 0;
    if (pend & (1u << SIGKILL)) fatal_sig = SIGKILL;
    else if (pend & (1u << SIGTERM)) fatal_sig = SIGTERM;
    else if (pend & (1u << SIGINT)) fatal_sig = SIGINT;

    if (fatal_sig) {
        //clear and terminate the process
        p->sig_pending &= ~(1u << fatal_sig);
        process_exit(128 + fatal_sig);
        return; //not reached for current process
    }

    //clear SIGCHLD if pending (default is to ignore but parent may read it)
    if (pend & (1u << SIGCHLD)) {
        p->sig_pending &= ~(1u << SIGCHLD);
    }
}

void signal_check_current(void) {
    process_t* cur = process_get_current();
    signal_check(cur);
}
