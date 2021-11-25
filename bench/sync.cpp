#include <string>

#include "exe.h"

using namespace exe;

unsigned do_some_math(unsigned x) {
  return just_value(x) | then([](unsigned x) { return x + 2; }) |
         then([](unsigned x) { return -x; }) | then([](unsigned x) { return x * 4; }) |
         then([](unsigned x) { return x - 3; }) | sync_await();
}

unsigned do_some_math_noexcept(unsigned x) noexcept {
  return just_value(x) | then([](unsigned x) { return x + 2; }) |
         then([](unsigned x) { return -x; }) | then([](unsigned x) { return x * 4; }) |
         then([](unsigned x) { return x - 3; }) | sync_await();
}

unsigned do_some_math_noexcept_lambdas(unsigned x) {
  return just_value(x) | then([](unsigned x) noexcept { return x + 2; }) |
         then([](unsigned x) noexcept { return -x; }) |
         then([](unsigned x) noexcept { return x * 4; }) |
         then([](unsigned x) noexcept { return x - 3; }) | sync_await();
}

std::string write_a_poem(std::string pfx) {
  return just_value(std::move(pfx)) | then([](std::string x) { return x + " lorem"; }) |
         then([](std::string x) { return x + " ipsum"; }) |
         then([](std::string x) { return x + " dolor"; }) |
         then([](std::string x) { return x + " sit"; }) | sync_await();
}
