//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#include "Scheduler.hpp"
#include <algorithm>
#include <vector>
#include <climits>
#include <iostream>

// Global variables.
static unsigned total_machines;
static unsigned active_machines;

void Scheduler::Init() {
    SimOutput("Scheduler::Init(): Total number of machines is " + to_string(Machine_GetTotal()), 3);
    total_machines = Machine_GetTotal();
    active_machines = total_machines;
    
    // Build machines list.
    for (unsigned i = 0; i < total_machines; i++) {
        MachineId_t m = MachineId_t(i);
        machines.push_back(m);
    }
    
    // Pre-provision VMs on each machine.
    for (MachineId_t m : machines) {
        MachineInfo_t mInfo = Machine_GetInfo(m);
        // Policy: Create one VM per CPU.
        unsigned numVMs = mInfo.num_cpus;
        for (unsigned j = 0; j < numVMs; j++) {
            VMId_t vm = VM_Create(LINUX, mInfo.cpu);
            vms.push_back(vm);
            VM_Attach(vm, m);
        }
    }
    
    SimOutput("Scheduler::Init(): Initialized " + to_string(vms.size()) +
              " VMs across " + to_string(total_machines) + " machines.", 3);
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    SimOutput("Scheduler::MigrationComplete(): VM " + to_string(vm_id) +
              " migration completed at time " + to_string(time), 3);
    
}

//ChatGPT helped fill in the gaps not covered in the class slides
// Scheduler::NewTask
// For each new workload, scan through all machines (m) in order:
//   For each machine j, compute:
//       u = memory_used / memory_size   (current utilization)
//       v = required_memory / memory_size  (load factor for the task)
//   If u + v < 1, place workload i in a VM on machine j and break out.
//   If no machine can accommodate the workload, record an SLA violation.
// After processing new requests, if any machine has u = 0, turn it off.
//
void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    bool taskPlaced = false;
    TaskInfo_t taskInfo = GetTaskInfo(task_id);
    
    // Iterate over machines in the order of sortedMachines.
    for (MachineId_t m : machines) {
        MachineInfo_t mInfo = Machine_GetInfo(m);
        // Skip if the machine's CPU type is incompatible or machine is off.
        if (mInfo.cpu != taskInfo.required_cpu || mInfo.s_state == S5)
            continue;
        
        // Compute current utilization (u) and load factor (v).
        double u = double(mInfo.memory_used) / mInfo.memory_size;
        double v = double(taskInfo.required_memory) / mInfo.memory_size;
        
        if (u + v < 1.0) {
            // Try to find an existing VM on machine m.
            bool foundVM = false;
            for (VMId_t vm : vms) {
                VMInfo_t vmInfo = VM_GetInfo(vm);
                if (vmInfo.machine_id == m) {
                    try {
                        VM_AddTask(vm, task_id, (taskInfo.required_sla == SLA0) ? HIGH_PRIORITY : MID_PRIORITY);
                        foundVM = true;
                        taskPlaced = true;
                        SimOutput("NewTask(): Placed task " + to_string(task_id) +
                                  " in VM " + to_string(vm) + " on machine " + to_string(m), 1);
                        break;
                    } catch (const std::exception &e) {
                        SimOutput("NewTask(): Failed to add task " + to_string(task_id) +
                                  " to VM " + to_string(vm) + ": " + e.what(), 1);
                    }
                }
            }
            // If no VM exists on m, attempt to create a new one if there's capacity.
            if (!foundVM && (mInfo.memory_size - mInfo.memory_used >= taskInfo.required_memory + VM_MEMORY_OVERHEAD)) {
                VMId_t new_vm = VM_Create(LINUX, mInfo.cpu);
                vms.push_back(new_vm);
                VM_Attach(new_vm, m);
                try {
                    VM_AddTask(new_vm, task_id, (taskInfo.required_sla == SLA0) ? HIGH_PRIORITY : MID_PRIORITY);
                    taskPlaced = true;
                    SimOutput("NewTask(): Created new VM " + to_string(new_vm) +
                              " on machine " + to_string(m) + " and placed task " + to_string(task_id), 1);
                } catch (const std::exception &e) {
                    SimOutput("NewTask(): Failed to add task " + to_string(task_id) +
                              " to new VM: " + e.what(), 1);
                }
            }
            if (taskPlaced)
                break; // Task placed; exit loop.
        }
    }
    
    if (!taskPlaced) {
        SimOutput("NewTask(): FAILED to place task " + to_string(task_id) + " -- SLA violation", 1);
    }
}

void Scheduler::PeriodicCheck(Time_t now) {
    // Monitor and log overall machine utilization.
    double totalUtil = 0;
    unsigned count = 0;
    for (MachineId_t m : machines) {
        MachineInfo_t mInfo = Machine_GetInfo(m);
        if (mInfo.s_state != S0)
            continue;
        double u = double(mInfo.memory_used) / mInfo.memory_size;
        totalUtil += u;
        count++;
    }
    double avgUtil = (count > 0) ? totalUtil / count : 0;
    SimOutput("PeriodicCheck(): Average machine utilization: " + to_string(avgUtil), 3);
}

void Scheduler::Shutdown(Time_t time) {
    // Do your final reporting and bookkeeping here.
    // Report about the total energy consumed
    // Report about the SLA compliance
    // Shutdown everything to be tidy :-)
    for(auto & vm: vms) {
        VM_Shutdown(vm);
    }
    SimOutput("SimulationComplete(): Finished!", 4);
    SimOutput("SimulationComplete(): Time is " + to_string(time), 4);
}

// Scheduler::TaskComplete
// Greedy Allocation on slides had a check for migration every time a task completed but that was slowing things down and I think it's more interesting to compare it this way to the other algo from the slides (pMapper) which we do have migrating workloads after task complete
void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    SimOutput("TaskComplete(): Task " + to_string(task_id) + " completed at time " + to_string(now), 4);
}

// SLAWarning
// ChatGPT helped fill in the gaps not covered in the class slides
// When a workload violates its SLA on a machine J, we:
//   - Sort all active machines (except J) by utilization (ascending order).
//   - Compute the load factor v for the violating workload.
//   - Find a candidate machine that can accommodate v (u + v < 1).
//   - If found, migrate the workload (via its VM) to that machine; otherwise, log failure.
// (This just gets called by the one in the public interface, that way we didn't have to copy over vms and machines)
void Scheduler::SLAViolation(Time_t time, TaskId_t task_id) {
    TaskInfo_t tInfo = GetTaskInfo(task_id);
    MachineId_t machineJ = 0;
    VMId_t vmHosting = 0;
    // Identify machineJ and the VM hosting workload i.
    for (VMId_t vm : vms) {
        VMInfo_t vmInfo = VM_GetInfo(vm);
        for (TaskId_t t : vmInfo.active_tasks) {
            if (t == task_id) {
                machineJ = vmInfo.machine_id;
                vmHosting = vm;
                break;
            }
        }
        if (machineJ != 0)
            break;
    }
    if (machineJ == 0) {
        SimOutput("SLAWarning(): Task " + to_string(task_id) + " not found.", 1);
        return;
    }
    
    // Sort all active machines (except machineJ) by utilization.
    std::vector<std::pair<MachineId_t, double>> utilList;
    for (MachineId_t m : machines) {
        if (m == machineJ)
            continue;
        MachineInfo_t mInfo = Machine_GetInfo(m);
        if (mInfo.s_state != S0)
            continue;
        double u = double(mInfo.memory_used) / mInfo.memory_size;
        utilList.push_back({m, u});
    }
    std::sort(utilList.begin(), utilList.end(), [](auto &a, auto &b) {
        return a.second < b.second;
    });
    
    // Compute load factor v for task_id on machineJ.
    MachineInfo_t mJInfo = Machine_GetInfo(machineJ);
    double v = double(tInfo.required_memory) / mJInfo.memory_size;
    
    bool migrated = false;
    for (auto &p : utilList) {
        MachineId_t candidate = p.first;
        MachineInfo_t candInfo = Machine_GetInfo(candidate);
        if (candInfo.s_state != S0)
            continue;
        double u_candidate = double(candInfo.memory_used) / candInfo.memory_size;
        if (u_candidate + v < 1.0) {
            try {
                VM_Migrate(vmHosting, candidate);
                SimOutput("SLAWarning(): Migrated task " + to_string(task_id) +
                          " from machine " + to_string(machineJ) + " to machine " + to_string(candidate), 1);
                migrated = true;
                break;
            } catch (const std::exception &e) {
                SimOutput("SLAWarning(): Migration failed for task " + to_string(task_id) +
                          " to machine " + to_string(candidate) + ": " + e.what(), 1);
            }
        }
    }
    if (!migrated) {
        // Attempt standby: look for a machine in S5 with matching CPU type.
        MachineId_t standbyCandidate = 0;
        for (MachineId_t m : machines) {
            MachineInfo_t mInfo = Machine_GetInfo(m);
            if (mInfo.s_state == S5 && mInfo.cpu == mJInfo.cpu) {
                standbyCandidate = m;
                break;
            }
        }
        if (standbyCandidate != 0) {
            Machine_SetState(standbyCandidate, S0); // Wake up standby machine.
            MachineInfo_t standbyInfo = Machine_GetInfo(standbyCandidate);
            double u_standby = double(standbyInfo.memory_used) / standbyInfo.memory_size;
            if (u_standby + v < 1.0) {
                try {
                    VM_Migrate(vmHosting, standbyCandidate);
                    SimOutput("SLAWarning(): Migrated task " + to_string(task_id) +
                              " from machine " + to_string(machineJ) + " to standby machine " + to_string(standbyCandidate), 1);
                    migrated = true;
                } catch (const std::exception &e) {
                    SimOutput("SLAWarning(): Migration to standby machine " + to_string(standbyCandidate) +
                              " failed: " + e.what(), 1);
                }
            }
        }
    }
    if (!migrated) {
        SimOutput("SLAWarning(): FAILED to migrate task " + to_string(task_id) + " after SLA violation", 1);
    }
}

// Public interface below

static Scheduler Scheduler;

void InitScheduler() {
    SimOutput("InitScheduler(): Initializing scheduler", 4);
    Scheduler.Init();
}

void HandleNewTask(Time_t time, TaskId_t task_id) {
    SimOutput("HandleNewTask(): Received new task " + to_string(task_id) + " at time " + to_string(time), 4);
    Scheduler.NewTask(time, task_id);
}

void HandleTaskCompletion(Time_t time, TaskId_t task_id) {
    SimOutput("HandleTaskCompletion(): Task " + to_string(task_id) + " completed at time " + to_string(time), 4);
    Scheduler.TaskComplete(time, task_id);
}

void MemoryWarning(Time_t time, MachineId_t machine_id) {
    // The simulator is alerting you that machine identified by machine_id is overcommitted
    SimOutput("MemoryWarning(): Overflow at " + to_string(machine_id) + " was detected at time " + to_string(time), 0);
}

void MigrationDone(Time_t time, VMId_t vm_id) {
    // The function is called on to alert you that migration is complete
    SimOutput("MigrationDone(): Migration of VM " + to_string(vm_id) + " was completed at time " + to_string(time), 4);
    Scheduler.MigrationComplete(time, vm_id);
}

void SchedulerCheck(Time_t time) {
    // This function is called periodically by the simulator, no specific event
    SimOutput("SchedulerCheck(): SchedulerCheck() called at " + to_string(time), 4);
    Scheduler.PeriodicCheck(time);
    static unsigned counts = 0;
    counts++;
    /*
    if(counts == 10) {
        migrating = true;
        VM_Migrate(1, 9);
    }
    */
}

void SimulationComplete(Time_t time) {
    // This function is called before the simulation terminates Add whatever you feel like.
    cout << "SLA violation report" << endl;
    cout << "SLA0: " << GetSLAReport(SLA0) << "%" << endl;
    cout << "SLA1: " << GetSLAReport(SLA1) << "%" << endl;
    cout << "SLA2: " << GetSLAReport(SLA2) << "%" << endl;     // SLA3 do not have SLA violation issues
    cout << "Total Energy " << Machine_GetClusterEnergy() << "KW-Hour" << endl;
    cout << "Simulation run finished in " << double(time)/1000000 << " seconds" << endl;
    SimOutput("SimulationComplete(): Simulation finished at time " + to_string(time), 4);
    
    Scheduler.Shutdown(time);
}

void SLAWarning(Time_t time, TaskId_t task_id) {
    Scheduler.SLAViolation(time, task_id);
}

void StateChangeComplete(Time_t time, MachineId_t machine_id) {
    // Called in response to an earlier request to change the state of a machine
}

