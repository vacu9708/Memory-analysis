#include <windows.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <tlhelp32.h>
#include <algorithm>

using namespace std;

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

// Structure to hold position addresses
struct MyPosition {
    uintptr_t facing_direction_address;
    uintptr_t coordinates_address;
};

// Function to read three floats from a given address in the target process
bool Read3Floats(HANDLE hProcess, uintptr_t address, float (&outValues)[3])
{
    SIZE_T bytesRead;
    // Attempt to read 12 bytes (3 floats) from the target process's memory
    if (!ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(address), outValues, sizeof(float) * 3, &bytesRead))
    {
        std::cerr << "Failed to read memory at address: 0x" << std::hex << address << std::dec
                  << ". Error: " << GetLastError() << std::endl;
        return false;
    }

    // Ensure that exactly 12 bytes were read
    if (bytesRead != sizeof(float) * 3)
    {
        std::cerr << "Incomplete read at address: 0x" << std::hex << address << std::dec << std::endl;
        return false;
    }
    // Ensure that 3 consecutive floats are not all close to zero
    if (abs(outValues[0]) < 0.1f || abs(outValues[1]) < 0.1f || abs(outValues[2]) < 0.1f)
    {
        std::cerr << "All values are zero at address: 0x" << std::hex << address << std::dec << std::endl;
        return false;
    }
    // Ensure nan values are not present
    if (isnan(outValues[0]) || isnan(outValues[1]) || isnan(outValues[2]))
    {
        std::cerr << "Nan values detected at address: 0x" << std::hex << address << std::dec << std::endl;
        return false;
    }
    return true;
}

// Function to determine if two sets of three floats are similar within a small difference
bool AreValuesSimilar(const float a[3], const float b[3])
{
    int count = 0;
    if (std::abs(a[0] - b[0]) <= 1.5f &&
        std::abs(a[2] - b[2]) <= 1.5f)
        return true;
    return false;
}

int get_playersAddresses_deprecated(HANDLE hProcess, std::vector<uintptr_t>& playersAddresses, MyPosition& myPos)
{
    // Open the input text file containing addresses
    std::string playersAddressesPath = "D:\\hacking\\hypervisor\\aimbot\\playersAddresses.txt";
    std::ifstream infile(playersAddressesPath);
    if (!infile)
    {
        std::cerr << "Failed to open file: " << playersAddressesPath << std::endl;
        CloseHandle(hProcess);
        return 1;
    }

    std::string line;
    while (std::getline(infile, line))
    {
        std::stringstream ss(line);
        std::string addressStr;
        ss >> addressStr;

        if (addressStr.empty())
            continue;

        uintptr_t address = 0;
        try
        {
            address = std::stoull(addressStr, nullptr, 16);
        }
        catch (const std::exception& e)
        {
            std::cerr << "Invalid address format: " << addressStr << std::endl;
            continue;
        }

        if (address == 0)
        {
            std::cerr << "Address cannot be zero: " << addressStr << std::endl;
            continue;
        }

        // Check if the address is already in the array
        if (std::find(playersAddresses.begin(), playersAddresses.end(), address) != playersAddresses.end())
            continue;

        float currentValues[3];
        if (!Read3Floats(hProcess, address, currentValues))
        {
            continue;
        }

        bool similarFound = false;
        for (const auto& existingAddress : playersAddresses)
        {
            float existingValues[3];
            if (!Read3Floats(hProcess, existingAddress, existingValues))
            {
                continue;
            }

            if (AreValuesSimilar(currentValues, existingValues))
            {
                similarFound = true;
                break;
            }
        }
        // Compare with MyPosition's coordinates
        if (!similarFound)
        {
            float myCoordinates[3];
            if (Read3Floats(hProcess, myPos.coordinates_address, myCoordinates))
            {
                if (AreValuesSimilar(currentValues, myCoordinates))
                {
                    similarFound = true;
                }
            }
        }

        if (similarFound)
            continue;

        playersAddresses.push_back(address);
    }
    return 0;
}

int get_playersAddresses(HANDLE hProcess, std::vector<uintptr_t>& playersAddresses, MyPosition& myPos)
{
    // Open the input text file containing addresses
    std::string playersAddressesPath = "D:\\hacking\\hypervisor\\aimbot\\playersAddresses.txt";
    std::ifstream infile(playersAddressesPath);
    if (!infile)
    {
        std::cerr << "Failed to open file: " << playersAddressesPath << std::endl;
        CloseHandle(hProcess);
        return 1;
    }
    std::string line;
    if (!std::getline(infile, line))
    {
        std::cerr << "Failed to read base address from file: " << playersAddressesPath << std::endl;
        return false;
    }

    // Trim whitespace
    line.erase(std::remove_if(line.begin(), line.end(), ::isspace), line.end());

    if (line.empty())
    {
        std::cerr << "Base address is empty in file: " << playersAddressesPath << std::endl;
        return false;
    }

    uintptr_t baseAddress = 0;
    try
    {
        baseAddress = std::stoull(line, nullptr, 16);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Invalid base address format: " << line << std::endl;
        return false;
    }

    if (baseAddress == 0)
    {
        std::cerr << "Base address cannot be zero." << std::endl;
        return false;
    }

    const uintptr_t offsetBetweenSets = 0x50; // 80 bytes

    // Push the base address of each set to the playersAddresses vector
    for(int i = -10; i <= 10; ++i)
    {
        uintptr_t playerAddress = baseAddress + i * offsetBetweenSets;
        // Compare with MyPosition's coordinates
        float currentValues[3];
        if (!Read3Floats(hProcess, playerAddress, currentValues))
            continue;
        float myCoordinates[3];
        if (Read3Floats(hProcess, myPos.coordinates_address, myCoordinates))
        {
            if (AreValuesSimilar(currentValues, myCoordinates))
                continue;
        }
        playersAddresses.push_back(playerAddress);
    }
    return true;
}

// Function to get MyPosition by reading "myAddress.txt" and storing addresses
bool getMyPosition(HANDLE hProcess, MyPosition& pos)
{
    // Path to the myAddress.txt file
    std::string myAddressPath = "D:\\hacking\\hypervisor\\aimbot\\myAddress.txt";
    std::ifstream infile(myAddressPath);
    if (!infile)
    {
        std::cerr << "Failed to open file: " << myAddressPath << std::endl;
        return false;
    }

    std::string line;
    if (!std::getline(infile, line))
    {
        std::cerr << "Failed to read base address from file: " << myAddressPath << std::endl;
        return false;
    }

    // Trim whitespace
    line.erase(std::remove_if(line.begin(), line.end(), ::isspace), line.end());

    if (line.empty())
    {
        std::cerr << "Base address is empty in file: " << myAddressPath << std::endl;
        return false;
    }

    uintptr_t baseAddress = 0;
    try
    {
        baseAddress = std::stoull(line, nullptr, 16);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Invalid base address format: " << line << std::endl;
        return false;
    }

    if (baseAddress == 0)
    {
        std::cerr << "Base address cannot be zero." << std::endl;
        return false;
    }

    // Calculate addresses
    uintptr_t facingDirectionAddress = baseAddress + 0x20;
    uintptr_t coordinatesAddress = baseAddress + 0x30;

    // Store the addresses in the MyPosition structure
    pos.facing_direction_address = facingDirectionAddress;
    pos.coordinates_address = coordinatesAddress;

    return true;
}

int process_position_addresses(MyPosition& myPos, std::vector<uintptr_t>& playersAddresses) {
    // Step 1: Get the Process ID of Overwatch.exe
    DWORD processID = GetProcessID(L"Overwatch.exe");
    if (processID == 0)
    {
        std::cerr << "Process Overwatch.exe not found." << std::endl;
        return 1;
    }

    // Step 2: Open the target process with necessary access rights
    HANDLE hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, processID);
    if (hProcess == NULL)
    {
        std::cerr << "Failed to open process with PID: " << processID
                  << ". Error: " << GetLastError() << std::endl;
        return 1;
    }

    // Step 3: Get MyPosition
    
    if (getMyPosition(hProcess, myPos))
    {
        // Read and print the facing direction values
        float facingDirectionValues[3];
        if (Read3Floats(hProcess, myPos.facing_direction_address, facingDirectionValues))
        {
            std::cout << "My facing direction address: 0x" << std::hex << myPos.facing_direction_address << std::dec
                      << " Values: "
                      << std::fixed << std::setprecision(6)
                      << facingDirectionValues[0] << ", "
                      << facingDirectionValues[1] << ", "
                      << facingDirectionValues[2] << std::endl;
        }
        else
        {
            std::cerr << "Failed to read facing direction values." << std::endl;
        }

        // Read and print the coordinates values
        float coordinatesValues[3];
        if (Read3Floats(hProcess, myPos.coordinates_address, coordinatesValues))
        {
            std::cout << "My coordinates address: 0x" << std::hex << myPos.coordinates_address << std::dec
                      << " Values: "
                      << std::fixed << std::setprecision(6)
                      << coordinatesValues[0] << ", "
                      << coordinatesValues[1] << ", "
                      << coordinatesValues[2] << std::endl;
        }
        else
        {
            std::cerr << "Failed to read coordinates values." << std::endl;
        }
    }
    else
    {
        std::cerr << "Failed to retrieve MyPosition." << std::endl;
    }

    // Step 4: Retrieve and display player addresses
    get_playersAddresses(hProcess, playersAddresses, myPos);
    std::cout << "Total player addresses added: " << playersAddresses.size() << std::endl;
    for (const auto& address : playersAddresses)
    {
        float currentValues[3];
        if (Read3Floats(hProcess, address, currentValues))
        {
            std::cout << "Player address: 0x" << std::hex << address << std::dec
                      << " Values: "
                      << std::fixed << std::setprecision(6)
                      << currentValues[0] << ", "
                      << currentValues[1] << ", "
                      << currentValues[2] << std::endl;
        }
        else
        {
            std::cerr << "Failed to read values at address: 0x" << std::hex << address << std::dec << std::endl;
        }
    }

    // Step 5: Close the handle to the target process
    CloseHandle(hProcess);

    return 0;
}

// int main() {
//     MyPosition myPos;
//     std::vector<uintptr_t> playersAddresses;
//     process_position_addresses(myPos, playersAddresses);

//     return 0;
// }