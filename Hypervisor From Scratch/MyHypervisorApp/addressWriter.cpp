#include <fstream>
#include <string>

bool appendToFile(const std::string& filePath, const std::string& content) {
    std::ofstream file(filePath, std::ios::app);  // Open file in append mode
    if (!file.is_open()) {
        return false;  // Failed to open the file
    }

    file << content;   // Append content to file
    file.close();      // Close the file
    return true;       // Indicate success
}