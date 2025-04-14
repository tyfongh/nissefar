#include <Nissefar.h>

int main(int argc, char **argv) {
  try {
    Nissefar nissefar{};
    nissefar.run();
  } catch (const std::exception &e) {
    std::cout << std::format("Failed to initialize bot: {}", e.what())
              << std::endl;
  }
}
