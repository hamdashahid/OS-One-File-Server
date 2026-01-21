#include "parking.h"
#include <iostream>
#include <unistd.h>   // sleep
#include <cstdlib>
#include <iomanip>
#include <mutex>
using namespace std;
// For emergency preemption awareness
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

// Define global parking lots
ParkingLot F10_parking;
ParkingLot F11_parking;

// simple random helper
static int rand_int_p(int min, int max) {
    return min + rand() % (max - min + 1);
}

void init_parking_lot(ParkingLot &lot, const string &name,
                      int spots, int queueSize) {
    lot.name = name;
    lot.max_spots = spots;
    lot.max_queue = queueSize;
    lot.current_spots = 0;

    sem_init(&lot.available_spots, 0, spots);   // all spots free
    sem_init(&lot.waiting_slots, 0, queueSize); // queue capacity

    pthread_mutex_init(&lot.state_lock, NULL);
}

bool reserve_parking_spot(ParkingLot &lot, Vehicle *v) {
    {
        std::lock_guard<std::mutex> lk(g_log_mutex);
        cout << ANSI_CYAN << "  🅿️  [Vehicle #" << v->id << "] Requesting parking at "
             << lot.name << ANSI_RESET << endl;
    }

     // If emergency preemption is active at the origin intersection, avoid blocking by skipping parking
     // Determine intersection id from lot name (simple map: F10/F11 in name)
     IntersectionId originId = (lot.name.find("F10") != string::npos) ? IntersectionId::F10 : IntersectionId::F11;
     if (is_emergency_preempt(originId)) {
          {
              std::lock_guard<std::mutex> lk(g_log_mutex);
              cout << ANSI_BOLD << ANSI_RED << "  ⚠️  [Vehicle #" << v->id << "] Emergency preemption active - "
                   << "skipping parking" << ANSI_RESET << endl;
          }
          return false;
     }

    // Step 1: Try to enter waiting queue (bounded)
    if (sem_trywait(&lot.waiting_slots) != 0) {
        {
            std::lock_guard<std::mutex> lk(g_log_mutex);
            cout << ANSI_YELLOW << "  ⚠️  [Vehicle #" << v->id << "] Parking queue FULL - "
                 << "skipping parking" << ANSI_RESET << endl;
        }
        return false;
    }

    cout << "[Vehicle " << v->id << "] entered waiting queue at "
         << lot.name << endl;

    // Step 2: Wait for an available parking spot
    cout << "[Vehicle " << v->id << "] waiting for free spot at "
         << lot.name << endl;

    sem_wait(&lot.available_spots);   // blocks until a spot is free

    // Step 3: Now vehicle has reserved a spot; leave waiting queue
    sem_post(&lot.waiting_slots);

    pthread_mutex_lock(&lot.state_lock);
    lot.current_spots++;
    int usingNow = lot.current_spots;
    pthread_mutex_unlock(&lot.state_lock);

    {
        std::lock_guard<std::mutex> lk(g_log_mutex);
        cout << ANSI_BOLD << ANSI_GREEN << "  ✓ [Vehicle #" << v->id << "] RESERVED parking spot at "
             << lot.name << " (" << usingNow << "/" << lot.max_spots << " occupied)" << ANSI_RESET << endl;
    }

    // Important: we DO NOT release available_spots here.
    // It remains reserved until use_and_release_parking() is called.

    return true;
}

void use_and_release_parking(ParkingLot &lot, Vehicle *v) {
    {
        std::lock_guard<std::mutex> lk(g_log_mutex);
        cout << ANSI_MAGENTA << "  🅿️  [Vehicle #" << v->id << "] Now PARKED at " << lot.name << ANSI_RESET << endl;
    }

    // Simulate some parking duration
    sleep(rand_int_p(1, 3));

    pthread_mutex_lock(&lot.state_lock);
    lot.current_spots--;
    int usingNow = lot.current_spots;
    pthread_mutex_unlock(&lot.state_lock);

    // Release the parking spot
    sem_post(&lot.available_spots);

    {
        std::lock_guard<std::mutex> lk(g_log_mutex);
        cout << ANSI_CYAN << "  ➤ [Vehicle #" << v->id << "] LEFT parking at "
             << lot.name << " (" << usingNow << "/" << lot.max_spots << " occupied)" << ANSI_RESET << endl;
    }
}

void destroy_parking_lot(ParkingLot &lot) {
     // Ensure counters consistent, then destroy semaphores and mutex
     pthread_mutex_lock(&lot.state_lock);
     pthread_mutex_unlock(&lot.state_lock);
     sem_destroy(&lot.available_spots);
     sem_destroy(&lot.waiting_slots);
     pthread_mutex_destroy(&lot.state_lock);
}
