# Operating Systems Project – One File Server Simulation

## 📖 Project Overview
This project is a simple simulation of a server developed for the **Operating Systems** course.  
It demonstrates core OS concepts such as **multithreading**, **synchronization**, and **file handling** using **C++**.

The system supports **user signup**, **login**, and **forgot password** operations.  
All user data is stored in a single shared text file, simulating a basic server-side database.

---

## 🎯 Objectives
- Understand multithreading in operating systems
- Implement concurrent access to shared resources
- Learn synchronization using mutex locks
- Simulate server request handling
- Practice file handling in C++

---

## 🧠 Operating System Concepts Used
- Multithreading
- Critical Section
- Synchronization
- Mutual Exclusion (Mutex)
- Shared Resource Management
- File System Operations
- Process and Thread Coordination

---

## 🛠 Technologies Used
- **Programming Language:** C++
- **Compiler:** g++
- **Operating System:** Linux / Windows
- **Libraries Used:**
  - `<thread>`
  - `<mutex>`
  - `<fstream>`
  - `<iostream>`
  - `<string>`

---

## 🏗 System Architecture
- Single server process
- Multiple threads simulate client requests
- One shared file (`users.txt`) acts as a database
- Mutex ensures safe access to the shared file
- Threads execute concurrently to demonstrate OS behavior

---

## 🧵 Threads Description
The following threads are created to simulate concurrent user requests:

| Operation         | Number of Threads |
|------------------|------------------|
| Signup           | 3                |
| Login            | 2                |
| Forgot Password  | 2                |

Each thread accesses the shared user file in a controlled and synchronized manner.

---

## 📂 Project Structure
