#include <DiffUtil.h>

#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect_true(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    ++failures;
  }
}

void test_diff_without_transpose() {
  const std::string old_csv = "A,B\nx,1\ny,2\n";
  const std::string new_csv = "A,B\nx,1\ny,3\n";

  const std::string diff = diff_csv(old_csv, new_csv, 9101);

  expect_true(diff.find("-y,2") != std::string::npos,
              "regular diff includes removed row");
  expect_true(diff.find("+y,3") != std::string::npos,
              "regular diff includes added row");
}

void test_diff_with_transpose() {
  const std::string old_csv = "SoC,Car1,Car2\n10,100,90\n20,80,70\n";
  const std::string new_csv = "SoC,Car1,Car2\n10,100,95\n20,80,70\n";

  const std::string diff = diff_csv(old_csv, new_csv, 9102, true);

  expect_true(diff.find("-Car2,90,70") != std::string::npos,
              "transposed diff includes removed car row");
  expect_true(diff.find("+Car2,95,70") != std::string::npos,
              "transposed diff includes added car row");
}

} // namespace

int main() {
  test_diff_without_transpose();
  test_diff_with_transpose();

  if (failures != 0) {
    std::cerr << failures << " test failure(s)\n";
    return 1;
  }

  std::cout << "All DiffUtil tests passed\n";
  return 0;
}
