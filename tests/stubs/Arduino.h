#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <sstream>
#include <iomanip>
#include <type_traits>

#define IRAM_ATTR
inline int64_t testClockUs = 0;
inline unsigned long millis() { return static_cast<unsigned long>(testClockUs / 1000); }

class String {
    std::string value;
public:
    String() = default;
    String(const char* s) : value(s ? s : "") {}
    String(const std::string& s) : value(s) {}
    template<class T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
    String(T n) : value(std::to_string(n)) {}
    String(double n, int decimals = 2) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(decimals) << n;
        value = out.str();
    }
    const char* c_str() const { return value.c_str(); }
    size_t length() const { return value.length(); }
    String& operator+=(const String& other) { value += other.value; return *this; }
    friend String operator+(const String& a, const String& b) { return String(a.value + b.value); }
};

struct TestSerial {
    template<class... Args> void printf(const char*, Args...) {}
    template<class T> void println(const T&) {}
};
inline TestSerial Serial;
