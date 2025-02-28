# Input.md file for CS 378 Energy-Efficient Computing Semester Project
# This file defines a cluster with mid, low, and high-performance X86 machines
# and tasks with varying SLA requirements to test low and high load scenarios.

# Mid-performance X86 machine class
machine class:
{
    Number of machines: 8
    CPU type: X86
    Number of cores: 4
    Memory: 8192
    S-States: [100, 80, 80, 60, 30, 5, 0]
    P-States: [10, 7, 5, 3]
    C-States: [10, 2, 1, 0]
    MIPS: [800, 600, 400, 200]
    GPUs: no
}

# Low-performance X86 machine class
machine class:
{
    Number of machines: 12
    CPU type: X86
    Number of cores: 2
    Memory: 4096
    S-States: [80, 60, 60, 40, 20, 5, 0]
    P-States: [8, 6, 4, 2]
    C-States: [8, 2, 1, 0]
    MIPS: [500, 400, 300, 150]
    GPUs: no
}

# High-performance X86 machine class
machine class:
{
    Number of machines: 4
    CPU type: X86
    Number of cores: 8
    Memory: 16384
    S-States: [150, 120, 120, 100, 50, 10, 0]
    P-States: [15, 12, 9, 6]
    C-States: [15, 3, 1, 0]
    MIPS: [1200, 1000, 800, 600]
    GPUs: yes
}

# Task class for SLA0 (strictest SLA: 95% within 1.2x expected runtime)
task class:
{
    Start time: 50000
    End time: 1000000
    Inter arrival: 10000
    Expected runtime: 500000
    Memory: 512
    VM type: LINUX
    GPU enabled: no
    SLA type: SLA0
    CPU type: X86
    Task type: WEB
    Seed: 123456
}

# Task class for SLA1 (90% within 1.5x expected runtime)
task class:
{
    Start time: 60000
    End time: 1200000
    Inter arrival: 20000
    Expected runtime: 800000
    Memory: 1024
    VM type: LINUX
    GPU enabled: yes
    SLA type: SLA1
    CPU type: X86
    Task type: AI
    Seed: 654321
}

# Task class for SLA2 (80% within 2.0x expected runtime)
task class:
{
    Start time: 70000
    End time: 1500000
    Inter arrival: 30000
    Expected runtime: 1000000
    Memory: 2048
    VM type: LINUX
    GPU enabled: no
    SLA type: SLA2
    CPU type: X86
    Task type: STREAM
    Seed: 987654
}

# Task class for SLA3 (best effort basis)
task class:
{
    Start time: 80000
    End time: 2000000
    Inter arrival: 50000
    Expected runtime: 1500000
    Memory: 256
    VM type: LINUX
    GPU enabled: no
    SLA type: SLA3
    CPU type: X86
    Task type: WEB
    Seed: 456789
}

# Edited with Grok3