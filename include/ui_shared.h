#pragma once

#include <vector>
#include <pthread.h>
#include "vehicle.h"
#include "intersection.h"

// UI lifecycle
void ui_start();
void ui_stop();

// Hooks from simulation
void ui_notify_vehicle_approach(IntersectionId id, Vehicle* v);
void ui_notify_vehicle_enter(IntersectionId id, Vehicle* v);
void ui_notify_vehicle_exit(IntersectionId id, Vehicle* v);
void ui_notify_vehicle_parking(int vehicleId, bool entering); // entering=true when parking, false when leaving
void ui_update_signal(IntersectionId id, LightColor color);
void ui_notify_emergency_preempt(IntersectionId id, bool active);
void ui_log_event(const std::string& message);
