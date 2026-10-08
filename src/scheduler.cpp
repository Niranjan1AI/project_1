//
//  scheduler.cpp
//  Processor Scheduler
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 9/13/26.
//
// Scheduler: This scheduler uses a FIFO queue where the cores are always
// set at p3. At the start the small cores are put in to idle while the big
// cores are turned off (C6). The big cores are woken only when more than 20
// jobs are waiting. Whenever no jobs are waiting to be run, the cores that are
// IDLE are turned off and woken up when jobs arrive.

// Attributions: I used Claude to help run measurements and help me work through
// designs that could be useful when trying to balance energy usage and speed.
// It helped me understand c++ concepts as I am unfamiliar with the language.

#include <queue>
#include "scheduler.hpp"
#include <unordered_map>

static std::queue<ProcessId_t> readyQ;
static std::unordered_map<ProcessId_t, Time_t> arrivalTime;
static double totalTurnaround = 0;
static unsigned completedProcesses = 0;

static const CPUId_t NUM_CORES = 8;
static const CPUId_t FIRST_SMALL_CORE = 4;
static const PState_t RUN_PSTATE = P3;
static const size_t BIG_CORE_QUEUE = 20;

typedef enum
{
    OFF,
    WAKING,
    IDLE,
    BUSY
} CoreState_t;
// OFF: Core is at C6
// WAKING: Core was requested at SetCState and its waiting for CStateTransitionComplete()
// IDLE: in C1
// BUSY: Running a process

struct Core
{
    CoreState_t state;
    ProcessId_t pid;
};

static Core cores[NUM_CORES];

// Called at the start to set up all the cores for the scheduler
static void InitCores_()
{
    static bool initialized = false;
    if (initialized)
    {
        return;
    }
    initialized = true;
    // Turn off all the big cores and put small cores to IDLE
    for (CPUId_t c = 0; c < NUM_CORES; c++)
    {
        cores[c].pid = InvalidProcessId();
        if (c < FIRST_SMALL_CORE)
        {
            SetCState(c, C6);
            cores[c].state = OFF;
        }
        else
        {
            cores[c].state = IDLE;
        }
    }
}

// Put waiting jobs on cores that are awake and free
static void StartJobs(CPUId_t first, CPUId_t last)
{
    for (CPUId_t c = first; c < last && !readyQ.empty(); c++)
    {
        if (cores[c].state != IDLE)
        {
            continue;
        }
        ProcessId_t pid = readyQ.front();
        readyQ.pop();
        cores[c].pid = pid;
        cores[c].state = BUSY;
        LoadContext(pid, c);
        RunCore(c);
        SetPState(c, RUN_PSTATE);
    }
}

// Make core that is off wake up
static void WakeCore(CPUId_t c)
{
    SetCState(c, C1);
    cores[c].state = WAKING;
}

// Called when something changes: start jobs that are waiting or wake more cores
static void Dispatch()
{
    StartJobs(FIRST_SMALL_CORE, NUM_CORES);
    StartJobs(0, FIRST_SMALL_CORE);

    // How many cores are in the process of waking up
    size_t waking = 0;
    for (CPUId_t c = 0; c < NUM_CORES; c++)
    {
        if (cores[c].state == WAKING)
        {
            waking++;
        }
    }

    // Wake more small cores if jobs exceed already waking cores
    for (CPUId_t c = FIRST_SMALL_CORE; c < NUM_CORES && readyQ.size() > waking; c++)
    {
        if (cores[c].state == OFF)
        {
            WakeCore(c);
            waking++;
        }
    }

    // Wake big cores only when work is piling up
    for (CPUId_t c = 0; c < FIRST_SMALL_CORE && readyQ.size() > BIG_CORE_QUEUE + waking; c++)
    {
        if (cores[c].state == OFF)
        {
            WakeCore(c);
            waking++;
        }
    }
}

void CreateProcess(ProcessId_t pid) {
    // A new process has been created. Update the scheduler's data structures and decisions accordingly.
    SimOutput("CreateProcess(" + std::to_string(pid) + ")", 4);
    InitCores_();
    arrivalTime[pid] = Now();
    readyQ.push(pid);
    Dispatch();
}

void ExitProcess(ProcessId_t pid) {
    // Process finished running. Update the scheduler's data structures and decisions accordingly.
    CPUId_t c = 0;
    while (c < NUM_CORES && !(cores[c].state == BUSY && cores[c].pid == pid))
    {
        c++;
    }
    if (c == NUM_CORES)
    {
        ThrowException("A process that was not running is calling exit!!!");
    }
    cores[c].state = IDLE;
    cores[c].pid = InvalidProcessId();
    totalTurnaround += Now() - arrivalTime[pid];
    arrivalTime.erase(pid);
    completedProcesses++;
    Dispatch();
}

void TimerInterrupt(Time_t now) {
    // You received a timer interrupt. This is where you want to execute scheduling decisions
    InitCores_();
    if (readyQ.empty())
    {
        for (CPUId_t c = 0; c < NUM_CORES; c++)
        {
            if (cores[c].state == IDLE)
            {
                SetCState(c, C6);
                cores[c].state = OFF;
            }
        }
    }
    Dispatch();
}

void CStateTransitionComplete(CPUId_t core_id){
    // A core is now in C1 and can take a job
    if (cores[core_id].state == WAKING)
    {
        cores[core_id].state = IDLE;
    }
    Dispatch();
}

void SimulationComplete(Time_t now) {
    // Add any bookkeeping or statistics that you would want to collect. Program terminates after this function returns.
    std::cout << "Run stopped at " << FormatTime(now) << " after consuming " << GetTotalEnergyConsumed()/3600000000.0 << " kWh" << std::endl;
    std::cout << "Processes completed: " << completedProcesses
              << ", average turnaround: " << (completedProcesses ? totalTurnaround / completedProcesses / 1000000.0 : 0) << " s" << std::endl;
}
