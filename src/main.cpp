#include <iostream>
#include <pthread.h>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <iomanip>
#include <mutex>
#include <sstream>
using namespace std;

// ANSI color codes for terminal output
#define ANSI_RESET   "\033[0m"
#define ANSI_BOLD    "\033[1m"
#define ANSI_RED     "\033[31m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_BLUE    "\033[34m"
#define ANSI_MAGENTA "\033[35m"
#define ANSI_CYAN    "\033[36m"
#define ANSI_BG_RED  "\033[41m"
#define ANSI_BG_GREEN "\033[42m"

#include "vehicle.h"
#include "intersection.h"
#include "parking.h"
#include "controller.h"
#include "ui_shared.h"

// Global log mutex for thread-safe output
mutex g_log_mutex;

// Define the pipes here (storage for the extern in controller.cpp)
int pipeF10toF11[2];
int pipeF11toF10[2];

static volatile sig_atomic_t g_shutdown = 0;

static void sigint_handler(int){
    g_shutdown = 1;
}

int main(int argc, char** argv) {
    srand(time(NULL));
    signal(SIGINT, sigint_handler);

    cout << ANSI_BOLD << ANSI_CYAN << "\n" << string(70, '=') << ANSI_RESET << endl;
    cout << ANSI_BOLD << ANSI_CYAN << "       TRAFFIC SIMULATION SYSTEM - F10 & F11 INTERSECTIONS" << ANSI_RESET << endl;
    cout << ANSI_BOLD << ANSI_CYAN << string(70, '=') << ANSI_RESET << endl;
    cout << ANSI_YELLOW << "  📍 Two Intersections | 🚗 Concurrent Vehicles | 🚨 Emergency Priority" << ANSI_RESET << endl;
    cout << ANSI_YELLOW << "  🅿️  Parking System | 🚦 Traffic Controllers | 🔄 IPC via Pipes" << ANSI_RESET << endl;
    cout << ANSI_BOLD << ANSI_CYAN << string(70, '=') << ANSI_RESET << "\n" << endl;

    // Create pipes for two-way controller communication
    if (pipe(pipeF10toF11) == -1 || pipe(pipeF11toF10) == -1) {
        cerr << "Failed to create pipes.\n";
        return 1;
    }

    // Fork controller for F10
    pid_t f10 = fork();
    if (f10 == 0) {
        // Child process: Controller F10
        Controller ctrl10;
        ctrl10.name = "F10";
        ctrl10.read_fd  = pipeF11toF10[0]; // reads messages sent F11 -> F10
        ctrl10.write_fd = pipeF10toF11[1]; // could write F10 -> F11
        run_controller(ctrl10);
        return 0;
    }

    // Fork controller for F11
    pid_t f11 = fork();
    if (f11 == 0) {
        // Child process: Controller F11
        Controller ctrl11;
        ctrl11.name = "F11";
        ctrl11.read_fd  = pipeF10toF11[0]; // reads messages sent F10 -> F11
        ctrl11.write_fd = pipeF11toF10[1]; // could write F11 -> F10
        run_controller(ctrl11);
        return 0;
    }

    // Parent process continues here: simulation engine
    cout << ANSI_BOLD << ANSI_GREEN << "\n✓ [SYSTEM] Both traffic controllers initialized successfully" << ANSI_RESET << endl;
    cout << ANSI_BLUE << "  └─ Controller F10: Process ID " << f10 << ANSI_RESET << endl;
    cout << ANSI_BLUE << "  └─ Controller F11: Process ID " << f11 << ANSI_RESET << endl;

    // Initialize intersections
    init_intersection(F10_intersection, IntersectionId::F10);
    init_intersection(F11_intersection, IntersectionId::F11);

    // Initialize parking lots
    init_parking_lot(F10_parking, "F10 Parking Lot", 10, 5);
    init_parking_lot(F11_parking, "F11 Parking Lot", 10, 5);

    // 🔹 Start traffic lights
    start_traffic_lights();
    // 🔹 Start UI
    cout << ANSI_BOLD << ANSI_GREEN << "\n✓ [SYSTEM] Starting SFML Visual Interface..." << ANSI_RESET << endl;
    ui_start();

    int NUM_VEHICLES = 15;
    if (argc >= 2) {
        int n = atoi(argv[1]);
        if (n > 0) NUM_VEHICLES = n;
    }

    cout << ANSI_BOLD << ANSI_YELLOW << "\n🚗 [SIMULATION] Spawning " << NUM_VEHICLES << " vehicles..." << ANSI_RESET << endl;
    cout << ANSI_CYAN << string(70, '-') << ANSI_RESET << "\n" << endl;

    vector<Vehicle> vehicles;
    vector<pthread_t> threads(NUM_VEHICLES);

    // Create vehicles
    for (int i = 0; i < NUM_VEHICLES; ++i) {
        vehicles.push_back(make_random_vehicle(i + 1));
    }

    // Spawn vehicle threads
    for (int i = 0; i < NUM_VEHICLES; ++i) {
        if (g_shutdown) break;
        int ret = pthread_create(&threads[i], NULL, vehicle_thread_func, &vehicles[i]);
        if (ret != 0) {
            cerr << "Error creating thread for vehicle " << vehicles[i].id
                 << ", pthread_create returned " << ret << endl;
        }
        // randomized spawn delay 100-500ms
        int delay_ms = 100 + rand() % 401;
        usleep(delay_ms * 1000);
    }

    // Join vehicle threads (respect shutdown)
    for (int i = 0; i < NUM_VEHICLES; ++i) {
        if (threads[i]) pthread_join(threads[i], NULL);
        if (g_shutdown) break;
    }

    cout << ANSI_BOLD << ANSI_CYAN << "\n" << string(70, '=') << ANSI_RESET << endl;
    cout << ANSI_BOLD << ANSI_GREEN << "✓ [SYSTEM] All vehicles completed their journeys" << ANSI_RESET << endl;
    cout << ANSI_YELLOW << "  └─ Initiating graceful shutdown sequence..." << ANSI_RESET << endl;

    // 🔹 Stop traffic lights thread (new in Step 6)
    stop_traffic_lights();
    // Stop UI
    ui_stop();

    // Send SHUTDOWN signal to both controllers
    ControllerSignal shutdownSig = ControllerSignal::SHUTDOWN;
    write(pipeF10toF11[1], &shutdownSig, sizeof(shutdownSig)); // to F11
    write(pipeF11toF10[1], &shutdownSig, sizeof(shutdownSig)); // to F10

    // Wait for child processes (controllers) to exit
    waitpid(f10, NULL, 0);
    waitpid(f11, NULL, 0);

    // Close pipes
    close(pipeF10toF11[0]);
    close(pipeF10toF11[1]);
    close(pipeF11toF10[0]);
    close(pipeF11toF10[1]);

    // Cleanup resources
    cout << ANSI_BLUE << "  └─ Cleaning up intersection resources..." << ANSI_RESET << endl;
    destroy_intersection(F10_intersection);
    destroy_intersection(F11_intersection);
    cout << ANSI_BLUE << "  └─ Cleaning up parking lot resources..." << ANSI_RESET << endl;
    destroy_parking_lot(F10_parking);
    destroy_parking_lot(F11_parking);

    cout << ANSI_BOLD << ANSI_GREEN << "\n✓ [SYSTEM] Simulation ended cleanly - All resources released" << ANSI_RESET << endl;
    cout << ANSI_BOLD << ANSI_CYAN << string(70, '=') << ANSI_RESET << "\n" << endl;
    return 0;
}
