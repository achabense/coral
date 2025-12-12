#include <ctime>

#include "rule.hpp"

int main() {
    std::mt19937 rand((unsigned)std::time(0));
    iso3::test_all(rand);
}
