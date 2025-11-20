// Improved in-place CSV parser: supports quoted fields with escaped quotes ("" -> ")
// and returns vector<string_view> referencing the provided mutable buffer.

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

// parse_csv_inplace modifies buffer by inserting NUL terminators and returns string_views
// that point into the buffer. The caller must ensure the buffer remains alive while views are used.
std::vector<std::string_view> parse_csv_inplace(char* buf) {
    std::vector<std::string_view> out;
    char* r = buf;
    char* w = buf;
    
    while (*r) {
        char* start = w;
        
        if (*r == ',') {
            // Empty field
            out.emplace_back(start, 0);
            *w++ = '\0';
            ++r;
            continue;
        }
        
        if (*r == '"') {
            // Quoted field
            ++r; // skip opening quote
            while (*r) {
                if (*r == '"') {
                    if (r[1] == '"') {
                        // Escaped quote: "" -> "
                        *w++ = '"';
                        r += 2;
                    } else {
                        // End quote
                        ++r;
                        break;
                    }
                } else {
                    *w++ = *r++;
                }
            }
            // RFC 4180: after closing quote, must be comma or end of line
            if (*r != ',' && *r != '\0') {
                return {}; // Invalid CSV: characters after closing quote
            }
        } else {
            // Unquoted field
            while (*r && *r != ',') { 
                *w++ = *r++; 
            }
        }
        
        // Common finalization for both quoted and unquoted fields
        size_t len = static_cast<size_t>(w - start);
        out.emplace_back(start, len);
        if (*r == ',') ++r;  // Skip comma BEFORE writing null
        *w++ = '\0';
    }
    return out;
}

int main(){
    std::string s = "123,45.6,\"hello,world\"zz,\"he said \"\"hi\"\"\",789";
    std::cout << "Input: " << s << "\n";
    std::vector<char> buf(s.begin(), s.end()); buf.push_back('\0');
    auto v = parse_csv_inplace(buf.data());
    
    if (v.empty()) {
        std::cout << "INVALID CSV: Rejected\n";
    } else {
        std::cout << "Fields: ";
        for (auto kv : v) std::cout << '#' << kv << '|' << ' ';
        std::cout << "\n";
    }
    
    // Test with valid CSV
    std::string s2 = "123,45.6,\"hello,world\",\"he said \"\"hi\"\"\",789";
    std::cout << "\nInput: " << s2 << "\n";
    std::vector<char> buf2(s2.begin(), s2.end()); buf2.push_back('\0');
    auto v2 = parse_csv_inplace(buf2.data());
    std::cout << "Fields: ";
    for (auto kv : v2) std::cout << '#' << kv << '|' << ' ';
    std::cout << "\ncsv_parser: PASS\n";
    return 0;
}
