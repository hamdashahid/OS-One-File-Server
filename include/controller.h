#pragma once
#include <unistd.h>
#include <string>
using namespace std;

#include "vehicle.h"

// Message types controller will send/receive
enum class ControllerSignal {
    NORMAL,
    EMERGENCY_INCOMING,
    SHUTDOWN
};

struct Controller {
    string name;
    int read_fd;   // read-end of pipe
    int write_fd;  // write-end of pipe (to other controller, if needed)
};

// Global pipes (defined in main.cpp)
extern int pipeF10toF11[2];   // [0]=read, [1]=write
extern int pipeF11toF10[2];   // [0]=read, [1]=write

void run_controller(Controller ctrl);
string signal_name(ControllerSignal s);

// Called by parent/vehicles when an emergency vehicle moves
void notify_emergency_from_to(IntersectionId from, IntersectionId to);
