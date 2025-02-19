#include <windows.h>
#include <vector>
#include <fstream>
#include <iostream>
#include <cstdint>
#include <string>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <tlhelp32.h> // For process snapshot

// Function to handle each address
void handlePlayersAddress(HANDLE hProcess, const std::string& addressStr);
void handleMyAddress(const std::string& addressStr);

HANDLE processOpener();