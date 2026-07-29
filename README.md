# Description
This project is an educational reverse-engineering project that implements a hypervisor-assisted runtime memory analysis for a protected 3D Windows application.  
The goal is to learn how to identify specific runtime data (e.g., world-space positions of entities) in memory under anti-debugging and protection mechanisms by intercepting guest memory access via EPT violations.
This project was conducted on a personal test environment and was used solely for educational analysis of protected processes and visualization techniques.

# Hypervisor
![image](https://github.com/user-attachments/assets/8f9f1214-fff0-4f37-a044-aaf98f18e9b9)

A hypervisor (also called a virtual machine monitor or VMM) is a software layer that allows different operating systems to run concurrently on the same hardware while remaining isolated from each other.

- **Why use the hypervisor:**
    - Because the hypervisor operates below the Windows kernel, the analyzed application cannot directly detect or interfere with it.
    - The hypervisor effectively functions as an out-of-guest debugger, enabling observation of a protected process even when in-guest debuggers are restricted by anti-debugging mechanisms.
        - Debugging using **Vectored Exception Handling** after DLL injection is blocked by anti-debugging mechanisms.
        - Debugging using **Win32 debugging APIs** is also blocked.
- **What the hypervisor does:**  
  The hypervisor exposes CPU registers when EPT (Extended Page Table) violations occur at specific guest-physical memory addresses, in a similar way that traditional debuggers expose registers at breakpoints.
- **What EPT Violation (Extended Page Table Violation) means:**  
  An EPT violation is a type of page fault that occurs in Intel VT-x virtualization when a guest virtual machine (VM) attempts to access memory in a way that violates the EPT permissions configured by the hypervisor.

The hypervisor used in this project targets Intel CPUs (VT-x with EPT).

# Steps to track specific information

In this prototype, the “specific information” is the world-space positions of entities (e.g., the local player and other actors) inside a 3D application.

## 1. Find the memory addresses where position information is stored

`scanner.cpp` and `candidate_analyzer.cpp` are used to scan the target process memory and identify candidate addresses that likely store 3D position data.

Candidate addresses are filtered using conditions such as:

- Three contiguous `float` values (x, y, z).
- Values staying within a valid numeric range, e.g. `[−123456, 123456]`.
- Non-zero values.
- Values changing in a correlated way when the entity’s position changes in the application.

For the local entity, an additional value representing the facing direction (e.g. in the range `[-1, 1]`) is needed along with the 3D coordinates to perform the **World To Screen** computation described below.  
This facing direction is likely stored in the same struct as the 3D coordinates.

## 2. Use the hypervisor to find the assembly instructions executed when the identified memory addresses are accessed

The memory addresses found in step 1 are dynamically allocated and change after the process restarts, so they cannot be used as stable identifiers.  
To make the tracking robust, this step identifies the instructions that access those addresses.

![image](https://github.com/user-attachments/assets/20aa7248-e6e2-42f4-af05-42e0bd7d0ebd)

1. Run the hypervisor device driver.
2. Execute `./MyHypervisorApp.exe [process ID] [memory address] 1`, which configures the hypervisor to trigger an EPT access violation whenever the specified guest-physical address is accessed.
3. Each time the target address is accessed, `MyHypervisorApp.exe` receives the VM-exit information and prints the current value of the RIP register (the guest instruction pointer).
4. Use `instruction_offset_calculator.cpp` to compute the instruction offset as:  
   `offset = instruction_address (RIP) − process_base_address`.

These instruction-relative offsets can be reused after restarts by recomputing the absolute addresses from the new module base.

## 3. Use the instruction addresses to recover the runtime addresses of the position data

This step uses the stable instruction locations found in step 2 to continuously rediscover the actual data addresses where position information is stored, even across process restarts.

![image](https://github.com/user-attachments/assets/a19d5583-8bec-45ee-8621-8b7e14b0670c)

`position_addresses_generator.py` performs the following:

1. Adds the current base address of the target module to the stored instruction offsets to compute the absolute instruction addresses, then saves them to a file.
2. Configures the hypervisor to trigger EPT violations on those instruction addresses and, when the VM exits, inspects the memory operands to obtain the actual addresses of the position data structures.

As a result, the toolchain can re-identify the relevant data structures after each process start without hardcoding raw pointers.

## 4. Track entity positions using "World To Screen" matrix computation
[**World-To-Screen**](https://github.com/vacu9708/Hypervisor-Reverse_engineering/blob/main/World%20To%20Screen/World%20To%20Screen.pdf) computation refers to the process of converting a 3D point in world space to a 2D point on the screen.<br>
![image](https://github.com/user-attachments/assets/9e801bc4-fa55-44f8-9bd6-96fe1bc44155)

`rectangle_drawer.cpp` is responsible for visualization and performs:

1. Reading the position information (3D world coordinates for the local entity and other entities) from the reconstructed addresses.
2. Applying a World-To-Screen transformation to convert 3D world coordinates into 2D screen-space coordinates, taking into account:
    - The application’s right-handed coordinate system.
    - A vertical field-of-view (vertical FOV) projection.
3. Rendering simple rectangles around the projected 2D positions for visual inspection and debugging.

This end-to-end flow demonstrates how to combine hypervisor-based dynamic analysis, reverse-engineering of data structures, and 3D math to build a transparent runtime visualization tool for a protected 3D application.

# Result
[a](https://github.com/user-attachments/assets/6b9f77b8-544e-47e3-9b92-c3f8a80dcb90)
