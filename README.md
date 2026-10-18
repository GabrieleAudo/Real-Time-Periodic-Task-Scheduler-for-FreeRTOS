# Project_CAOS

# Project 3 - Simple Explanation

Project 3 asks you to **add support for non-periodic tasks** (tasks that run when something happens, not on a fixed schedule) to the scheduler you built in Project 2.

## The Core Idea

Think of it like a restaurant:
- **Periodic tasks** (from Project 2) = Regular reservations that come at fixed times
- **Aperiodic tasks** (Project 3) = Walk-in customers who show up randomly

You need a **"Polling Server"** - think of it as a waiter who checks the door every few minutes to see if there are walk-ins waiting, and serves them when their turn comes.

## What You Need to Build

### 1. The Polling Server
- It's a **special periodic task** that runs at regular intervals
- When it runs, it checks: "Are there any aperiodic tasks waiting to execute?"
- If yes, it runs them (one or more, depending on available time)
- If no, it just finishes and waits for its next turn

### 2. API for Aperiodic Tasks
Create functions to:
- Register aperiodic tasks (add them to a waiting queue)
- Each aperiodic task gets a **soft deadline** - how long it has to finish once it starts

### 3. Deadline Handling
When an aperiodic task misses its deadline, you can either:
- **OVERRUN**: Let it keep running (log the miss)
- **KILL**: Stop it immediately and move to the next task

## Example Scenario

```
Periodic Task A runs every 10ms (priority 3)
Polling Server runs every 20ms (priority 2)

At 15ms: Aperiodic task X arrives (deadline = 8ms)
At 20ms: Polling Server wakes up → starts running task X
At 28ms: Task X should finish (20 + 8 = 28)
At 30ms: Task X still running → DEADLINE MISS!
         → Apply policy (OVERRUN or KILL)
```

## Key Point

The polling server protects your periodic tasks (the important ones) while still giving aperiodic tasks a chance to run in a controlled, predictable way.