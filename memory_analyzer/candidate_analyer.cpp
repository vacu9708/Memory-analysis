#include <iostream>
#include <Windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <vector>
#include <fstream>
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <type_traits>
#include <functional>
#include <filesystem>
#include <limits>

// Function to get the process ID based on the process name
inline DWORD GetProcessID(const std::wstring& processName) {
    PROCESSENTRY32W pe32; // Use wide-character version
    pe32.dwSize = sizeof(PROCESSENTRY32W);

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); // Use TH32CS_SNAPPROCESS
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    if (Process32FirstW(hSnapshot, &pe32)) { // Use wide-character function
        do {
            std::wstring exeName = pe32.szExeFile; // Now szExeFile is wchar_t array
            if (processName == exeName) {
                CloseHandle(hSnapshot);
                return pe32.th32ProcessID;
            }
        } while (Process32NextW(hSnapshot, &pe32)); // Use wide-character function
    }

    CloseHandle(hSnapshot);
    return 0;
}

struct Candidate {
    uintptr_t address;
    float value1;
    float value2;
    float value3;
};

// Template function to read a range of memory addresses around a specified address
template<typename T>
inline void ReadMemoryRange(DWORD processID) {
    HANDLE hProcess = OpenProcess(PROCESS_VM_READ, FALSE, processID);
    if (!hProcess) {
        std::cerr << "Unable to open process." << std::endl;
        return;
    }

    while (true) {
        // Prompt for memory address
        std::string addressStr;
        std::cout << "\nEnter memory address to read (e.g., 0x2cf1e771958): ";
        std::getline(std::cin, addressStr);

        try {
            // Convert the hex string to an address
            uintptr_t addr = std::stoull(addressStr, nullptr, 16);
            BYTE* baseAddress = reinterpret_cast<BYTE*>(addr);

            // Prompt for range
            int range;
            std::cout << "Enter the number of values to read in each direction: ";
            std::cin >> range;
            std::cin.ignore(); // Ignore the newline after reading an integer

            SIZE_T bytesRead;
            T value;

            // Lambda for printing values based on type
            auto PrintValue = [&](BYTE* readAddress, T val) {
                if constexpr (std::is_same<T, uintptr_t>::value) {
                    std::cout << "Value at address 0x" << std::hex << reinterpret_cast<uintptr_t>(readAddress) 
                          << std::dec << ": 0x" << std::hex << val << std::dec << std::endl;
                } else if constexpr (std::is_same<T, BYTE>::value) {
                    printf("%d, ", val);
                } else {
                    std::cout << "Value at address 0x" << std::hex << reinterpret_cast<uintptr_t>(readAddress) 
                          << std::dec << ": " << val << std::endl;
                }
            };

            // Function to read and print a value at a specific address
            auto ReadAndPrint = [&](BYTE* readAddress) -> bool {
                if (!ReadProcessMemory(hProcess, readAddress, &value, sizeof(T), &bytesRead) || bytesRead != sizeof(T)) {
                    std::cerr << "Failed to read memory at address 0x" 
                              << std::hex << reinterpret_cast<uintptr_t>(readAddress) 
                              << std::dec << std::endl;
                    return false;
                }

                // Exclude float values between -0.1 and 0.1
                if constexpr (std::is_same<T, float>::value) {
                    if (value > -0.1f && value < 0.1f) {
                        // Skip this value
                        return false;
                    }
                }

                // Skip too big float values
                if constexpr (std::is_same<T, float>::value) {
                    if (std::abs(value) > 4000.0f) {
                        return false;
                    }
                }

                // Additional checks for pointer types with correct literal suffixes
                if constexpr (std::is_same<T, uintptr_t>::value) {
                    if (!(value >= 0x1000000000ULL && value <= 0x30000000000ULL)) {
                        return false;
                    }
                }

                PrintValue(readAddress, value);
                return true;
            };

            // if T is char
            if constexpr (std::is_same<T, BYTE>::value) {
                // Read values before the specified address
                for (int i = range; i >= 1; --i) {
                    BYTE* readAddress = baseAddress - i;
                    if (!ReadAndPrint(readAddress)) {
                        continue; // Skip to next if read failed or condition not met
                    }
                }
                // Read 12 bytes from the specified address
                std::cout << "**";
                for (int i = 0; i < 12; ++i) {
                    BYTE* readAddress = baseAddress + i;
                    if (!ReadAndPrint(readAddress)) {
                        continue; // Skip to next if read failed or condition not met
                    }
                }
                std::cout << "**";
                // Read values after the 12 bytes from the specified address
                for (int i = 1; i <= range; ++i) {
                    BYTE* readAddress = baseAddress + 12 + i;
                    if (!ReadAndPrint(readAddress)) {
                        continue; // Skip to next if read failed or condition not met
                    }
                }
            }

            // if T is not char
            if constexpr (!std::is_same<T, BYTE>::value) {
                // Read values before the specified address
                for (int i = range; i >= 1; i--) {
                    BYTE* readAddress = baseAddress - (i * sizeof(T));
                    if (!ReadAndPrint(readAddress)) {
                        continue; // Skip to next if read failed or condition not met
                    }
                }
                // Read the value from the specified address
                std::cout << ReadAndPrint(baseAddress) << std::endl;
                for (int i = 1; i <= range; i++) {
                    BYTE* readAddress = baseAddress + (i * sizeof(T));
                    if (!ReadAndPrint(readAddress)) {
                        continue; // Skip to next if read failed or condition not met
                    }
                }
            }

        } catch (const std::exception& e) {
            std::cerr << "Invalid address format." << std::endl;
            continue; // Prompt again if address format is invalid
        }
    }

    CloseHandle(hProcess);
}

// Existing functions: findWorldCoordinates, findDirectionVector, ScanForPattern
// [Omitted here for brevity; assume they are present as in your original code]

inline void RecursiveMemorySearch(DWORD processID, uintptr_t startAddress, int range, int maxDepth) {
    HANDLE hProcess = OpenProcess(PROCESS_VM_READ, FALSE, processID);
    if (!hProcess) {
        std::cerr << "Unable to open process for recursive search." << std::endl;
        return;
    }

    // Open the output file
    std::ofstream outFile("memory_records/recursive_search_results.txt", std::ios::out | std::ios::trunc);
    if (!outFile.is_open()) {
        std::cerr << "Failed to open output file for writing." << std::endl;
        CloseHandle(hProcess);
        return;
    }

    float lastFloatValue = std::numeric_limits<float>::quiet_NaN(); // Initialize to NaN

    // Define a lambda function for recursion
    std::function<void(uintptr_t, int, uintptr_t, uintptr_t)> search = [&](uintptr_t addr, int depth, uintptr_t parentStartAddr, uintptr_t parentCurrentAddr) {
        if (depth > maxDepth) {
            return;
        }
        // Temporary storage for found float values
        std::vector<std::pair<uintptr_t, float>> foundFloats;

        // Search for float values within the specified range
        for(int i = -range; i <= range; ++i) {
            uintptr_t currentAddr = addr + i * sizeof(float);
            float floatValue;
            SIZE_T bytesRead;

            if(ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(currentAddr), &floatValue, sizeof(float), &bytesRead) && bytesRead == sizeof(float)) {
                if(std::isnan(floatValue))
                    continue;
                if(floatValue > -1.0f && floatValue < 1.0f)
                    continue;
                // Skip too big float values
                if (std::abs(floatValue) > 4000.0f) {
                    continue;
                }
                // Skip if floatValue does not have a fractional part
                if (std::fmod(floatValue, 1.0f) == 0.0f) {
                    continue; // Skip writing integer-like float values
                }
                // Omit consecutive duplicate float values
                if (!std::isnan(lastFloatValue) && floatValue == lastFloatValue) {
                    continue;
                }

                // Add to found floats
                foundFloats.emplace_back(currentAddr, floatValue);
                lastFloatValue = floatValue;
            }
        }

        // If any floats are found, write the addresses and the float values
        if(!foundFloats.empty()) {
            outFile << "parentStartAddr: 0x" << std::hex << parentStartAddr 
                    << " / parentCurrentAddr: 0x" << parentCurrentAddr
                    << " / currentStartAddr: 0x" << addr << std::dec << std::endl;


            for(const auto& [fa, fv] : foundFloats) {
                outFile << "Depth: " << depth 
                        << " / Addr: 0x" << std::hex << fa 
                        << " / Value: " << std::dec << fv << std::endl;
            }
            outFile << std::endl; // Add a newline for better readability
        }

        // Search for pointer values within the specified range and recurse
        for(int i = -range; i <= range; ++i) {
            if(i == 0) continue; // Optionally skip the exact start address

            uintptr_t currentPtrAddr = addr + i * sizeof(uintptr_t);
            uintptr_t ptrValue;
            SIZE_T bytesRead;

            if(ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(currentPtrAddr), &ptrValue, sizeof(uintptr_t), &bytesRead) && bytesRead == sizeof(uintptr_t)) {
                if(ptrValue >= 0x1000000000ULL && ptrValue <= 0x30000000000ULL) {
                    // Recurse into the pointer's address
                    search(ptrValue, depth + 1, (uintptr_t)addr, (uintptr_t)currentPtrAddr);
                }
            }
        }
    };

    // Start the recursive search
    search(startAddress, 0, 0, 0);

    outFile.close();
    CloseHandle(hProcess);
}

// Function to initiate RecursiveMemorySearch based on user input
inline void InitiateRecursiveSearch(DWORD processID) {
    HANDLE hProcess = OpenProcess(PROCESS_VM_READ, FALSE, processID);
    if (!hProcess) {
        std::cerr << "Unable to open process." << std::endl;
        return;
    }

    // Prompt for start address
    std::string startAddressStr;
    std::cout << "Enter start memory address for recursive search (e.g., 0x2cf1e771958): ";
    std::getline(std::cin, startAddressStr);

    uintptr_t startAddress;
    try {
        startAddress = std::stoull(startAddressStr, nullptr, 16);
    } catch (const std::exception& e) {
        std::cerr << "Invalid address format." << std::endl;
        CloseHandle(hProcess);
        return;
    }

    // Prompt for range
    int range;
    std::cout << "Enter the number of values to read in each direction: ";
    std::cin >> range;
    std::cin.ignore(); // Ignore the newline after reading an integer

    // Prompt for maximum recursion depth
    int maxDepth;
    std::cout << "Enter the maximum recursion depth: ";
    std::cin >> maxDepth;
    std::cin.ignore(); // Ignore the newline after reading an integer

    // Start the recursive search
    RecursiveMemorySearch(processID, startAddress, range, maxDepth);

    CloseHandle(hProcess);
}

// Template function to write a value to a specific memory address in the target process
template<typename T>
inline void WriteMemoryValue(DWORD processID) {
    // Open the target process with write permissions
    HANDLE hProcess = OpenProcess(PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, processID);
    if (!hProcess) {
        std::cerr << "Unable to open process with write permissions." << std::endl;
        return;
    }

    while (true) {
        // Prompt for memory address
        std::string addressStr;
        std::cout << "Enter memory address to write (e.g., 0x2cf1e771958): ";
        std::getline(std::cin, addressStr);

        try {
            // Convert the hex string to an address
            uintptr_t addr = std::stoull(addressStr, nullptr, 16);
            BYTE* targetAddress = reinterpret_cast<BYTE*>(addr);

            // Prompt for the value to write
            T newValue;
            std::cout << "Enter the value to write (";
            if constexpr (std::is_same<T, float>::value) {
                std::cout << "float";
            } else if constexpr (std::is_same<T, int>::value) {
                std::cout << "int";
            } else {
                std::cout << "unknown type";
            }
            std::cout << "): ";
            std::cin >> newValue;
            std::cin.ignore(); // Ignore the newline after reading the value

            SIZE_T bytesWritten;
            BOOL success = WriteProcessMemory(hProcess, targetAddress, &newValue, sizeof(T), &bytesWritten);
            if (!success || bytesWritten != sizeof(T)) {
                std::cerr << "Failed to write memory at address 0x" 
                          << std::hex << addr << std::dec << std::endl;
            } else {
                std::cout << "Successfully wrote value to address 0x" 
                          << std::hex << addr << std::dec << std::endl;
            }

        } catch (const std::exception& e) {
            std::cerr << "Invalid address or value format." << std::endl;
            continue; // Prompt again if address or value format is invalid
        }
    }

    CloseHandle(hProcess);
}

// Template function to dereference a pointer and read a range of values around the address it points to
template<typename T>
inline void DereferencePointer(DWORD processID) {
    // Open the target process with read permissions
    HANDLE hProcess = OpenProcess(PROCESS_VM_READ, FALSE, processID);
    if (!hProcess) {
        std::cerr << "Unable to open process." << std::endl;
        return;
    }

    while (true) {
        // Prompt for pointer address
        std::string pointerStr;
        std::cout << "Enter pointer address to dereference (e.g., 0x2cf1e771958): ";
        std::getline(std::cin, pointerStr);

        try {
            // Convert the hex string to an address
            uintptr_t ptrAddr = std::stoull(pointerStr, nullptr, 16);
            BYTE* ptr = reinterpret_cast<BYTE*>(ptrAddr);

            // Read the address stored at the pointer
            uintptr_t derefAddr;
            SIZE_T bytesRead;
            if (!ReadProcessMemory(hProcess, ptr, &derefAddr, sizeof(uintptr_t), &bytesRead) || bytesRead != sizeof(uintptr_t)) {
                std::cerr << "Failed to read pointer address at 0x" 
                          << std::hex << ptrAddr << std::dec << std::endl;
                continue; // Prompt again if reading pointer failed
            }

            // Prompt for the range to read around the dereferenced address
            int range;
            std::cout << "Enter the number of values to read in each direction: ";
            std::cin >> range;
            std::cin.ignore(); // Ignore the newline after reading an integer

            // Display the base address
            std::cout << "Pointer at address 0x" << std::hex << ptrAddr 
                      << " points to base address 0x" << derefAddr << std::dec << std::endl;

            T value;

            // Read and display values around the dereferenced address
            for (int i = -range; i <= range; ++i) {
                uintptr_t currentAddr = derefAddr + i * sizeof(T);
                
                if (ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(currentAddr), &value, sizeof(T), &bytesRead) && bytesRead == sizeof(T)) {
                    std::cout << "Value at address 0x" << std::hex << currentAddr << std::dec << ": ";
                    
                    if constexpr (std::is_same<T, uintptr_t>::value) {
                        std::cout << "0x" << std::hex << value << std::dec;
                    } else {
                        std::cout << value;
                    }
                    std::cout << std::endl;
                } else {
                    std::cerr << "Failed to read value at address 0x" 
                              << std::hex << currentAddr << std::dec << std::endl;
                }
            }

        } catch (const std::exception& e) {
            std::cerr << "Invalid address format." << std::endl;
            continue; // Prompt again if address format is invalid
        }
    }

    CloseHandle(hProcess);
}

int main() {
    const std::wstring processName = L"Overwatch.exe"; // Ensure the process name matches (case-sensitive)
    DWORD processID = GetProcessID(processName);
    if (processID == 0) {
        std::cerr << "Process not found." << std::endl;
        return 1;
    } else {
        std::cout << "Process ID: " << processID << std::endl;
    }
    
    while (1) {
        char choice;
        std::cout << "\nChoose an option:\n"
                  << "1 - Read a range of values\n"
                  << "2 - Dereference a pointer\n"
                  << "3 - Write a value to a specific address\n"
                  << "4 - Recursive memory search\n"
                  << "5 - Scan memory for pattern\n"
                  << "Enter your choice: ";
        std::cin >> choice;
        std::cin.ignore(); // Ignore the newline after reading a character

        if (choice == '1') {
            char readChoice;
            std::cout << "Choose the type of values to read:\n"
                      << "1 - float\n"
                      << "2 - int\n"
                      << "3 - byte\n"
                      << "4 - pointer (uintptr_t)\n"
                      << "Enter your choice: ";
            std::cin >> readChoice;
            std::cin.ignore(); // Ignore the newline after reading a character

            if (readChoice == '1') {
                ReadMemoryRange<float>(processID); // Read range of float values
            } else if (readChoice == '2') {
                ReadMemoryRange<int>(processID); // Read range of int values
            } else if (readChoice == '3') {
                ReadMemoryRange<BYTE>(processID); // Read range of byte values
            } else if (readChoice == '4') {
                ReadMemoryRange<uintptr_t>(processID); // Read range of pointer values
            } else {
                std::cerr << "Invalid read type choice." << std::endl;
            }
        } else if (choice == '2') {
            char derefChoice;
            std::cout << "Choose the type to dereference:\n"
                      << "1 - float\n"
                      << "2 - int\n"
                      << "3 - byte\n"
                      << "4 - pointer (uintptr_t)\n"
                      << "Enter your choice: ";
            std::cin >> derefChoice;
            std::cin.ignore(); // Ignore the newline after reading a character

            if (derefChoice == '1') {
                DereferencePointer<float>(processID); // Dereference as float
            } else if (derefChoice == '2') {
                DereferencePointer<int>(processID); // Dereference as int
            } else if (derefChoice == '3') {
                DereferencePointer<BYTE>(processID); // Dereference as BYTE
            } else if (derefChoice == '4') {
                DereferencePointer<uintptr_t>(processID); // Dereference as uintptr_t
            } else {
                std::cerr << "Invalid dereference type choice." << std::endl;
            }
        } else if (choice == '3') { // Write option is now in the third position
            char writeChoice;
            std::cout << "Choose the type to write:\n"
                      << "1 - float\n"
                      << "2 - int\n"
                      << "Enter your choice: ";
            std::cin >> writeChoice;
            std::cin.ignore(); // Ignore the newline after reading a character

            if (writeChoice == '1') {
                WriteMemoryValue<float>(processID); // Write a float value
            } else if (writeChoice == '2') {
                WriteMemoryValue<int>(processID); // Write an int value
            } else {
                std::cerr << "Invalid write type choice." << std::endl;
            }
        } else if (choice == '4') { // Recursive search option is now in the fourth position
            InitiateRecursiveSearch(processID); // Initiate recursive memory search
        } else if (choice == '5') {
            // ScanForPattern(processID); // Scan memory for the specified pattern
            std::cout << "Scan for pattern functionality is not implemented in this snippet." << std::endl;
        } else {
            std::cerr << "Invalid choice." << std::endl;
        }
    }

    return 0;
}