#include "controller.h"
#include <iostream>
#include <unistd.h>
#include <mutex>
using namespace std;
// Preemption control functions
#include "intersection.h"

// External log mutex
extern mutex g_log_mutex;

// ANSI Color Codes
#define ANSI_RESET   "\033[0m"
#define ANSI_BOLD    "\033[1m"
#define ANSI_RED     "\033[31m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_CYAN    "\033[36m"
#define ANSI_MAGENTA "\033[35m"

// Define global pipes (real definitions are in main.cpp)
// ✔ Correct version (only declaration)
extern int pipeF10toF11[2];
extern int pipeF11toF10[2];

string signal_name(ControllerSignal s) {
    switch (s) {
        case ControllerSignal::NORMAL:            return "NORMAL_TRAFFIC";
        case ControllerSignal::EMERGENCY_INCOMING:return "EMERGENCY_ALERT";
        case ControllerSignal::SHUTDOWN:          return "SHUTDOWN";
    }
    return "UNKNOWN";
}

// Helper used by parent/vehicle threads to notify the controllers
void notify_emergency_from_to(IntersectionId from, IntersectionId to) {
    if (from == to) return; // no cross-intersection movement

    ControllerSignal sig = ControllerSignal::EMERGENCY_INCOMING;

    if (from == IntersectionId::F10 && to == IntersectionId::F11) {
        // Preempt destination intersection to clear path
        set_emergency_preempt(IntersectionId::F11, true);
        // Message goes F10 -> F11
        write(pipeF10toF11[1], &sig, sizeof(sig));
        {
            std::lock_guard<std::mutex> lk(g_log_mutex);
            cout << ANSI_BOLD << ANSI_RED << "🚨 [PARENT] Emergency F10→F11: Preempting F11 intersection" << ANSI_RESET << endl;
        }
    } else if (from == IntersectionId::F11 && to == IntersectionId::F10) {
        // Preempt destination intersection to clear path
        set_emergency_preempt(IntersectionId::F10, true);
        // Message goes F11 -> F10
        write(pipeF11toF10[1], &sig, sizeof(sig));
        {
            std::lock_guard<std::mutex> lk(g_log_mutex);
            cout << ANSI_BOLD << ANSI_RED << "🚨 [PARENT] Emergency F11→F10: Preempting F10 intersection" << ANSI_RESET << endl;
        }
    }
}
        // Controller main loop (runs inside child process)
void run_controller(Controller ctrl) {
    {
        std::lock_guard<std::mutex> lk(g_log_mutex);
        cout << ANSI_BOLD << ANSI_GREEN << "📡 [Controller " << ctrl.name << "] ONLINE and listening" << ANSI_RESET << endl;
    }

    while (true) {
        ControllerSignal sig;
        int n = read(ctrl.read_fd, &sig, sizeof(sig));

        if (n <= 0) {
            // nothing to read; just continue
            continue;
        }

        {
            std::lock_guard<std::mutex> lk(g_log_mutex);
            cout << "[Controller " << ctrl.name << "] Received Signal: "
                 << signal_name(sig) << endl;

            if (sig == ControllerSignal::EMERGENCY_INCOMING) {
                cout << ANSI_BOLD << ANSI_RED << "🚨 [Controller " << ctrl.name
                     << "] EMERGENCY ALERT - Clearing intersection for emergency vehicle" << ANSI_RESET << endl;
                // Controller process cannot directly modify parent's intersections.
                // Logging here; parent sets preemption via notify_emergency_from_to.
            } else if (sig == ControllerSignal::SHUTDOWN) {
                cout << "[Controller " << ctrl.name << "] Shutting down.\n";
            }
        }

        if (sig == ControllerSignal::SHUTDOWN) {
            break;
        }
        // NORMAL can be used later if you want to reset to normal cycle.
    }

    // Child process ends
}
