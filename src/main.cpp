
#include <iostream>
#include "models/Task.h"

int main() {
    Task task;

    task.id = "T1";
    task.title = "Thiet ke UML";
    task.priority = Priority::High;

    if (task.priority == Priority::High) {
        std::cout << "Task co do uu tien cao\n";
    }

    return 0;
}
