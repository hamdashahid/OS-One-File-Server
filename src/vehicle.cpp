#include <iostream>
#include <pthread.h>
#include <unistd.h>   // sleep
#include <cstdlib>    // rand
#include <sstream>
#include <iomanip>
#include <mutex>
using namespace std;

// External log mutex
extern mutex g_log_mutex;

// ANSI Color Codes
#define ANSI_RESET   "\033[0m"
#define ANSI_BOLD    "\033[1m"
#define ANSI_RED     "\033[31m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_BLUE    "\033[34m"
#define ANSI_MAGENTA "\033[35m"
#define ANSI_CYAN    "\033[36m"

#include "vehicle.h"
#include "intersection.h"
#include "parking.h"
#include "controller.h"   // for notify_emergency_from_to
#include "ui_shared.h"     // for UI approach hooks

// ------------- RANDOM HELPERS -----------------
static int rand_int(int min, int max) {
    return min + rand() % (max - min + 1);
}

static bool rand_bool(float probability = 0.5) {
    return ((float)rand() / RAND_MAX) < probability;
}

// ------------- STRING FUNCTIONS ----------------
static string vehicleEmoji(VehicleType type) {
    switch (type) {
        case VehicleType::Ambulance: return "🚑";
        case VehicleType::FireTruck: return "🚒";
        case VehicleType::Bus:       return "🚌";
        case VehicleType::Car:       return "🚗";
        case VehicleType::Bike:      return "🚲";
        case VehicleType::Tractor:   return "🚜";
    }
    return "🚙";
}

string to_string(VehicleType type) {
    switch (type) {
        case VehicleType::Ambulance: return "Ambulance";
        case VehicleType::FireTruck: return "FireTruck";
        case VehicleType::Bus:       return "Bus";
        case VehicleType::Car:       return "Car";
        case VehicleType::Bike:      return "Bike";
        case VehicleType::Tractor:   return "Tractor";
    }
    return "Unknown";
}

string to_string(Direction dir) {
    switch (dir) {
        case Direction::Straight: return "Straight";
        case Direction::Left:     return "Left";
        case Direction::Right:    return "Right";
    }
    return "Unknown";
}

string to_string(IntersectionId id) {
    switch (id) {
        case IntersectionId::F10: return "F10";
        case IntersectionId::F11: return "F11";
    }
    return "Unknown";
}

// ------------- PRIORITY RULES ------------------
int compute_priority(VehicleType type) {
    if (type == VehicleType::Ambulance) return 0;
    if (type == VehicleType::FireTruck) return 1;
    if (type == VehicleType::Bus)       return 2;
    return 3;  // normal vehicles
}

// ------------- RANDOM VEHICLE GENERATION --------
Vehicle make_random_vehicle(int id) {
    VehicleType type = static_cast<VehicleType>(rand_int(0, 5));
    IntersectionId origin = rand_bool() ? IntersectionId::F10 : IntersectionId::F11;
    IntersectionId dest   = rand_bool() ? IntersectionId::F10 : IntersectionId::F11;
    Direction dir = static_cast<Direction>(rand_int(0, 2));

    bool parkingAllowed =
        (type == VehicleType::Car ||
         type == VehicleType::Bike ||
         type == VehicleType::Bus ||
         type == VehicleType::Tractor);

    bool wantsParking = parkingAllowed && rand_bool(0.5);

    Vehicle v;
    v.id = id;
    v.type = type;
    v.priority = compute_priority(type);
    v.arrival_time = time(NULL);
    v.originIntersection = origin;
    v.destIntersection = dest;
    v.direction = dir;
    v.wantsParking = wantsParking;

    return v;
}

// ---------------- VEHICLE THREAD ----------------
void* vehicle_thread_func(void* arg) {
    Vehicle* v = (Vehicle*)arg;

    // Color based on type
    const char* vColor = ANSI_BLUE;
    if (v->type == VehicleType::Ambulance) vColor = ANSI_RED;
    else if (v->type == VehicleType::FireTruck) vColor = ANSI_RED;
    else if (v->type == VehicleType::Bus) vColor = ANSI_YELLOW;
    else if (v->type == VehicleType::Bike || v->type == VehicleType::Tractor) vColor = ANSI_GREEN;

    {
        std::lock_guard<std::mutex> lk(g_log_mutex);
        cout << ANSI_BOLD << vColor << "\n" << vehicleEmoji(v->type) << " [Vehicle #" << setw(2) << v->id << "] "
             << to_string(v->type) << ANSI_RESET << endl;
        cout << "  ├─ Origin: " << ANSI_CYAN << to_string(v->originIntersection) << ANSI_RESET
             << " → Destination: " << ANSI_CYAN << to_string(v->destIntersection) << ANSI_RESET << endl;
        cout << "  ├─ Direction: " << to_string(v->direction)
             << " | Priority: " << ANSI_MAGENTA << v->priority << ANSI_RESET << endl;
        cout << "  └─ Parking: " << (v->wantsParking ? ANSI_GREEN "YES" : "NO") << ANSI_RESET << endl;
    }

    // Determine intersection + parking lot
    Intersection *I = NULL;
    ParkingLot *lot = NULL;

    if (v->originIntersection == IntersectionId::F10) {
        I = &F10_intersection;
        lot = &F10_parking;
    } else {
        I = &F11_intersection;
        lot = &F11_parking;
    }

    bool hasReservedParking = false;

    // Emergency vehicles NEVER interact with parking
    if (v->wantsParking &&
        v->type != VehicleType::Ambulance &&
        v->type != VehicleType::FireTruck) {

        hasReservedParking = reserve_parking_spot(*lot, v);

        if (!hasReservedParking) {
            std::lock_guard<std::mutex> lk(g_log_mutex);
            cout << ANSI_YELLOW << "  ⚠️  [Vehicle #" << v->id
                 << "] Could not reserve parking - will pass through" << ANSI_RESET << endl;
        }
    }

    {
        std::lock_guard<std::mutex> lk(g_log_mutex);
        cout << ANSI_CYAN << "  ➤ [Vehicle #" << v->id << "] Approaching 🚦 "
             << intersection_name(v->originIntersection) << ANSI_RESET << endl;
    }

    // Notify UI that the vehicle is approaching (to animate stopping at stop line)
    ui_notify_vehicle_approach(v->originIntersection, v);
    std::ostringstream oss;
    oss << "V" << v->id << " " << to_string(v->type) << " approaching";
    ui_log_event(oss.str());

    // If emergency and moving cross-intersection, preempt destination early to clear path
    if ((v->type == VehicleType::Ambulance || v->type == VehicleType::FireTruck)
        && v->originIntersection != v->destIntersection) {
        notify_emergency_from_to(v->originIntersection, v->destIntersection);
    }

    // Medium priority for bus: allow entering on ANSI_RED when intersection is free (without preemption)
    // This operationalizes bus priority without full scheduler
    if (v->type == VehicleType::Bus) {
        // Temporarily set light to ANSI_GREEN-like behavior for bus when intersection free and no emergency preempt
        // This is handled inside enter_intersection by checking emergency_preempt and busy
    }

    // Request to enter intersection (blocks if busy)
    enter_intersection(*I, v);

    // Simulate time taken to cross intersection
    sleep(rand_int(1, 2));

    // Leave intersection
    leave_intersection(*I, v);

    // After crossing, if emergency preemption was set on destination, clear it to resume normal traffic
    if ((v->type == VehicleType::Ambulance || v->type == VehicleType::FireTruck)
        && v->originIntersection != v->destIntersection) {
        set_emergency_preempt(v->destIntersection, false);
    }

    // If parking was reserved, now simulate actual parking usage
    if (hasReservedParking) {
        ui_notify_vehicle_parking(v->id, true);
        use_and_release_parking(*lot, v);
        ui_notify_vehicle_parking(v->id, false);
    }

    {
        std::lock_guard<std::mutex> lk(g_log_mutex);
        cout << ANSI_BOLD << ANSI_GREEN << "  ✓ [Vehicle #" << v->id << "] Journey completed successfully" << ANSI_RESET << endl;
    }

    return NULL;
}
