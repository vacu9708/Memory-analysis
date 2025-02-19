#include <iostream>
#include <Windows.h>
#include <tlhelp32.h>
#include <vector>
#include <fstream>
#include <string>
#include <cmath>
#include <cstdint>
#include <tuple>        // Added for std::tuple

// Define the optimized MemoryBlock structure
template<typename T>
struct MemoryBlock {
    BYTE* baseAddress;
    std::vector<SIZE_T> offsets; // Store only the offsets where the target value is found

    MemoryBlock(BYTE* addr) : baseAddress(addr) {}

    void addOffset(SIZE_T offset) {
        offsets.push_back(offset);
    }
};

#include <map>
#include <Psapi.h>
#pragma comment(lib, "Psapi.lib")
uintptr_t GetModuleBaseAddress(DWORD processID, const char* moduleName) {
    HMODULE hModules[1024];
    DWORD cbNeeded;

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processID);
    if (!hProcess) {
        std::cerr << "Failed to open process with ID " << processID << std::endl;
        return 0;
    }

    if (EnumProcessModules(hProcess, hModules, sizeof(hModules), &cbNeeded)) {
        for (unsigned int i = 0; i < (cbNeeded / sizeof(HMODULE)); i++) {
            wchar_t szModuleName[MAX_PATH];
            if (GetModuleBaseNameW(hProcess, hModules[i], szModuleName, sizeof(szModuleName) / sizeof(wchar_t))) {
                if (wcscmp(szModuleName, std::wstring(moduleName, moduleName + strlen(moduleName)).c_str()) == 0) {
                    uintptr_t baseAddress = reinterpret_cast<uintptr_t>(hModules[i]);
                    CloseHandle(hProcess);
                    return baseAddress;
                }
            }
        }
    }

    CloseHandle(hProcess);
    std::cerr << "Module not found: " << moduleName << std::endl;
    return 0;
}

// Function to get Process ID by process name
DWORD GetProcessID(const std::wstring& processName) {
    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32W);

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    if (Process32FirstW(hSnapshot, &pe32)) { // Use Process32FirstW for wide-character support
        do {
            std::wstring exeName = pe32.szExeFile; // szExeFile is WCHAR[260] in PROCESSENTRY32W
            if (processName == exeName) {
                CloseHandle(hSnapshot);
                return pe32.th32ProcessID;
            }
        } while (Process32NextW(hSnapshot, &pe32)); // Use Process32NextW for wide-character support
    }

    CloseHandle(hSnapshot);
    return 0;
}

// Function to evaluate if a memory region is valid for scanning
bool IsValidMemoryRegion(const MEMORY_BASIC_INFORMATION& mem_info) {
    // Skip executable memory regions
    if (mem_info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) {
        return false;
    }

    // Check if memory is committed, readable, and not guarded
    if (mem_info.State == MEM_COMMIT &&
        (mem_info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY)) &&
        !(mem_info.Protect & PAGE_GUARD)) {
        return true;
    }

    return false;
}

template<typename T>
void ScanInt(DWORD processID) {
    HANDLE hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, processID);
    if (!hProcess) {
        std::cerr << "Unable to open process for scanning." << std::endl;
        return;
    }

    std::vector<MemoryBlock<T>> memoryBlocks;

    T userValue;
    std::cout << "Enter a value to maintain: ";
    std::cin >> userValue;
    std::cin.ignore();  // Clear the newline character from the input buffer

    // Enumerate memory regions directly within the initial scan
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);

    MEMORY_BASIC_INFORMATION mem_info;
    BYTE* address = static_cast<BYTE*>(sysInfo.lpMinimumApplicationAddress);

    while (VirtualQueryEx(hProcess, address, &mem_info, sizeof(mem_info))) {
        if (address > static_cast<BYTE*>(sysInfo.lpMaximumApplicationAddress)) {
            break;
        }

        if (!IsValidMemoryRegion(mem_info)) {
            address += mem_info.RegionSize;
            continue;
        }

        SIZE_T bytesRead;
        SIZE_T regionSize = mem_info.RegionSize;
        BYTE* baseAddress = static_cast<BYTE*>(mem_info.BaseAddress);

        // Calculate total number of elements of type T in the region
        SIZE_T totalElements = regionSize / sizeof(T);
        SIZE_T totalReadPointers = 0;

        // Define chunk size (number of elements) to read at a time
        const SIZE_T chunkSize = 1000;

        std::vector<T> buffer(chunkSize);

        while (totalReadPointers < totalElements) {
            SIZE_T elementsToRead = std::min(chunkSize, totalElements - totalReadPointers);
            SIZE_T bytesToRead = elementsToRead * sizeof(T);

            if (ReadProcessMemory(hProcess, baseAddress + totalReadPointers * sizeof(T), buffer.data(), bytesToRead, &bytesRead)) {
                SIZE_T elementsRead = bytesRead / sizeof(T);
                for (SIZE_T i = 0; i < elementsRead; ++i) {
                    if (buffer[i] == userValue) {
                        // Check if a MemoryBlock for this region already exists
                        if (memoryBlocks.empty() || memoryBlocks.back().baseAddress != baseAddress) {
                            memoryBlocks.emplace_back(baseAddress);
                        }
                        memoryBlocks.back().addOffset(totalReadPointers + i);
                    }
                }
                totalReadPointers += elementsRead;
            } else {
                // If ReadProcessMemory fails, skip the remaining part of this region
                std::cerr << "Failed to read memory at address: " << static_cast<void*>(baseAddress + totalReadPointers * sizeof(T)) << std::endl;
                break;
            }
        }
        address += mem_info.RegionSize;
    }

    std::cout << "Initial scan complete. Number of memory blocks: " << memoryBlocks.size() << std::endl;

    while (true) {
        std::vector<MemoryBlock<T>> alive_blocks;
        unsigned int number_of_alive_values = 0;

        T newUserValue;
        std::cout << "Enter a new value to maintain: ";
        std::cin >> newUserValue;
        std::cin.ignore();  // Clear the newline character from the input buffer

        // Iterate through existing memory blocks and verify if values still match
        for (auto& block : memoryBlocks) {
            std::vector<SIZE_T> aliveOffsets;
            for (const auto& offset : block.offsets) {
                T currentValue;
                SIZE_T bytesRead;
                if (ReadProcessMemory(hProcess, block.baseAddress + offset * sizeof(T), &currentValue, sizeof(T), &bytesRead)) {
                    if (currentValue == newUserValue) {
                        aliveOffsets.push_back(offset);
                        number_of_alive_values++;
                    }
                }
            }
            if (!aliveOffsets.empty()) {
                MemoryBlock<T> aliveBlock(block.baseAddress);
                aliveBlock.offsets = std::move(aliveOffsets);
                alive_blocks.push_back(std::move(aliveBlock));
            }
        }

        if (alive_blocks.empty()) {
            std::cout << "No memory blocks found with the specified value." << std::endl;
            break;
        }

        std::cout << "The number of alive blocks: " << alive_blocks.size() << std::endl;
        std::cout << "The number of alive values: " << number_of_alive_values << std::endl;
        std::cout << "Press 'a' to write changed memory blocks to file, or any other key to continue: ";
        std::string input;
        std::getline(std::cin, input);

        // Write changed memory blocks to file if input is 'a'
        if (input == "a") {
            std::ofstream outFile("memory_records/changed_memory.txt");
            if (!outFile) {
                std::cerr << "Error opening file." << std::endl;
                break;
            }
            for (const auto& block : alive_blocks) {
                outFile << std::hex << "Base Address: " << static_cast<void*>(block.baseAddress) << std::endl;
                for (const auto& offset : block.offsets) {
                    T currentValue;
                    if (ReadProcessMemory(hProcess, block.baseAddress + offset * sizeof(T), &currentValue, sizeof(T), nullptr)) {
                        outFile << "Offset: 0x" << std::hex << (uintptr_t)(block.baseAddress + offset * sizeof(T))
                                << std::dec
                                << ", Value: " << currentValue << std::endl;
                    }
                }
            }
            outFile.close();
            std::cout << "Memory blocks written to file." << std::endl;
        }

        memoryBlocks = std::move(alive_blocks);
    }

    CloseHandle(hProcess);
}

template<typename T>
void ScanCoordinates(DWORD processID) {
    HANDLE hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, processID);
    if (!hProcess) {
        std::cerr << "Unable to open process for scanning." << std::endl;
        return;
    }

    std::vector<MemoryBlock<T>> memoryBlocks;

    T userValues[3];
    std::cout << "Enter 3 values to maintain: ";
    std::cin >> userValues[0] >> userValues[1] >> userValues[2];
    std::cin.ignore();  // Clear the newline character from the input buffer

    // Enumerate memory regions directly within the initial scan
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);

    MEMORY_BASIC_INFORMATION mem_info;
    BYTE* address = static_cast<BYTE*>(sysInfo.lpMinimumApplicationAddress);

    while (VirtualQueryEx(hProcess, address, &mem_info, sizeof(mem_info))) {
        if (address > static_cast<BYTE*>(sysInfo.lpMaximumApplicationAddress)) {
            break;
        }

        if (!IsValidMemoryRegion(mem_info)) {
            address += mem_info.RegionSize;
            continue;
        }

        SIZE_T bytesRead;
        SIZE_T regionSize = mem_info.RegionSize;
        BYTE* baseAddress = static_cast<BYTE*>(mem_info.BaseAddress);

        // Calculate total number of elements of type T in the region
        SIZE_T totalElements = regionSize / sizeof(T);
        SIZE_T totalReadPointers = 0;

        // Define chunk size (number of elements) to read at a time
        const SIZE_T chunkSize = 1000;
        std::vector<T> buffer(chunkSize);

        while (totalReadPointers + 2 < totalElements) { // Ensure there are 3 elements to compare
            SIZE_T elementsToRead = std::min(chunkSize, totalElements - totalReadPointers);
            SIZE_T bytesToRead = elementsToRead * sizeof(T);

            if (ReadProcessMemory(hProcess, baseAddress + totalReadPointers * sizeof(T), buffer.data(), bytesToRead, &bytesRead)) {
                SIZE_T elementsRead = bytesRead / sizeof(T);
                for (SIZE_T i = 0; i + 2 < elementsRead; ++i) {
                    if (std::abs(buffer[i] - userValues[0]) < 0.1f &&
                        std::abs(buffer[i + 1] - userValues[1]) < 0.1f &&
                        std::abs(buffer[i + 2] - userValues[2]) < 0.1f) {
                        // Check if a MemoryBlock for this region already exists
                        if (memoryBlocks.empty() || memoryBlocks.back().baseAddress != baseAddress) {
                            memoryBlocks.emplace_back(baseAddress);
                        }
                        memoryBlocks.back().addOffset(totalReadPointers + i);
                        i += 2; // Skip the next two as they are part of the matched triple
                    }
                }
                totalReadPointers += elementsRead;
            } else {
                // If ReadProcessMemory fails, skip the remaining part of this region
                std::cerr << "Failed to read memory at address: " << static_cast<void*>(baseAddress + totalReadPointers * sizeof(T)) << std::endl;
                break;
            }
        }
        address += mem_info.RegionSize;
    }

    std::cout << "Initial scan complete. Number of memory blocks: " << memoryBlocks.size() << std::endl;

    while (true) {
        std::vector<MemoryBlock<T>> alive_blocks;
        unsigned int number_of_alive_triples = 0;

        T newUserValues[3];
        std::cout << "Enter 3 new values to maintain: ";
        std::cin >> newUserValues[0] >> newUserValues[1] >> newUserValues[2];
        std::cin.ignore();  // Clear the newline character from the input buffer

        // Iterate through existing memory blocks and verify if coordinate triples still match
        for (auto& block : memoryBlocks) {
            std::vector<SIZE_T> aliveOffsets;
            for (const auto& offset : block.offsets) {
                T currentValues[3];
                SIZE_T bytesRead;
                if (ReadProcessMemory(hProcess, block.baseAddress + offset * sizeof(T), currentValues, 3 * sizeof(T), &bytesRead)) {
                    if (std::abs(currentValues[0] - newUserValues[0]) < 0.1f &&
                        std::abs(currentValues[1] - newUserValues[1]) < 0.1f &&
                        std::abs(currentValues[2] - newUserValues[2]) < 0.1f) {
                        aliveOffsets.push_back(offset);
                        number_of_alive_triples++;
                    }
                }
            }
            if (!aliveOffsets.empty()) {
                MemoryBlock<T> aliveBlock(block.baseAddress);
                aliveBlock.offsets = std::move(aliveOffsets);
                alive_blocks.push_back(std::move(aliveBlock));
            }
        }

        if (alive_blocks.empty()) {
            std::cout << "No memory blocks found with the specified values." << std::endl;
            break;
        }

        std::cout << "The number of alive blocks: " << alive_blocks.size() << std::endl;
        std::cout << "The number of alive coordinate triples: " << number_of_alive_triples << std::endl;
        std::cout << "Press 'a' to write changed memory blocks to file, or any other key to continue: ";
        std::string input;
        std::getline(std::cin, input);

        // Write changed memory blocks to file if input is 'a'
        if (input == "a") {
            std::ofstream outFile("memory_records/changed_memory.txt");
            if (!outFile) {
                std::cerr << "Error opening file." << std::endl;
                break;
            }
            for (const auto& block : alive_blocks) {
                outFile << std::hex << "Base Address: " << static_cast<void*>(block.baseAddress) << std::endl;
                for (const auto& offset : block.offsets) {
                    T currentValues[3];
                    if (ReadProcessMemory(hProcess, block.baseAddress + offset * sizeof(T), currentValues, 3 * sizeof(T), nullptr)) {
                        outFile << "Offset: 0x" << std::hex << (uintptr_t)(block.baseAddress + offset * sizeof(T))
                                << std::dec
                                << ", Values: " << currentValues[0] << ", " << currentValues[1] << ", " << currentValues[2] << std::endl;
                    }
                }
            }
            outFile.close();
            std::cout << "Memory blocks written to file." << std::endl;
        }

        memoryBlocks = std::move(alive_blocks);
    }

    CloseHandle(hProcess);
}

// Modified ScanFloatPointers function
bool IsValidCoordinate(float x, float y, float z) {
    // Skip specific float value range
    if (!(x > -400.0f && x < -200.0f) ||
        !(y > 0.0f && y < 30.0f) ||
        !(z > 100.0f && z < 200.0f)) {
        return false;
    }

    // Skip if any value is too big
    if (std::abs(x) > 4000.0f || std::abs(y) > 4000.0f || std::abs(z) > 4000.0f) {
        return false;
    }

    // Skip if any value is not between -1.0 and 1.0
    // if (!(x >= -1.0f && x <= 1.0f) ||
    //     !(y >= -1.0f && y <= 1.0f) ||
    //     !(z >= -1.0f && z <= 1.0f)) {
    //     return false;
    // }
    // Skip if any value is between -1.0 and 1.0
    if ((x >= -1.0f && x <= 1.0f) ||
        (y >= -1.0f && y <= 1.0f) ||
        (z >= -1.0f && z <= 1.0f)) {
        return false;
    }

    // Skip if any value does not have a fractional part
    auto hasFractional = [](float value) -> bool {
        return std::abs(value - std::floor(value)) > 1e-6f;
    };

    if (!hasFractional(x) || !hasFractional(y) || !hasFractional(z)) {
        return false;
    }

    // Skip if any two consecutive values are identical
    if ((std::abs(x - y) < 1e-6f) || (std::abs(y - z) < 1e-6f)) {
        return false;
    }

    // All conditions met
    return true;
}

struct FloatValueRecord {
    BYTE* regionBase;
    BYTE* address;
    float x;
    float y;
    float z;
};

// Function to scan for float values directly
void ScanFloatValues(DWORD processID) {
    HANDLE hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, processID);
    if (!hProcess) {
        std::cerr << "Unable to open process for scanning float values." << std::endl;
        return;
    }

    // Vector to store all valid float value records
    std::vector<FloatValueRecord> floatValueRecords;

    // Enumerate memory regions for the initial scan
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);

    MEMORY_BASIC_INFORMATION mem_info;
    BYTE* address = static_cast<BYTE*>(sysInfo.lpMinimumApplicationAddress);

    while (VirtualQueryEx(hProcess, address, &mem_info, sizeof(mem_info))) {
        if (address > static_cast<BYTE*>(sysInfo.lpMaximumApplicationAddress)) {
            break;
        }

        // Skip memory regions outside the valid range
        BYTE* regionBase = static_cast<BYTE*>(mem_info.BaseAddress);
        if ((uintptr_t)regionBase < 0x1000000000ULL) {
            address += mem_info.RegionSize;
            continue;
        }
        if ((uintptr_t)regionBase > 0x30000000000ULL) {
            break;
        }

        if (!IsValidMemoryRegion(mem_info)) {
            address += mem_info.RegionSize;
            continue;
        }

        SIZE_T bytesRead;
        SIZE_T regionSize = mem_info.RegionSize;
        // We need to read floats in triplets, so step by sizeof(float) each time
        const SIZE_T stepSize = sizeof(float);
        SIZE_T totalSteps = regionSize / stepSize;

        // Define chunk size (number of floats) to read at a time
        const SIZE_T chunkSize = 4096; // Read 16 KB at a time (4096 floats)
        std::vector<float> buffer(chunkSize);

        SIZE_T currentOffset = 0;

        while (currentOffset + 3 <= regionSize) { // Ensure there's space for a triplet
            SIZE_T bytesToRead = chunkSize * sizeof(float);
            if (currentOffset + bytesToRead > regionSize) {
                bytesToRead = regionSize - currentOffset;
            }

            BYTE* currentAddress = regionBase + currentOffset;
            SIZE_T floatsToRead = bytesToRead / sizeof(float);

            if (!ReadProcessMemory(hProcess, currentAddress, buffer.data(), bytesToRead, &bytesRead)) {
                // Optionally, handle read errors
                break;
            }

            SIZE_T floatsRead = bytesRead / sizeof(float);
            for (SIZE_T i = 0; i + 2 < floatsRead; ++i) { // Ensure triplet is within buffer
                float x = buffer[i];
                float y = buffer[i + 1];
                float z = buffer[i + 2];

                // Apply the filtering criteria using the helper function
                if (!IsValidCoordinate(x, y, z)) {
                    continue;
                }

                // Calculate the address of the X coordinate
                BYTE* coordAddress = currentAddress + i * sizeof(float);

                // Create and store the record
                FloatValueRecord record;
                record.regionBase = regionBase;
                record.address = coordAddress;
                record.x = x;
                record.y = y;
                record.z = z;
                floatValueRecords.push_back(record);
            }

            currentOffset += bytesRead;
        }

        address += mem_info.RegionSize;
    }

    std::cout << "Initial scan complete. Number of float value records after signature matching: " 
              << floatValueRecords.size() << std::endl;

    // Continue with the sieving process
    while (true) {
        std::cout << "\nChoose an option:\n"
                  << "1. Keep changed values\n"
                  << "2. Keep unchanged values\n"
                  << "3. Stop scanning and write to file\n"
                  << "Enter your choice (1, 2, or 3): ";
        char choice;
        std::cin >> choice;
        std::cin.ignore(); // Clear the newline character from the input buffer

        if (choice == '3') {
            break; // Exit the scanning loop to write to file
        } else if (choice != '1' && choice != '2') {
            std::cerr << "Invalid choice. Please enter 1, 2, or 3." << std::endl;
            continue;
        }

        // Vector to hold records that remain after exclusion
        std::vector<FloatValueRecord> updatedRecords;
        unsigned int number_of_remaining_records = 0;

        for (auto& record : floatValueRecords) {
            float currentCoordinates[3];
            SIZE_T bytesRead;

            // Unified ReadProcessMemory to read X, Y, Z together
            if (!ReadProcessMemory(hProcess, record.address, currentCoordinates, sizeof(currentCoordinates), &bytesRead) 
                || bytesRead != sizeof(currentCoordinates)) {
                continue; // Skip if unable to read all three coordinates
            }

            float currentX = currentCoordinates[0];
            float currentY = currentCoordinates[1];
            float currentZ = currentCoordinates[2];

            // Reapply the filtering criteria
            if (!IsValidCoordinate(currentX, currentY, currentZ)) {
                continue; // Exclude if no longer valid
            }

            bool hasChanged = (std::abs(currentX - record.x) > 1e-6f) ||
                              (std::abs(currentY - record.y) > 1e-6f) ||
                              (std::abs(currentZ - record.z) > 1e-6f);

            bool hasUnchanged = (std::abs(currentX - record.x) <= 1e-6f) &&
                                 (std::abs(currentY - record.y) <= 1e-6f) &&
                                 (std::abs(currentZ - record.z) <= 1e-6f);

            if ((choice == '1' && hasUnchanged) ||
                (choice == '2' && hasChanged)) {
                // Exclude this record based on user choice
                continue;
            }

            // Update the record with the current values
            record.x = currentX;
            record.y = currentY;
            record.z = currentZ;

            // Add to the updated records
            updatedRecords.push_back(record);
            number_of_remaining_records++;
        }

        // Update the main records vector
        floatValueRecords = std::move(updatedRecords);

        std::cout << "Number of remaining float value records: " << number_of_remaining_records << std::endl;

        // If no records remain, exit the loop
        if (floatValueRecords.empty()) {
            std::cout << "No remaining float value records to monitor." << std::endl;
            break;
        }
    }

    if (!floatValueRecords.empty()) {
        // Group records by base address
        std::map<BYTE*, std::vector<FloatValueRecord>> groupedRecords;
        for (const auto& record : floatValueRecords) {
            groupedRecords[record.regionBase].push_back(record);
        }

        // Write the grouped records to file
        std::ofstream outFile("memory_records/float_values.txt");
        if (!outFile) {
            std::cerr << "Error opening file for writing float values." << std::endl;
            CloseHandle(hProcess);
            return;
        }

        // Write the module base address
        outFile << "Module Base Address: 0x" << std::hex << GetModuleBaseAddress(processID, "Overwatch.exe") << std::dec << std::endl;

        for (const auto& [baseAddr, records] : groupedRecords) {
           outFile << "Base Address: 0x" << std::hex << reinterpret_cast<uintptr_t>(baseAddr) << std::dec << "\n";
            for (const auto& record : records) {
                outFile << "    Address: 0x" << std::hex << reinterpret_cast<uintptr_t>(record.address) << std::dec << "\n";
                outFile << "    Coordinates: X = " << record.x << ", Y = " << record.y << ", Z = " << record.z << "\n";
                outFile << "    ----------------------------------------\n";
            }
            outFile << "\n"; // Add a newline for better readability between base addresses
        }

        outFile.close();
        std::cout << "Remaining float value records written to 'memory_records/float_values.txt'." << std::endl;
    } else {
        std::cout << "No float value records to write to file." << std::endl;
    }

    CloseHandle(hProcess);
}

// Define a structure to hold float pointer records
struct FloatPointerRecord {
    BYTE* regionBase;
    BYTE* pointerAddress;
    BYTE* coordinatesAddress;
    float x;
    float y;
    float z;
};

void ScanFloatPointers(DWORD processID) {
    HANDLE hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, processID);
    if (!hProcess) {
        std::cerr << "Unable to open process for scanning float pointers." << std::endl;
        return;
    }

    // Vector to store all valid float pointer records
    std::vector<FloatPointerRecord> floatPointerRecords;

    // Enumerate memory regions for the initial scan
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);

    MEMORY_BASIC_INFORMATION mem_info;
    BYTE* address = static_cast<BYTE*>(sysInfo.lpMinimumApplicationAddress);

    while (VirtualQueryEx(hProcess, address, &mem_info, sizeof(mem_info))) {
        if (address > static_cast<BYTE*>(sysInfo.lpMaximumApplicationAddress)) {
            break;
        }

        BYTE* regionBase = static_cast<BYTE*>(mem_info.BaseAddress);
        // Skip memory regions outside the valid range
        if((uintptr_t)regionBase < 0x1000000000ULL) {
            address += mem_info.RegionSize;
            continue;
        }
        if((uintptr_t)regionBase > 0x30000000000ULL) {
            break;
        }

        if (!IsValidMemoryRegion(mem_info)) {
            address += mem_info.RegionSize;
            continue;
        }

        SIZE_T bytesRead;
        SIZE_T regionSize = mem_info.RegionSize;
        // Calculate total number of pointers in the region
        SIZE_T pointerSize = sizeof(uintptr_t);
        SIZE_T totalPointers = regionSize / pointerSize;
        SIZE_T totalReadPointers = 0;
        // Define chunk size (number of pointers) to read at a time
        const SIZE_T chunkSize = 1000;
        std::vector<uintptr_t> buffer(chunkSize);

        while (totalReadPointers < totalPointers) {
            SIZE_T pointersToRead = std::min(chunkSize, totalPointers - totalReadPointers);
            SIZE_T bytesToReadChunk = pointersToRead * pointerSize;
            BYTE* basePtrAddress = regionBase + totalReadPointers * pointerSize;

            if (!ReadProcessMemory(hProcess, basePtrAddress, buffer.data(), bytesToReadChunk, &bytesRead)) {
                // Optionally, handle read errors
                break;
            }

            SIZE_T pointersRead = bytesRead / pointerSize;

            for (SIZE_T i = 0; i < pointersRead; ++i) {
                BYTE* currPtrAddress = basePtrAddress + i * pointerSize;
                BYTE* ptrValue = reinterpret_cast<BYTE*>(buffer[i]);
                // Skip if pointer value is out of range
                if ((uintptr_t)ptrValue < 0x1000000000ULL || (uintptr_t)ptrValue > 0x30000000000ULL) {
                    continue;
                }

                float coordinates[3];
                SIZE_T floatBytesRead;

                // Unified ReadProcessMemory to read X, Y, Z together
                if (!ReadProcessMemory(hProcess, ptrValue, coordinates, sizeof(coordinates), &floatBytesRead) || floatBytesRead != sizeof(coordinates)) {
                    continue; // Skip if unable to read all three coordinates
                }

                float x = coordinates[0];
                float y = coordinates[1];
                float z = coordinates[2];

                // Apply the filtering criteria using the helper function
                if (!IsValidCoordinate(x, y, z)) {
                    continue;
                }

                // Create and store the record with baseAddress
                FloatPointerRecord record;
                record.regionBase = regionBase; // Assign the base address
                record.pointerAddress = currPtrAddress;
                record.coordinatesAddress = ptrValue;
                record.x = x;
                record.y = y;
                record.z = z;
                floatPointerRecords.push_back(record);
            }

            totalReadPointers += pointersRead;
        }

        address += mem_info.RegionSize;
    }

    std::cout << "Initial scan complete. Number of float pointer records after signature matching: " 
              << floatPointerRecords.size() << std::endl;

    // Continue with the existing scanning loop and file writing...

    while (true) {
        std::cout << "\nChoose an option:\n"
                  << "1. Keep changed values\n"
                  << "2. Keep unchanged values\n"
                  << "3. Stop scanning and write to file\n"
                  << "Enter your choice (1, 2, or 3): ";
        char choice;
        std::cin >> choice;
        std::cin.ignore(); // Clear the newline character from the input buffer

        if (choice == '3') {
            break; // Exit the scanning loop to write to file
        } else if (choice != '1' && choice != '2') {
            std::cerr << "Invalid choice. Please enter 1, 2, or 3." << std::endl;
            continue;
        }

        // Vector to hold records that remain after exclusion
        std::vector<FloatPointerRecord> updatedRecords;
        unsigned int number_of_remaining_records = 0;

        for (auto& record : floatPointerRecords) {
            float currentCoordinates[3];
            SIZE_T bytesRead;

            // Unified ReadProcessMemory to read X, Y, Z together
            if (!ReadProcessMemory(hProcess, record.coordinatesAddress, currentCoordinates, sizeof(currentCoordinates), &bytesRead) 
                || bytesRead != sizeof(currentCoordinates)) {
                continue; // Skip if unable to read all three coordinates
            }

            float currentX = currentCoordinates[0];
            float currentY = currentCoordinates[1];
            float currentZ = currentCoordinates[2];

            // Reapply the filtering criteria
            if (!IsValidCoordinate(currentX, currentY, currentZ)) {
                continue; // Exclude if no longer valid
            }

            bool hasChanged = (std::abs(currentX - record.x) > 1e-6f) ||
                              (std::abs(currentY - record.y) > 1e-6f) ||
                              (std::abs(currentZ - record.z) > 1e-6f);

            bool hasUnchanged = (std::abs(currentX - record.x) <= 1e-6f) &&
                                 (std::abs(currentY - record.y) <= 1e-6f) &&
                                 (std::abs(currentZ - record.z) <= 1e-6f);

            if ((choice == '1' && hasUnchanged) ||
                (choice == '2' && hasChanged)) {
                // Exclude this record based on user choice
                continue;
            }

            // Update the record with the current values
            record.x = currentX;
            record.y = currentY;
            record.z = currentZ;

            // Add to the updated records
            updatedRecords.push_back(record);
            number_of_remaining_records++;
        }

        // Update the main records vector
        floatPointerRecords = std::move(updatedRecords);

        std::cout << "Number of remaining float pointer records: " << number_of_remaining_records << std::endl;

        // If no records remain, exit the loop
        if (floatPointerRecords.empty()) {
            std::cout << "No remaining float pointer records to monitor." << std::endl;
            break;
        }
    }

    if (!floatPointerRecords.empty()) {
        // Group records by baseAddress
        std::map<BYTE*, std::vector<FloatPointerRecord>> groupedRecords;
        for (const auto& record : floatPointerRecords) {
            groupedRecords[record.regionBase].push_back(record);
        }

        // Write the grouped records to file
        std::ofstream outFile("memory_records/float_pointers.txt");
        if (!outFile) {
            std::cerr << "Error opening file for writing float pointers." << std::endl;
            CloseHandle(hProcess);
            return;
        }
        // Write the module base address
        outFile << "Module Base Address: 0x" << std::hex << GetModuleBaseAddress(processID, "Overwatch.exe") << std::dec << std::endl;

        for (const auto& [baseAddr, records] : groupedRecords) {
            outFile << std::hex << "Base Address: " << reinterpret_cast<uintptr_t>(baseAddr) << std::endl;
            for (const auto& record : records) {
                float currentCoordinates[3];
                SIZE_T bytesRead;

                // Unified ReadProcessMemory to read X, Y, Z together1
                if (!ReadProcessMemory(hProcess, record.coordinatesAddress, currentCoordinates, sizeof(currentCoordinates), &bytesRead) 
                    || bytesRead != sizeof(currentCoordinates)) {
                    continue; // Skip if unable to read all three coordinates
                }

                float currentX = currentCoordinates[0];
                float currentY = currentCoordinates[1];
                float currentZ = currentCoordinates[2];

                outFile << "    Pointer address: 0x" << std::hex << reinterpret_cast<uintptr_t>(record.pointerAddress)
                        << " / Coordinates address: 0x" << std::hex << reinterpret_cast<uintptr_t>(record.coordinatesAddress) << "\n";
                outFile << std::dec; // Switch back to decimal for float values
                outFile << "    Coordinates: X = " << currentX << ", Y = " << currentY << ", Z = " << currentZ << "\n";
                outFile << "    ----------------------------------------\n";
            }
            outFile << "\n"; // Add a newline for better readability between base addresses
        }

        outFile.close();
        std::cout << "Remaining float pointer records written to 'memory_records/float_pointers.txt'." << std::endl;
    } else {
        std::cout << "No float pointer records to write to file." << std::endl;
    }

    CloseHandle(hProcess);
}

// Function to search for pointers that point to a specific address
void SearchForPointerToAddress(DWORD processID) {
    HANDLE hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, processID);
    if (!hProcess) {
        std::cerr << "Unable to open process for scanning." << std::endl;
        return;
    }

    uintptr_t targetAddress;
    std::cout << "Enter the target address (in hexadecimal): ";
    std::cin >> std::hex >> targetAddress;
    std::cin.ignore(); // Clear the newline character from the input buffer

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);

    MEMORY_BASIC_INFORMATION mem_info;
    BYTE *address = static_cast<BYTE *>(sysInfo.lpMinimumApplicationAddress);
    std::vector<BYTE *> foundPointers;

    // Scan memory regions for pointers
    while (VirtualQueryEx(hProcess, address, &mem_info, sizeof(mem_info))) {
        if (address > static_cast<BYTE *>(sysInfo.lpMaximumApplicationAddress)) {
            break;
        }

        if (!IsValidMemoryRegion(mem_info)) {
            address += mem_info.RegionSize;
            continue;
        }

        SIZE_T bytesRead;
        SIZE_T regionSize = mem_info.RegionSize;
        SIZE_T pointerSize = sizeof(uintptr_t);
        SIZE_T totalPointers = regionSize / pointerSize;
        SIZE_T totalReadPointers = 0;
        const SIZE_T chunkSize = 1000;
        std::vector<uintptr_t> buffer(chunkSize);

        while (totalReadPointers < totalPointers) {
            SIZE_T pointersToRead = std::min(chunkSize, totalPointers - totalReadPointers);
            SIZE_T bytesToReadChunk = pointersToRead * pointerSize;
            BYTE *basePtrAddress = static_cast<BYTE *>(mem_info.BaseAddress) + totalReadPointers * pointerSize;

            if (ReadProcessMemory(hProcess, basePtrAddress, buffer.data(), bytesToReadChunk, &bytesRead)) {
                SIZE_T pointersRead = bytesRead / pointerSize;
                for (SIZE_T i = 0; i < pointersRead; ++i) {
                    if (buffer[i] == targetAddress) {
                        foundPointers.push_back(basePtrAddress + i * pointerSize);
                    }
                }
                totalReadPointers += pointersRead;
            } else {
                break;
            }
        }
        address += mem_info.RegionSize;
    }

    if (!foundPointers.empty()) {
        std::cout << "Found pointers that point to the specified address:" << std::endl;
        for (const auto &ptr : foundPointers) {
            // Cast to uintptr_t for consistent output format
            std::cout << "Pointer at: 0x" << std::hex << reinterpret_cast<uintptr_t>(ptr) << std::endl;
        }

        // Optionally write results to file
        std::cout << "Press 'a' to save results to file, or any other key to skip: ";
        char input;
        std::cin >> input;
        if (input == 'a') {
            std::ofstream outFile("memory_records/pointers_to_address.txt");
            for (const auto &ptr : foundPointers) {
                outFile << "Pointer at: 0x" << std::hex << reinterpret_cast<uintptr_t>(ptr) << std::endl;
            }
            outFile.close();
            std::cout << "Pointers written to 'memory_records/pointers_to_address.txt'" << std::endl;
        }
    } else {
        std::cout << "No pointers found that point to the specified address." << std::endl;
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

    char choice;
    std::cout << "Choose an option:\n"
              << "1. Scan for coordinates\n"
              << "2. Scan for integer values\n"
              << "3. Scan for float pointers and coordinates\n"
              << "4. Scan for float values\n"
              << "5. Search for pointers to a specific address\n"
              << "Enter your choice: ";
    std::cin >> choice;
    std::cin.ignore();

    if (choice == '1') {
        ScanCoordinates<float>(processID); // Scan for float values
    } else if (choice == '2') {
        ScanInt<int>(processID); // Scan for integer values
    } else if (choice == '3') {
        ScanFloatPointers(processID); // Scan for float pointers and their coordinates
    } else if (choice == '4') {
        ScanFloatValues(processID); // Scan for float values
    } else if (choice == '5') {
        SearchForPointerToAddress(processID); // Search for pointers to a specific address
    } else {
        std::cerr << "Invalid choice." << std::endl;
    }

    return 0;
}