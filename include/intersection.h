#pragma once

#include <string>
#include <pthread.h>
using namespace std;

#include "vehicle.h"

// Simple traffic light colors
enum class LightColor {
    RED,
    GREEN
};

// Simple intersection model
struct Intersection {
    IntersectionId id;

    pthread_mutex_t lock;     // to protect state
    pthread_cond_t canPass;   // condition variable for waiting
    bool busy;                // true if a vehicle is currently in intersection

    LightColor light;         // current signal color for this intersection

    // Emergency preemption flag: when true, non-emergency vehicles must wait
    volatile bool emergency_preempt;

    // Concurrent movement tracking (conservative): allow Straight+Straight only
    int active_count;         // number of vehicles currently inside
    bool active_straight;
    bool active_left;
    bool active_right;
};

// Global intersections (defined in intersection.cpp)
extern Intersection F10_intersection;
extern Intersection F11_intersection;

// Functions to manage intersections
void init_intersection(Intersection &I, IntersectionId id);

void enter_intersection(Intersection &I, Vehicle *v);

void leave_intersection(Intersection &I, Vehicle *v);

string intersection_name(IntersectionId id);

// Traffic light control (implemented in intersection.cpp)
void start_traffic_lights();
void stop_traffic_lights();
// Cleanup resources for intersections
void destroy_intersection(Intersection &I);

// Emergency preemption controls
void set_emergency_preempt(IntersectionId id, bool enabled);
bool is_emergency_preempt(IntersectionId id);
