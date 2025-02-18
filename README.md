![Screenshot_20250217_214151_Gallery](https://github.com/user-attachments/assets/16aa0185-5165-48f7-906e-ffa47b1e4d49)

# Why use the hypervisor?
The hypervisor is used as a debugger instead of normal debuggers because they are blocked by anti-debugging mechanisms.<br>
The hypervisor shows CPU registers' values upon EPT violations.<br>

# Steps to track enemies' positions
## 1. Find the memory addresses where position info is stored are accessed
1. Use scanner.cpp and candidate_analyer.cpp to find memory addresses where position info(player's and enemies') is stored.

## 2. Use the hypervisor to find the instruction addresses executed at the moment the found memory addresses are accessed.
The memory addresses where position info is stored can always be found using the instructions executed at the moment they are accessed.
1. Run the hypervisor device driver
2. Execute ./MyHypervisorApp.exe [process ID] [memory address] 1, which prints CPU register values each time the target address is accessed
3. Get the value in the RIP register that MyHypervisorApp.exe prints
4. Modify the instruction offset in instruction_offset_calculator.cpp (offset = game's base address - instruction address)

## 3. Use the found instruction addresses to get the addresses where the position info is stored
Run position_addresses_generator.py that does the following :
1. Adds the target app's base address to the target instrunction's offset and save the calculated instruction addresses to a file.
2. Uses the hypervisor to catch the moments the EPT violation is triggered upon the target instruction addresses and
get the position info's addresses

## 4. Track enemies' positions
![image](https://github.com/user-attachments/assets/3baca4d2-27d7-48ea-8165-2b5ff8727bc2)

Run aimbot.cpp that does the following :
1. Extracts the position info(3D world coordinates of the player and enemies) from the found addresses where the position info is stored.
2. Performs World To Screen computation to convert it to 2D screen coordinates.
    - Be careful of the fact that Overwatch's coordinate system is right-handed.
    - Be careful of the fact that Overwatch uses vertical FOV.
3. Draw the rectangles at the calculated screen coordinates on the screen.

# Result
[a](https://github.com/user-attachments/assets/6b9f77b8-544e-47e3-9b92-c3f8a80dcb90)
