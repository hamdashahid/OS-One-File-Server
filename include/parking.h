#pragma once

#include <semaphore.h>
#include <pthread.h>
#include <string>
using namespace std;

#include "vehicle.h"

// Parking lot attached to an intersection
struct ParkingLot {
    string name;

    int max_spots;     // total parking spots
    int max_queue;     // max waiting queue size

    sem_t available_spots;   // semaphore: free parking spots
    sem_t waiting_slots;     // semaphore: free positions in waiting queue

    pthread_mutex_t state_lock;  // for debug counters
    int current_spots;           // how many cars are currently parked
};

// Global parking lots (one per intersection)
extern ParkingLot F10_parking;
extern ParkingLot F11_parking;

// Initialize parking lot with given name, spots, queue size
void init_parking_lot(ParkingLot &lot, const string &name,
                      int spots = 10, int queueSize = 5);

// Try to reserve a parking spot for a vehicle.
// Returns true if vehicle successfully reserved a spot,
// false if waiting queue was full (vehicle skips parking).
bool reserve_parking_spot(ParkingLot &lot, Vehicle *v);

// Simulate staying in parking and then release the spot.
void use_and_release_parking(ParkingLot &lot, Vehicle *v);

// Cleanup parking resources
void destroy_parking_lot(ParkingLot &lot);
