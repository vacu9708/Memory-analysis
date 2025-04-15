#include <iostream>
#include <iomanip>
#include <cstdint>
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <fstream>

// Function to get the process ID of a given process name
DWORD GetProcessID(const std::wstring& processName) {
    DWORD processID = 0;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to create process snapshot." << std::endl;
        return 0;
    }

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(hSnapshot, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, processName.c_str()) == 0) {
                processID = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(hSnapshot, &pe));
    } else {
        std::cerr << "Failed to retrieve first process." << std::endl;
    }

    CloseHandle(hSnapshot);
    return processID;
}

// Function to get the base address of a module within a process
uintptr_t GetModuleBaseAddress(DWORD processID, const std::wstring& moduleName) {
    uintptr_t baseAddress = 0;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processID);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to create module snapshot." << std::endl;
        return 0;
    }

    MODULEENTRY32W me;
    me.dwSize = sizeof(MODULEENTRY32W);

    if (Module32FirstW(hSnapshot, &me)) {
        do {
            if (_wcsicmp(me.szModule, moduleName.c_str()) == 0) {
                baseAddress = reinterpret_cast<uintptr_t>(me.modBaseAddr);
                break;
            }
        } while (Module32NextW(hSnapshot, &me));
    } else {
        std::cerr << "Failed to retrieve first module." << std::endl;
    }

    CloseHandle(hSnapshot);
    return baseAddress;
}

int main() {
    // Define the process and module name
    std::wstring processName = L"Overwatch.exe";
    std::wstring moduleName = L"Overwatch.exe"; // Main module usually has the same name

    // Get the process ID
    DWORD processID = GetProcessID(processName);
    if (processID == 0) {
        std::cerr << "Process " << std::string(processName.begin(), processName.end()) << " not found." << std::endl;
        return 1;
    } else {
        std::cout << "Process ID: " << processID << std::endl;
    }

    // Get the base address of the module
    uintptr_t base_address = GetModuleBaseAddress(processID, moduleName);
    if (base_address == 0) {
        std::cerr << "Module " << std::string(moduleName.begin(), moduleName.end()) << " not found in process." << std::endl;
        return 1;
    }

    std::cout << "Base address of " << std::string(moduleName.begin(), moduleName.end()) 
              << ": 0x" << std::hex << base_address << std::dec << std::endl;

    // Calculate the offsets
    uintptr_t my_offset = 0x7ff70837026d - 0x7ff706270000;
    uintptr_t players_offset = 0x7ff768695811 - 0x7ff7677e0000; // entity list

    // Calculate the final addresses by adding the base address to the offsets
    uintptr_t my_offset_result = base_address + my_offset;
    uintptr_t players_offset_result = base_address + players_offset;

    // Print the results in hexadecimal format
    std::cout << "My instruction address: 0x" << std::hex << my_offset_result << std::dec << std::endl;
    std::cout << "Players instruction addresses: 0x" << std::hex << players_offset_result << std::dec << std::endl;
    // write the results
    std::ofstream myfile;
    myfile.open("instruction_addresses.txt");
    myfile << std::hex << my_offset_result << std::dec << std::endl;
    myfile << std::hex << players_offset_result << std::dec << std::endl;

    return 0;
}
