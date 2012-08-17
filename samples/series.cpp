#include <iostream>

int counter = 1;

int series() {
    int32_t sum = 0;

    for (int32_t i = 0; i < 10; ++i) {
        sum += counter++;
    }

    return sum;
}

int main() {
    int val = series();
    std::cout << "Series: " << val << std::endl;

    return 0;
}
