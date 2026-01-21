#pragma once

#include <string>
using namespace std;

// Basic intersections in the system
enum class IntersectionId {
    F10,
    F11
};

// Directions a vehicle can take at an intersection
enum class Direction {
    Straight,
    Left,
    Right
};

// Types of vehicles in the simulation
enum class VehicleType {
    Ambulance,
    FireTruck,
    Bus,
    Car,
    Bike,
    Tractor
};

// Core vehicle metadata
struct Vehicle {
    int id;
    VehicleType type;
    int priority;   // smaller value = higher priority
    time_t arrival_time; // spawn time
    IntersectionId originIntersection;
    IntersectionId destIntersection;
    Direction direction;
    bool wantsParking;
};

// --- Utility conversion helpers ---

string to_string(VehicleType type);
string to_string(Direction dir);
string to_string(IntersectionId id);

// Compute priority based on type
int compute_priority(VehicleType type);

// Factory to create a random vehicle with given id
Vehicle make_random_vehicle(int id);

// Thread function that simulates a vehicle's life
void* vehicle_thread_func(void* arg);
