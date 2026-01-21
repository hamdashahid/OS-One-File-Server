#include "intersection.h"
#include <iostream>
#include <unistd.h>   // sleep
#include <sstream>
#include <iomanip>
using namespace std;
// UI hooks
#include "ui_shared.h"

// ANSI Color Codes
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

// Define the global intersections here
Intersection F10_intersection;
Intersection F11_intersection;

// Internal: traffic light manager thread
static pthread_t traffic_thread;
static bool traffic_running = false;

string intersection_name(IntersectionId id) {
    if (id == IntersectionId::F10) return "F10";
    return "F11";
}

void init_intersection(Intersection &I, IntersectionId id) {
    I.id = id;
    I.busy = false;
    I.light = LightColor::RED;   // will be set properly by traffic manager
    I.emergency_preempt = false;
    I.active_count = 0;
    I.active_straight = false;
    I.active_left = false;
    I.active_right = false;

    pthread_mutex_init(&I.lock, NULL);
    pthread_cond_init(&I.canPass, NULL);
}

// ---- Vehicle entering intersection respecting lights ----
void enter_intersection(Intersection &I, Vehicle *v) {
    pthread_mutex_lock(&I.lock);

    bool isEmergency =
        (v->type == VehicleType::Ambulance ||
         v->type == VehicleType::FireTruck);

    while (true) {
        // Determine non-conflicting concurrency eligibility
        bool noActive = (I.active_count == 0);
        bool straightCompat = (v->direction == Direction::Straight) && I.active_straight && !I.active_left && !I.active_right;
        bool canEnterNow = noActive || straightCompat;

        if (isEmergency) {
            // Emergency vehicles ignore ANSI_RED/ ANSI_GREEN, only wait for intersection to be free.
            if (canEnterNow) break;
        } else {
            // Normal vehicle must obey ANSI_GREEN *and* intersection must be free.
            // Additionally, if emergency preemption is active, non-emergency must wait
            // Medium priority for Bus: allow entry on ANSI_RED when intersection is free and no emergency preempt
            bool bus_red_override = (v->type == VehicleType::Bus) && !I.emergency_preempt && canEnterNow;
            if ((!I.emergency_preempt && I.light == LightColor::GREEN && canEnterNow) || bus_red_override) break;
        }

        // Wait for condition: either light changes or intersection/movement becomes available
        pthread_cond_wait(&I.canPass, &I.lock);
    }

    // Register active movement
    if (v->direction == Direction::Straight) I.active_straight = true;
    else if (v->direction == Direction::Left) I.active_left = true;
    else I.active_right = true;
    I.active_count++;
    I.busy = (I.active_count > 0); // busy now indicates occupancy

    cout << ANSI_BOLD << ANSI_GREEN << "▶️  [Vehicle #" << setw(2) << v->id
         << " " << to_string(v->type)
         << "] ENTERED " << intersection_name(I.id) << ANSI_RESET;
    if (I.light == LightColor::GREEN) {
        cout << ANSI_BG_GREEN << " [GREEN] " << ANSI_RESET << endl;
    } else {
        cout << ANSI_BG_RED << " [RED] " << ANSI_RESET << " (Emergency/Bus Priority)" << endl;
    }
    // UI: vehicle enter
    ui_notify_vehicle_enter(I.id, v);
    std::ostringstream oss;
    oss << "V" << v->id << " " << to_string(v->type) << " entered " << intersection_name(I.id);
    ui_log_event(oss.str());

    if (I.active_count > 1) {
        cout << ANSI_BOLD << ANSI_MAGENTA << "  🔀 [" << intersection_name(I.id)
             << "] Concurrent movement: " << I.active_count << " vehicles crossing" << ANSI_RESET << endl;
    }

    pthread_mutex_unlock(&I.lock);
}

// ---- Vehicle leaving intersection ----
void leave_intersection(Intersection &I, Vehicle *v) {
    pthread_mutex_lock(&I.lock);

    // Deregister movement
    if (v->direction == Direction::Straight) I.active_straight = false;
    else if (v->direction == Direction::Left) I.active_left = false;
    else I.active_right = false;
    if (I.active_count > 0) I.active_count--;
    I.busy = (I.active_count > 0);

    cout << ANSI_BOLD << ANSI_BLUE << "◀️  [Vehicle #" << setw(2) << v->id
         << " " << to_string(v->type)
         << "] EXITED " << intersection_name(I.id) << ANSI_RESET << endl;
    // UI: vehicle exit
    ui_notify_vehicle_exit(I.id, v);
    std::ostringstream oss;
    oss << "V" << v->id << " exited " << intersection_name(I.id);
    ui_log_event(oss.str());

    // Wake up waiting vehicles to re-check conditions
    pthread_cond_broadcast(&I.canPass);
    pthread_mutex_unlock(&I.lock);
}

// ---- Traffic light manager thread function ----
static void set_light(Intersection &I, LightColor color, const char *label) {
    pthread_mutex_lock(&I.lock);
    I.light = color;
    
    if (color == LightColor::GREEN) {
        cout << ANSI_BOLD << ANSI_BG_GREEN << " 🚦 " << ANSI_RESET << ANSI_GREEN << " [" << label << "] "
             << intersection_name(I.id) << " → GREEN" << ANSI_RESET << endl;
    } else {
        cout << ANSI_BOLD << ANSI_BG_RED << " 🚦 " << ANSI_RESET << ANSI_RED << " [" << label << "] "
             << intersection_name(I.id) << " → RED" << ANSI_RESET << endl;
    }
    // notify UI
    ui_update_signal(I.id, color);
    std::ostringstream oss;
    oss << intersection_name(I.id) << " light -> " << (color == LightColor::GREEN ? "GREEN" : "RED");
    ui_log_event(oss.str());
    // Wake all vehicles waiting here so they can re-check the light
    pthread_cond_broadcast(&I.canPass);
    pthread_mutex_unlock(&I.lock);
}

static void* traffic_light_manager(void* arg) {
    (void)arg;

    cout << ANSI_BOLD << ANSI_YELLOW << "\n🚦 [TRAFFIC CONTROL] Light manager started - 3s cycle" << ANSI_RESET << endl;

    // Initial state: F10 ANSI_GREEN, F11 ANSI_RED
    set_light(F10_intersection, LightColor::GREEN, "Initial");
    set_light(F11_intersection, LightColor::RED,   "Initial");

    while (traffic_running) {
        // F10 ANSI_GREEN, F11 ANSI_RED
        sleep(3); // keep this state for 3 seconds
        if (!traffic_running) break;

        // Switch: F10 ANSI_RED, F11 ANSI_GREEN
        set_light(F10_intersection, LightColor::RED,   "Cycle");
        set_light(F11_intersection, LightColor::GREEN, "Cycle");

        sleep(3);
        if (!traffic_running) break;

        // Switch back: F10 ANSI_GREEN, F11 ANSI_RED
        set_light(F10_intersection, LightColor::GREEN, "Cycle");
        set_light(F11_intersection, LightColor::RED,   "Cycle");
    }

    cout << "[TRAFFIC] Traffic light manager stopping.\n";
    return NULL;
}

// ---- Public API for main.cpp ----
void start_traffic_lights() {
    traffic_running = true;
    pthread_create(&traffic_thread, NULL, traffic_light_manager, NULL);
}

void stop_traffic_lights() {
    traffic_running = false;
    // Wake all waiting vehicles so they don't block forever
    pthread_mutex_lock(&F10_intersection.lock);
    pthread_cond_broadcast(&F10_intersection.canPass);
    pthread_mutex_unlock(&F10_intersection.lock);

    pthread_mutex_lock(&F11_intersection.lock);
    pthread_cond_broadcast(&F11_intersection.canPass);
    pthread_mutex_unlock(&F11_intersection.lock);

    pthread_join(traffic_thread, NULL);
}

// ---- Emergency preemption controls ----
void set_emergency_preempt(IntersectionId id, bool enabled) {
    Intersection *I = (id == IntersectionId::F10) ? &F10_intersection : &F11_intersection;
    pthread_mutex_lock(&I->lock);
    I->emergency_preempt = enabled;
    // Setting preempt to true should wake threads to re-check conditions (they will block if non-emergency)
    // Clearing preempt should also wake threads to allow progress
    pthread_cond_broadcast(&I->canPass);
    pthread_mutex_unlock(&I->lock);
    // Notify UI
    ui_notify_emergency_preempt(id, enabled);
    if (enabled) {
        std::ostringstream oss;
        oss << "EMERGENCY preempt at " << intersection_name(id);
        ui_log_event(oss.str());
    }
}

bool is_emergency_preempt(IntersectionId id) {
    Intersection *I = (id == IntersectionId::F10) ? &F10_intersection : &F11_intersection;
    return I->emergency_preempt;
}

// ---- Resource cleanup ----
void destroy_intersection(Intersection &I) {
    pthread_mutex_lock(&I.lock);
    I.busy = false;
    I.emergency_preempt = false;
    pthread_cond_broadcast(&I.canPass);
    pthread_mutex_unlock(&I.lock);
    pthread_mutex_destroy(&I.lock);
    pthread_cond_destroy(&I.canPass);
}
