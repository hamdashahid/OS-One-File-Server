# OS Project – Traffic Intersection & Parking Lot Simulation (Fall 2025)

## 📘 Course Information
- **Course:** Operating Systems  
- **Semester:** Fall 2025  
- **Project Type:** Concurrent System Simulation  
- **Group Size:** 3 Students  

---

## 📖 Project Overview
This project simulates **two neighboring traffic intersections (F10 and F11)** using **Operating System concepts** such as **multithreading, multiprocessing, inter-process communication (IPC), synchronization, and semaphores**.

Vehicles are represented as **threads**, traffic controllers are implemented as **separate processes**, and coordination between intersections is achieved using **pipes**.  
An integrated **Parking Lot System** is attached to each intersection, simulating real-world parking behavior under limited capacity and high traffic load.

The simulation focuses on **safe concurrent execution**, **priority handling**, and **resource management**, rather than real-world traffic accuracy.

---

## 🎯 Project Objectives
- Demonstrate multithreading using pthreads
- Implement inter-process communication using pipes
- Apply synchronization using mutexes and semaphores
- Handle priority-based scheduling (emergency vehicles)
- Prevent deadlocks and intersection blocking
- Simulate bounded waiting queues
- Ensure graceful shutdown and cleanup

---

## 🧠 Operating System Concepts Used
- Threads (pthread)
- Processes (fork)
- Inter-Process Communication (pipes)
- Semaphores
- Mutex Locks
- Critical Sections
- Priority Scheduling
- Resource Allocation
- Signal Handling (SIGINT)
- Graceful Shutdown

---

## 🚦 High-Level System Scenario

### Intersections
- Two intersections: **F10** and **F11**
- Operate independently but coordinate when required
- Vehicles can move between F10 and F11

### Vehicles
- **Default Count:** 15 (configurable)
- Each vehicle is a **thread**
- Vehicles spawn at random intervals
- Each vehicle has:
  - `id`
  - `type`
  - `origin`
  - `destination` (left / right / straight)
  - `priority`
  - `arrival_time`

### Vehicle Categories
- Ambulance (Highest Priority)
- Firetruck (Highest Priority)
- Bus (Medium Priority)
- Car (Normal Priority)
- Bike (Lower Priority)
- Tractor (Lower Priority)

---

## 🚨 Priority Rules
- Ambulances and Firetrucks:
  - Highest priority
  - Can interrupt normal traffic signal cycles
  - Path is cleared in advance
- Buses:
  - Medium priority
- Cars, Bikes, Tractors:
  - Normal / lower priority

Emergency vehicles **never interact with parking lots**.

---

## 🧵 Traffic Controllers (Processes)
- Each intersection has **one controller**
- Controllers are implemented as **separate processes**
- Spawned using `fork()`
- Controllers coordinate using **pipes**
- Used to demonstrate **IPC in Operating Systems**

---

## 🔄 Coordination Between Intersections
When an emergency vehicle moves between intersections:
- Source intersection informs the destination via pipe
- Destination clears its intersection in advance
- Prevents emergency vehicle blocking
- Ensures safe and uninterrupted passage

---

## 🅿️ Parking Lot System

Each intersection (F10 & F11) has an attached parking lot.

### Core Characteristics

#### 1. Fixed Parking Capacity
- Each lot has **10 parking spots**
- Controlled using a **semaphore**
- No vehicle can park without acquiring a spot

#### 2. Bounded Waiting Queue
- Implemented using a second semaphore
- Limits the number of vehicles waiting to park
- Prevents unbounded queue growth

#### 3. Safe Parking Interaction
Vehicles intending to park must:
- Reserve a parking spot or waiting slot **before entering intersection**
- Never block the intersection while waiting
- Only request intersection access after confirming parking availability

#### 4. Priority Interaction
- Emergency vehicles bypass parking logic
- Parking must not interfere with emergency preemption
- Parking-bound vehicles may be delayed to clear emergency paths

---

## 🚥 Safe Crossing Constraint
- Only **non-conflicting movements** can occur concurrently
- Prevents collisions and unsafe crossings
- Enforced via synchronization mechanisms

---

## 🛠 Technologies Used
- **Language:** C++
- **Compiler:** g++
- **OS:** Linux / Windows
- **Libraries:**
  - `<pthread.h>`
  - `<semaphore.h>`
  - `<unistd.h>`
  - `<signal.h>`
  - `<sys/types.h>`
  - `<iostream>`
  - `<vector>`
  - `<ctime>`

---

## 📂 Project Structure
