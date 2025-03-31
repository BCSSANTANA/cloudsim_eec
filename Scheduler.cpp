//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#include "Scheduler.hpp"
#include <queue>
#include <vector>
#include <algorithm>
#include <climits>

static bool migrating = false;
static unsigned active_machines;
unsigned total_machines;

// Helper structure for sorting machines by energy consumption (lowest first)
struct MachineEnergyComparator {
    bool operator()(const MachineId_t &a, const MachineId_t &b) const {
        return Machine_GetEnergy(a) < Machine_GetEnergy(b);
    }
};

// Global sorted list of machines by energy consumption.
std::vector<MachineId_t> sortedMachines;

void Scheduler::Init() {
    SimOutput("Scheduler::Init(): Total number of machines is " + to_string(Machine_GetTotal()), 3);
    SimOutput("Scheduler::Init(): Initializing scheduler", 1);
    total_machines = Machine_GetTotal();
    active_machines = total_machines;
    
    // Build the machines vector and also set up each machine's cores.
    for (unsigned i = 0; i < total_machines; i++) {
        MachineInfo_t machine_info = Machine_GetInfo(MachineId_t(i));
        machines.push_back(MachineId_t(i));
    }
    
    // Sort machines by energy consumption (lowest first)
    sortedMachines = machines;
    std::sort(sortedMachines.begin(), sortedMachines.end(), MachineEnergyComparator());
    
    // Pre-provision a number of VMs per machine.
    // Create one VM for every CPU, not sure if that's a good policy but it's a policy
    for (MachineId_t m : sortedMachines) {
        MachineInfo_t machine_info = Machine_GetInfo(m);
        unsigned numVMs = machine_info.num_cpus;
        for (unsigned j = 0; j < numVMs; j++) {
            VMId_t vm = VM_Create(LINUX, machine_info.cpu);
            vms.push_back(vm);
            VM_Attach(vm, m);
        }
    }
    
    SimOutput("Scheduler::Init(): Initialized " + to_string(vms.size()) + " VMs across " + to_string(total_machines) + " machines.", 3);
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    SimOutput("Scheduler::MigrationComplete(): VM " + to_string(vm_id) +
              " migration completed at time " + to_string(time), 3);
    
}

//ChatGPT helped fill in the gaps not covered in the class slides
//Also important to note that our pmapper differs from the class slides because it does not attempt to shutdown any machines since there was no policy mentioned for bringing them back up when needed (and its more interesting to compare the energy use to other algos if its migrating tasks all the time), furthermore to differentiate it from other algos for comparison we elect to try and make a new VM for each task first THEN assign tasks to an existing VM once the max number of VMs have been created.
void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    bool taskAssigned = false;
    TaskInfo_t taskInfo = GetTaskInfo(task_id);
    Priority_t prio = (taskInfo.required_sla == SLA0) ? HIGH_PRIORITY :
                      (taskInfo.required_sla == SLA3) ? LOW_PRIORITY : MID_PRIORITY;
    
    // pMapper: assign tasks based on machines sorted by energy consumption.
    // Iterate over sortedMachines to find the first machine that can take the task.
    for (MachineId_t m : sortedMachines) {
        MachineInfo_t mInfo = Machine_GetInfo(m);

        if (mInfo.cpu != taskInfo.required_cpu)
            continue;

        // Skip machines that are not active.
        if (mInfo.s_state == S5)
            continue;

        // Check if the machine has enough free memory for this task.
        if (mInfo.memory_size - mInfo.memory_used >= taskInfo.required_memory + 100) {
            VMId_t new_vm = VM_Create(LINUX, mInfo.cpu);
                    vms.push_back(new_vm);
                    VM_Attach(new_vm, m);
                    try {
                        VM_AddTask(new_vm, task_id, prio);
                        taskAssigned = true;
                        SimOutput("NewTask(): Created new VM " + to_string(new_vm) + " on machine " + to_string(m) + " and assigned task " + to_string(task_id), 1);
                        break;
                    } catch (const std::exception &e) {
                        SimOutput("NewTask(): Failed to add task " + to_string(task_id) + " to new VM: " + e.what(), 1);
                    }
            // Try to find a VM on this machine with capacity.
            bool foundVM = false;
            for (VMId_t vm : vms) {
                VMInfo_t vmInfo = VM_GetInfo(vm);
                if (vmInfo.machine_id == m && 
                    (mInfo.memory_size - mInfo.memory_used >= taskInfo.required_memory)) {
                    try {
                        VM_AddTask(vm, task_id, prio);
                        taskAssigned = true;
                        foundVM = true;
                        SimOutput("NewTask(): Assigned task " + to_string(task_id) + " to VM " + to_string(vm) + " on machine " + to_string(m), 1);
                        break;
                    } catch (const std::exception &e) {
                        // If VM not ready, try next one.
                        SimOutput("NewTask(): Failed to add task " + to_string(task_id) + " to VM " + to_string(vm) + ": " + e.what(), 1);
                    }
                }
            }
            if (!foundVM) {
                // No existing VM on machine m can take the task, try to create one if possible.
                if (mInfo.memory_size - mInfo.memory_used >= taskInfo.required_memory + 10) {
                    VMId_t new_vm = VM_Create(LINUX, mInfo.cpu);
                    vms.push_back(new_vm);
                    VM_Attach(new_vm, m);
                    try {
                        VM_AddTask(new_vm, task_id, prio);
                        taskAssigned = true;
                        SimOutput("NewTask(): Created new VM " + to_string(new_vm) + " on machine " + to_string(m) + " and assigned task " + to_string(task_id), 1);
                    } catch (const std::exception &e) {
                        SimOutput("NewTask(): Failed to add task " + to_string(task_id) + " to new VM: " + e.what(), 1);
                    }
                    break; // Exit after trying one machine.
                }
            }
            if (taskAssigned) break;
        }
    }
    
    if (!taskAssigned) {
        SimOutput("NewTask(): FAILED to assign Task " + to_string(task_id) + " -- SLA violation", 1);
    }
}


void Scheduler::PeriodicCheck(Time_t now) {
    // Monitor and log overall machine utilization.
    double totalUtilization = 0;
    for (MachineId_t m : machines) {
        MachineInfo_t mInfo = Machine_GetInfo(m);
        if (mInfo.s_state == S5)
            continue;
        double utilization = double(mInfo.memory_used) / mInfo.memory_size;
        totalUtilization += utilization;
    }
    double avgUtilization = totalUtilization / machines.size();
    SimOutput("PeriodicCheck(): Average machine utilization: " + to_string(avgUtilization), 3);
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

//ChatGPT helped fill in the gaps not covered in the class slides
void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    SimOutput("Scheduler::TaskComplete(): Task " + to_string(task_id) + " is complete at " + to_string(now), 4);
    
    // pMapper rebalancing upon workload completion:
    // 1. Compute utilization for each machine (utilization = memory_used / memory_size since not really defined)
    std::vector<std::pair<MachineId_t, double>> machineUtil;
    for (MachineId_t m : machines) {
        MachineInfo_t mInfo = Machine_GetInfo(m);
        if (mInfo.s_state == S5)
            continue;
        double utilization = double(mInfo.memory_used) / mInfo.memory_size;
        machineUtil.push_back(std::make_pair(m, utilization));
    }
    
    // 2. Sort machines by utilization
    std::sort(machineUtil.begin(), machineUtil.end(), [](const auto &a, const auto &b) {
        return a.second < b.second;
    });
    
    // Divide machines into two halves:
    size_t midIndex = machineUtil.size() / 2;
    std::vector<MachineId_t> lowUtilMachines, highUtilMachines;
    for (size_t i = 0; i < machineUtil.size(); i++) {
        if (i < midIndex)
            lowUtilMachines.push_back(machineUtil[i].first);
        else
            highUtilMachines.push_back(machineUtil[i].first);
    }
    
    // 3. From the least utilized machine, select the smallest workload (by remaining instructions)
    VMId_t candidateVM = 0;
    TaskId_t candidateTask = 0;
    unsigned minWorkload = UINT_MAX;
    for (MachineId_t m : lowUtilMachines) {
        for (VMId_t vm : vms) {
            VMInfo_t vmInfo = VM_GetInfo(vm);
            if (vmInfo.machine_id == m && !vmInfo.active_tasks.empty()) {
                for (TaskId_t t : vmInfo.active_tasks) {
                    TaskInfo_t tInfo = GetTaskInfo(t);
                    if (tInfo.remaining_instructions < minWorkload) {
                        minWorkload = tInfo.remaining_instructions;
                        candidateTask = t;
                        candidateVM = vm;
                    }
                }
            }
        }
    }
    
    // 4. Migrate the candidate workload to one of the highly utilized machines to consolidate load (I think this step and the overhead of migration is what's slowing this algo down, but that's pmapper!)
    if (candidateTask != 0 && !highUtilMachines.empty()) {
        MachineId_t targetMachine = highUtilMachines.front();
        try {
            // Migrate the VM that is hosting candidateTask to the target machine.
            VM_Migrate(candidateVM, targetMachine);
            SimOutput("TaskComplete(): Migrated VM " + to_string(candidateVM) + " (carrying task " + to_string(candidateTask) + ") to machine " + to_string(targetMachine), 1);
        } catch (const std::exception &e) {
            SimOutput("TaskComplete(): Migration failed: " + string(e.what()), 1);
        }
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
    migrating = false;
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
    
}

void StateChangeComplete(Time_t time, MachineId_t machine_id) {
    // Called in response to an earlier request to change the state of a machine
}

