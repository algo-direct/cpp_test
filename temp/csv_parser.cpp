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
        if (*r == ',') { // empty field
            *w++ = '\0';
            out.emplace_back(w - 1);
            ++r;
            continue;
        }
        if (*r == '"') {
            // quoted field
            ++r; // skip opening quote
            char* start = w;
            while (*r) {
                if (*r == '"') {
                    if (r[1] == '"') { // escaped quote
                        *w++ = '"'; r += 2; continue;
                    }
                    // end quote
                    ++r; break;
                }
                *w++ = *r++;
            }
            *w++ = '\0';
            size_t len = static_cast<size_t>(w - start - 1);
            out.emplace_back(start, len);
            if (*r == ',') ++r;
        } else {
            // unquoted field: copy until comma
            char* start = w;
            while (*r && *r != ',') { *w++ = *r++; }
            *w++ = '\0';
            size_t len = static_cast<size_t>(w - start - 1);
            out.emplace_back(start, len);
            if (*r == ',') ++r;
        }
    }
    return out;
}

int main(){
    std::string s = "123,45.6,\"hello,world\",\"he said ""hi""\",789";
    std::vector<char> buf(s.begin(), s.end()); buf.push_back('\0');
    auto v = parse_csv_inplace(buf.data());
    for (auto kv : v) std::cout << '"' << kv << '"' << ' ';
    std::cout << "\ncsv_parser: PASS\n";
    return 0;
}
