# Description
<img src="https://github.com/user-attachments/assets/d4a40ab2-8d29-4d0e-8c65-48245d5bd141" width="70%"><br>
This project aims to bypass video game's security mechanisms, track enemy positions, and draw rectangles around them.<br>
This can be further developed into an aimbot.

# Prior knowledge
## Hypervisor
![image](https://github.com/user-attachments/assets/8f9f1214-fff0-4f37-a044-aaf98f18e9b9)

A hypervisor (also called a virtual machine monitor or VMM) is a software layer that allows different operating systems to run concurrently on the same hardware while remaining isolated from each other.
- **Why use the hypervisor:**
    - This project takes advantage of the fact that Windows applications cannot detect the existence of the hypervisor because it operates outside the windows kernel.
    - The hypervisor functions as a debugger to circumvent anti-debugging mechanisms that block conventional debuggers.
        - Debugging using the **Vectored Exception Handling** after DLL injection is blocked by anti-debugging mechanisms
        - Debugging using the **WIN APIs** is also blocked
- **What the hypervisor does:** The hypervisor shows CPU registers upon EPT violations at specific memory addresses just like normal debuggers show CPU registers upon breakpoints.
- **What EPT Violation (Extended Page Table Violation) means**: An EPT (Extended Page Table) violation is a type of page fault that occurs in Intel VT-x virtualization when a guest virtual machine (VM) attempts to access memory in a way that violates the Extended Page Table (EPT) permissions set by the hypervisor.

The hypervisor in this project only works in the intel CPU.

## World To Screen
**World-To-Screen** computation refers to the process of converting a 3D point in world space to a 2D point on the screen.<br>
![image](https://github.com/user-attachments/assets/9e801bc4-fa55-44f8-9bd6-96fe1bc44155)

[Explanation of World To Screen](https://github.com/vacu9708/Game-hacking/blob/main/World%20To%20Screen/World%20To%20Screen.pdf)

# Steps to track enemy positions
## 1. Find the memory addresses where position info is stored
I used `scanner.cpp` and `candidate_analyer.cpp` to find memory addresses where position info(player's and enemies') is stored.<br>
Keep filtering out candidate addresses by setting some conditions such as:
- 3 contiguous float values
- Not out of a range, e.g. [123456, -123456]
- Non-zero
- Values changed after the position changes in the game

For the player, the facing direction [-1, 1] is needed in addition to the player's 3D coordinates to perform the **World To Screen** computation below.<br>
The facing direction is likely to be stored in the same struct where the player's 3D coordinates are located.

## 2. Use the hypervisor to find the instruction addresses executed at the moment the indentified memory addresses are accessed.
The memory addreseses found above change after the process restarts because they are **dynamically allocated**.<br>
Therefore, they cannot consistently be used, which is why this step is necessary.
![image](https://github.com/user-attachments/assets/20aa7248-e6e2-42f4-af05-42e0bd7d0ebd)

1. Run the `hypervisor device driver`
2. Execute `./MyHypervisorApp.exe [process ID] [memory address] 1`, which sets up an access violation at the specified memory address
3. Get the value in the RIP register that `MyHypervisorApp.exe` prints each time the target address is accessed
4. Modify the instruction offset in `instruction_offset_calculator.cpp` -> offset = process's base address - instruction address(i.e. RIP register)

## 3. Use the found instruction addresses to get the addresses where the position info is stored
This step takes advantage of the fact that the memory addresses where position info is stored can consistently be found using the instructions executed at the moment they are accessed.<br>
![image](https://github.com/user-attachments/assets/a19d5583-8bec-45ee-8621-8b7e14b0670c)

Run `position_addresses_generator.py` that does the following :
1. Adds the target app's base address to the target instrunctions' offset to calculate instruction addresses, and save them to a file.
2. Uses the hypervisor to capture the moments when EPT violations are triggered at the target instruction addresses, and obtains the adresses of position info

## 4. Track enemy positions by performing "World To Screen" matrix computation
Run `aimbot.cpp` that does the following :
1. Extracts the position info(3D world coordinates of the player and enemies) from the found addresses where the position info is stored.
2. Performs World To Screen computation to convert 3D world coordinates to 2D screen coordinates, considering the following:
    - Overwatch's coordinate system is right-handed.
    - Overwatch uses vertical FOV.
4. Draw rectangles around the screen coordinates on the screen.

# Result
[a](https://github.com/user-attachments/assets/6b9f77b8-544e-47e3-9b92-c3f8a80dcb90)
