#include "main.h"
#include "string_utils.h"
#include <algorithm>
#include <cctype>
#include <sstream>


// Helper function to split a string by a delimiter
std::vector<std::string> splitString(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(str);
    while (std::getline(tokenStream, token, delimiter)) {
        // Optionally trim whitespace from each token
        token.erase(token.find_last_not_of(" \n\r\t")+1);
        token.erase(0, token.find_first_not_of(" \n\r\t"));
        tokens.push_back(token);
    }
    return tokens;
}

// Helper function to check if a string represents a valid integer
bool isInteger(const std::string& s) {
    if (s.empty()) return false;
    return std::all_of(s.begin(), s.end(), ::isdigit);
} 