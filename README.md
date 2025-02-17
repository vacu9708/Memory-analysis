# Game-hacking
![Screenshot_20250217_214151_Gallery](https://github.com/user-attachments/assets/16aa0185-5165-48f7-906e-ffa47b1e4d49)

# 1. Find the memory addresses where position info is stored are accessed
Run scanner.cpp : ScanFloatPointers(), ScanFloatValues() to find memory addresses where position info is stored.

# 2. Use the hypervisor to find the instruction addresses executed at the moment the found memory addresses are accessed.
The memory addresses where position info is stored can always be found using the instructions executed at the moment they are accessed.
1. stop_hypervisor.ps1; run_hypervisor.ps1
2. MyHypervisorApp.exe [process ID] [memory address] 1
3. Get the value in the RIP register.
4. Modify the instruction offset in instruction_offset_calculator.cpp

# 3. Use the found instruction addresses to get the addresses where the position info is stored
Run position_addresses_generator.py that does the following :
1. adds the target app's base address to the target instrunction's offset and save the calculated instruction addresses to a file.
2. executes the hypervisor. It will catch the moments the EPT violation is triggered upon the target instruction addresses and
get the addresses where the position info is stored.

# 4. Use the position info to know the enemies' positions
Run aimbot.cpp that does the following :
1. extracts position info(3D world coordinates) from the found addresses where the position info is stored.
2. performs World To Screen computation.
    - Be careful of the fact that Overwatch's coordinate system is right-handed.
    - Be careful of the fact that Overwatch uses vertical FOV.
3. Draw the rectangles at the calculated screen coordinates on the screen.
