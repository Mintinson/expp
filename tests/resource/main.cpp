#include <iostream>
#include <vector>


int main() {
    std::vector<int> myVector = {1, 2, 3, 4, 5};

    // Print the elements of the vector
    for (const auto& element : myVector) {
        std::cout << element << " ";
    }
    std::cout << std::endl;

    return 0;
}