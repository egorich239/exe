#include <cstring>
#include <iostream>

#include "exe.h"

using namespace exe;

auto test_just_value() {
  auto s = just_value("Hello world");
  static_assert(is_sender_v<decltype(s)>);
  static_assert(is_noexcept_sender_v<decltype(s)>);
  return s;
}

auto test_pipe_just_value_to_then() {
  auto s = just_value("Hello world") | then([](const char* s) noexcept { return strlen(s); });
  static_assert(is_sender_v<decltype(s)>);
  static_assert(is_noexcept_sender_v<decltype(s)>);
  return s;
}

auto test_pipe_just_value_to_then_str() {
  auto s = just_value("Hello world") | then([](std::string s) noexcept { return s.length(); });
  static_assert(is_sender_v<decltype(s)>);
  static_assert(!is_noexcept_sender_v<decltype(s)>);
  return s;
}

template <bool NoExcSpec>
auto test_pipe_with_on_exc() {
  auto s = just_value(42) | catch_exc([](std::exception_ptr exc) noexcept {
             try {
               rethrow_exception(exc);
             } catch (int x) {
               return x * 2;
             }
           }) |
           then([](auto) -> int { throw 12; }) | catch_exc([](std::exception_ptr exc) noexcept(NoExcSpec) {
             try {
               rethrow_exception(exc);
             } catch (int x) {
               return x * 3;
             }
           });
  static_assert(is_noexcept_sender_v<decltype(s)> == NoExcSpec);
  return s;
}

struct recv {
  template <typename T>
  void set_value(T&& v) noexcept {
    std::cout << "Value: " << v << "\n";
  }

  void set_error(std::exception_ptr exc) noexcept {
    try {
      rethrow_exception(exc);
    } catch (std::exception& e) {
      std::cout << "Exc: " << e.what() << "\n";
    } catch (int x) {
      std::cout << "Exc: " << x << "\n";
    }
  }
};

struct strlen_recv {
  void set_value(std::string s) noexcept { std::cout << "strlen = " << s.length() << "\n"; }
};

template <typename Rec, typename Sender>
void run(Sender&& sender) {
  auto op = connect((Sender &&) sender, Rec{});
  exe::start(op);
}

int main() {
  static_assert(!is_next_sender_noexcept_v<recv>);
  run<recv>(test_just_value());
  run<recv>(test_pipe_just_value_to_then());
  run<recv>(test_pipe_just_value_to_then_str());
  run<recv>(test_pipe_with_on_exc<false>());
  run<recv>(test_pipe_with_on_exc<true>());

#if 0
  run<strlen_recv>(test_just_value());
#endif
  return 0;
}
