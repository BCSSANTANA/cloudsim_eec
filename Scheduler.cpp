//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#include "Scheduler.hpp"
#include <queue>
#include <vector>

static bool migrating = false;
static unsigned active_machines = 16;
unsigned total_machines;

// Custom comparator for VMs based on the number of active tasks.
struct VMComparator {
    bool operator()(const VMId_t& a, const VMId_t& b) const {
        // Retrieve the VM info for both VMs.
        VMInfo_t infoA = VM_GetInfo(a);
        VMInfo_t infoB = VM_GetInfo(b);
        
        // Calculate total workload for each VM (using remaining instructions as an example).
        unsigned workloadA = 0;
        for (TaskId_t task : infoA.active_tasks) {
            TaskInfo_t taskInfo = GetTaskInfo(task);
            workloadA += taskInfo.remaining_instructions;  // or total_instructions, depending on your strategy
        }
        
        unsigned workloadB = 0;
        for (TaskId_t task : infoB.active_tasks) {
            TaskInfo_t taskInfo = GetTaskInfo(task);
            workloadB += taskInfo.remaining_instructions;
        }
        
        // We want the VM with lower total workload to have higher priority (min-heap).
        return workloadA > workloadB;
    }
};

std::priority_queue<VMId_t, std::vector<VMId_t>, VMComparator> vmQueue;

void Scheduler::Init() {
    // Find the parameters of the clusters
    // Get the total number of machines
    // For each machine:
    //      Get the type of the machine
    //      Get the memory of the machine
    //      Get the number of CPUs
    //      Get if there is a GPU or not
    // 
    SimOutput("Scheduler::Init(): Total number of machines is " + to_string(Machine_GetTotal()), 3);
    SimOutput("Scheduler::Init(): Initializing scheduler", 1);
    total_machines = Machine_GetTotal();
    for(unsigned i = 0; i < total_machines; i++) {
        MachineInfo_t machine_info = Machine_GetInfo(MachineId_t(i));
        for (unsigned k = 0; k < machine_info.num_cpus; k++) { //does this make a difference?
            Machine_SetCorePerformance(MachineId_t(i), k, CPUPerformance_t(P0)); 
        }
        machines.push_back(MachineId_t(i));
        for (unsigned j = 0; j < machine_info.num_cpus; j++) { 
            VMId_t vm = VM_Create(LINUX, machine_info.cpu);
            vms.push_back(vm);
            VM_Attach(vm, machines[i]);
        }
    }

    for (VMId_t vm : vms) {
        vmQueue.push(vm);
    }

    
    /*
    bool dynamic = false;
    if(dynamic)
        for(unsigned i = 0; i<4 ; i++)
            for(unsigned j = 0; j < 8; j++)
                Machine_SetCorePerformance(MachineId_t(0), j, P3);
    // Turn off the ARM machines
    for(unsigned i = 24; i < Machine_GetTotal(); i++)
        Machine_SetState(MachineId_t(i), S5);
    */
    SimOutput("Scheduler::Init(): VM ids are " + to_string(vms[0]) + " and " + to_string(vms[1]), 3);
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    // Update your data structure. The VM now can receive new tasks
}

void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    // Get the task parameters
    //  IsGPUCapable(task_id);
    //  GetMemory(task_id);
    //  RequiredVMType(task_id);
    //  RequiredSLA(task_id);
    //  RequiredCPUType(task_id);
    // Decide to attach the task to an existing VM, 
    //      vm.AddTask(taskid, Priority_T priority); or
    // Create a new VM, attach the VM to a machine
    //      VM vm(type of the VM)
    //      vm.Attach(machine_id);
    //      vm.AddTask(taskid, Priority_t priority) or
    // Turn on a machine, create a new VM, attach it to the VM, then add the task
    //
    // Turn on a machine, migrate an existing VM from a loaded machine....
    //
    // Other possibilities as desired
    bool taskAssigned = false;
    TaskInfo_t taskInfo = GetTaskInfo(task_id);
    Priority_t prio;
    if (taskInfo.required_sla == SLA0) {
        prio = HIGH_PRIORITY;
    }
    else if (taskInfo.required_sla == SLA3) {
        prio = LOW_PRIORITY;
    }
    else {
        prio = MID_PRIORITY;
    }
    VMType_t required_vmType = RequiredVMType(task_id);
    CPUType_t required_cpuType = RequiredCPUType(task_id);
    while (!vmQueue.empty()) {
        vmQueue.pop();
    }
    // Re-push all VMs so that the queue is sorted by the current active task counts.
    for (VMId_t vm : vms) {
        vmQueue.push(vm);
    }
    //make a prio queue of VMs based on number of active tasks
    if (!vmQueue.empty()) {
        // The top of the queue is the least busy VM.
        VMId_t bestVM = vmQueue.top();
        try {
            VM_AddTask(bestVM, task_id, prio);
            taskAssigned = true;
            SimOutput("Added task " + to_string(task_id) + " to existing VM ", 1);
        } catch (const std::exception &e) {
            SimOutput("Failed to add task " + to_string(task_id) + " to VM " +
                      to_string(bestVM) + ": " + e.what(), 1);
        }
    }
    if (!taskAssigned) {
        MachineId_t least_full_machine;
        unsigned lowest_used_mem;
        for (MachineId_t machine : machines) {
            unsigned used_mem = Machine_GetInfo(machine).active_vms; //should we go based on active vms or memory?
            if (used_mem < lowest_used_mem) {
                least_full_machine = machine;
                lowest_used_mem = used_mem;
            }
        }
        if (Machine_GetInfo(least_full_machine).memory_size - Machine_GetInfo(least_full_machine).memory_used > 100 && Machine_GetInfo(least_full_machine).cpu == required_cpuType) {
            VMId_t new_vm = VM_Create(LINUX, required_cpuType); //overflow from creating a VM on a machine that doesn't have space? creates problems trying to create a cputype on a machine that doesn't have that?
            vms.push_back(new_vm);
            VM_Attach(new_vm, least_full_machine);
            if (Machine_GetInfo(least_full_machine).memory_size - Machine_GetInfo(least_full_machine).memory_used >= GetTaskInfo(task_id).required_memory) {
                VM_AddTask(new_vm, task_id, prio);
                taskAssigned = true;
            }
        }
    }
}

void Scheduler::PeriodicCheck(Time_t now) {
    // This method should be called from SchedulerCheck()
    // SchedulerCheck is called periodically by the simulator to allow you to monitor, make decisions, adjustments, etc.
    // Unlike the other invocations of the scheduler, this one doesn't report any specific event
    // Recommendation: Take advantage of this function to do some monitoring and adjustments as necessary
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

void Scheduler::TaskComplete(Time_t now, TaskId_t task_id) {
    // Do any bookkeeping necessary for the data structures
    // Decide if a machine is to be turned off, slowed down, or VMs to be migrated according to your policy
    // This is an opportunity to make any adjustments to optimize performance/energy
    // Clear the current priority queue
    while (!vmQueue.empty()) {
        vmQueue.pop();
    }
    // Re-push all VMs so that the queue is sorted by the current active task counts.
    for (VMId_t vm : vms) {
        vmQueue.push(vm);
    }
    SimOutput("Scheduler::TaskComplete(): Task " + to_string(task_id) + " is complete at " + to_string(now), 4);
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
    if(counts == 10) {
        migrating = true;
        VM_Migrate(1, 9);
    }
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

