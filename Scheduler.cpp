//
//  Scheduler.cpp
//  CloudSim
//
//  Created by ELMOOTAZBELLAH ELNOZAHY on 10/20/24.
//

#include "Scheduler.hpp"
#include <queue>
#include <vector>


static unsigned total_machines;
static unsigned active_machines;

void Scheduler::Init() {
    // Find the parameters of the clusters
    // Get the total number of machines
    // For each machine:
    //      Get the type of the machine
    //      Get the memory of the machine
    //      Get the number of CPUs
    //      Get if there is a GPU or not
    // 
    total_machines = Machine_GetTotal();
    SimOutput("Scheduler::Init(): Total number of machines is " + to_string(Machine_GetTotal()), 3);
    SimOutput("Scheduler::Init(): Initializing scheduler", 1);
    for (unsigned i = 0; i < total_machines; i++) {  
        MachineId_t m = MachineId_t(i);
        Machine_SetState(m, S0);
        machines.push_back(m);    }
    SimOutput("Scheduler::Init(): Total number of active machines is: " + to_string(active_machines), 1);
}

void Scheduler::MigrationComplete(Time_t time, VMId_t vm_id) {
    // Update your data structure. The VM now can receive new tasks
}



void Scheduler::NewTask(Time_t now, TaskId_t task_id) {
    TaskInfo_t taskInfo = GetTaskInfo(task_id);
    
    // only one task per VM so HIGH_PRIORITY is assigned

    Priority_t prio = LOW_PRIORITY;
    if (taskInfo.required_sla == SLA0) {
        prio = HIGH_PRIORITY;
    }
    else if (taskInfo.required_sla == SLA3) {
        prio = LOW_PRIORITY;
    }
    else {
        prio = MID_PRIORITY;
    }
    CPUType_t required_cpu = taskInfo.required_cpu;
    VMType_t required_vm = taskInfo.required_vm;
    unsigned required_memory = taskInfo.required_memory;
    bool task_added = false;
    
    for (VMId_t vm : vms) {
        VMInfo_t vm_info = VM_GetInfo(vm);
        if (vm_info.vm_type == required_vm && vm_info.cpu == required_cpu) {
            MachineId_t machine = vm_info.machine_id;
            MachineInfo_t machine_info = Machine_GetInfo(machine);
            if (machine_info.memory_used  + required_memory < machine_info.memory_size &&
                machine_info.s_state != S5) {
                try {
                    VM_AddTask(vm, task_id, prio);
                    SimOutput("Scheduler::NewTask(): Task " + to_string(task_id) + " added to VM " +
                              to_string(vm) + " on machine " + to_string(machine), 4);
                    task_added = true;

                } catch (const std::exception &e) {
                    SimOutput("NewTask(): Failed to add task " + to_string(task_id) +
                              " to VM " + to_string(vm) + ": " + e.what(), 1);
                }
                break;
            }
        }
    }
    
    if (!task_added) {
        for (MachineId_t machine : machines) {
            MachineInfo_t machine_info = Machine_GetInfo(machine);
            if (machine_info.cpu == required_cpu && machine_info.memory_used + required_memory <  machine_info.memory_size) {
                bool just_turned_on = false;
                // If Machine is off, turn it on. Takes a while to chage state, so look for another machine
                if (machine_info.s_state == S5) {
                    Machine_SetState(machine, S0);
                    SimOutput("Scheduler::NewTask(): Machine " + to_string(machine) + " is turned on at time " + to_string(now), 1);
                    just_turned_on = true;
                }
               if (!just_turned_on) {
                VMId_t vm_id = VM_Create(required_vm, required_cpu);
                VM_Attach(vm_id, machine);
                vms.push_back(vm_id);
                VM_AddTask(vm_id, task_id, prio);
                task_added = true;
                break;
               }
            }
        }
    }
    
    if (!task_added) {
        SimOutput("Scheduler::NewTask(): No available capacity for task " +
                  to_string(task_id), 4);
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

    // TO DO: if VM can be turned off
    // Check to see if a task can be removed from the VM

    // Policy free VM and power down the machine
    static unsigned count = 0;
    for (auto it = vms.begin(); it != vms.end(); ) {
        VMId_t vm_id = *it;
        VMInfo_t vm_info = VM_GetInfo(vm_id);
        if (vm_info.active_tasks.size() == 0) {
            MachineId_t machine_id = vm_info.machine_id;
            VM_Shutdown(vm_id);
            SimOutput("Scheduler::TaskComplete(): VM " + to_string(vm_id) +
                      " is removed from machine " + to_string(machine_id), 1);
            // Remove this VM and move iterator to the next valid element
            it = vms.erase(it);
        } else {
            ++it;
        }
    }

    // Check to see if a machine can be turned off
    if (count == (total_machines / 2)) {
        for (MachineId_t machine : machines) {
            MachineInfo_t machine_info = Machine_GetInfo(machine);
            unsigned active_tasks = machine_info.active_tasks;
            if (active_tasks == 0 && machine_info.memory_used == 0 &&
                machine_info.s_state != S5) {
                Machine_SetState(machine, S5);
                SimOutput("Scheduler::TaskComplete(): Machine " + to_string(machine) + " is  off at time " + to_string(now), 1);
            }
        }
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
}

void SchedulerCheck(Time_t time) {
    // This function is called periodically by the simulator, no specific event
    SimOutput("SchedulerCheck(): SchedulerCheck() called at " + to_string(time), 4);
    static unsigned counts = 0;
    counts++;
    if (counts == 10) {
        Scheduler.PeriodicCheck(time);

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
    // Called in response to an earlier request to change the state of a machine.
    MachineInfo_t machine_info = Machine_GetInfo(machine_id);
    if (machine_info.s_state == S5) {
        active_machines--;
        SimOutput("StateChangeComplete(): Machine " + to_string(machine_id) + " is off at time " + to_string(time), 1);
    }
    else if (machine_info.s_state == S0) {
        active_machines++;
        SimOutput("StateChangeComplete(): Machine " + to_string(machine_id) + " is on at time " + to_string(time), 1);
    }
}