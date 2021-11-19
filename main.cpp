#include <iostream>
#include <type_traits>

namespace exe {
namespace detail {
template <typename... Ts>
struct type_list {
  template <template <typename> class P>
  using any_t = std::conjunction<P<Ts>...>;
};

template <typename S, typename U = S, typename = typename U::sender_tag>
std::true_type is_sender(int);
template <typename S>
std::false_type is_sender(...);

template <typename S, typename U = S>
auto sender_error_types(int) -> typename U::error_types;
template <typename S>
type_list<> sender_error_types(...);

template <typename E>
using is_exception_ptr_t = std::is_same<E, std::exception_ptr>;

template <bool B>
using throws_if = std::conditional_t<B, type_list<std::exception_ptr>, type_list<>>;

template <typename T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename Rec, typename U = Rec,
          typename = decltype(std::declval<U&&>().set_error(std::declval<std::exception_ptr>()))>
std::true_type has_set_error_exc_ptr(int);

template <typename Rec>
std::false_type has_set_error_exc_ptr(...);

}  // namespace detail

/// A simplistic sender detector.
template <typename S>
constexpr auto is_sender_v = decltype(detail::is_sender<S>(0))::value;

/// Returns empty list for non-senders.
template <typename S>
using sender_error_types_t = decltype(detail::sender_error_types<S>(0));

template <typename S>
constexpr auto is_noexcept_sender_v =
    is_sender_v<S> && !sender_error_types_t<S>::template any_t<detail::is_exception_ptr_t>::value;

namespace detail {
template <typename Rec, typename U = typename Rec::next_sender>
auto is_next_sender_noexcept(int) -> std::bool_constant<is_noexcept_sender_v<U>>;
template <typename Rec>
std::false_type is_next_sender_noexcept(...);
}  // namespace detail

template <typename Rec>
constexpr auto is_next_sender_noexcept_v = decltype(detail::is_next_sender_noexcept<Rec>(0))::value;

template <typename Rec>
constexpr auto has_set_error_exc_ptr_v = decltype(detail::has_set_error_exc_ptr<Rec>(0))::value;

template <typename Rec, typename T>
void set_value(Rec&& rec, T&& v) noexcept(noexcept(((Rec &&) rec).set_value((T &&) v))) {
  static_assert(!is_next_sender_noexcept_v<detail::remove_cvref_t<Rec>> ||
                    noexcept(((Rec &&) rec).set_value((T &&) v)),
                "Forwarding value to the noexcept sender `rec` might cause exceptions due to"
                " implicit conversions involved.");
  ((Rec &&) rec).set_value((T &&) v);
}

template <typename Rec, typename E>
void set_error(Rec&& rec, E&& e) noexcept {
  ((Rec &&) rec).set_error((E &&) e);
}

template <typename Op>
void start(Op&& op) noexcept(noexcept(((Op &&) op).start())) {
  ((Op &&) op).start();
}

template <typename InputSender>
struct capture_exc_sender;

template <typename Sender, typename Rec>
auto connect(Sender&& sender, Rec&& rec) noexcept(
    noexcept(std::declval<Sender&&>().connect(std::declval<Rec&&>()))) {
  if constexpr (has_set_error_exc_ptr_v<detail::remove_cvref_t<Rec>>) {
    return capture_exc_sender<Sender>{(Sender &&) sender}.connect((Rec &&) rec);
  } else {
    return ((Sender &&) sender).connect((Rec &&) rec);
  }
}

template <typename InputSender, typename Rec>
auto connect(capture_exc_sender<InputSender>&& sender,
             Rec&& rec) noexcept(noexcept(std::move(sender).connect((Rec &&) rec))) {
  return std::move(sender).connect((Rec &&) rec);
}

template <typename Sender, typename Rec>
using connect_t = decltype(connect(std::declval<Sender&&>(), std::declval<Rec&&>()));

template <typename InputSender>
struct capture_exc_sender;

template <typename InputSender, typename OutputRec>
struct capture_exc_op;

template <typename InputSender, typename OutputRec>
struct capture_exc_rec {
  using next_sender = capture_exc_sender<InputSender>;

  template <typename T>
  void set_value(T&& v) noexcept(noexcept(std::declval<OutputRec&&>().set_value((T &&) v))) {
    ((OutputRec &&) op.output).set_value((T &&) v);
  }

  capture_exc_op<InputSender, OutputRec>& op;
};

template <typename InputSender, typename Rec>
struct capture_exc_op {
  using input_op_t = connect_t<InputSender, capture_exc_rec<InputSender, Rec>>;

  explicit capture_exc_op(InputSender&& input, Rec&& output) noexcept
      : op{connect((InputSender &&) input, capture_exc_rec<InputSender, Rec>{*this})},
        output{(Rec &&) output} {}

  capture_exc_op(const capture_exc_op&) = delete;
  capture_exc_op(capture_exc_op&&) = delete;

  void start() noexcept(
      noexcept(real_start(std::bool_constant<is_noexcept_sender_v<InputSender>>{}))) {
    real_start(std::bool_constant<is_noexcept_sender_v<InputSender>>{});
  }

  template <typename = void>
  void real_start(std::true_type /* is_noexcept_sender_v<InputSender> */) noexcept(
      noexcept(start(std::declval<input_op_t&&>()))) {
    start((input_op_t &&) op);
  }

  template <typename = void>
  void real_start(std::false_type /* is_noexcept_sender_v<InputSender> */) noexcept {
    try {
      start((input_op_t &&) op);
    } catch (...) {
      set_error((Rec &&) output);
    }
  }

  input_op_t op;
  Rec output;
};

template <typename InputSender>
struct capture_exc_sender {
  using sender_tag = void;

  using value_types = typename InputSender::value_types;
  using error_types = detail::throws_if<!is_noexcept_sender_v<InputSender>>;

  template <typename Rec>
  auto connect(Rec&& rec) noexcept {
    return capture_exc_op<InputSender, Rec>{(InputSender &&) input, (Rec &&) rec};
  }

  InputSender input;
};

template <typename Rec, typename T>
struct just_value_op {
  void start() noexcept(noexcept(set_value(std::declval<Rec&&>(), std::declval<T&&>()))) {
    set_value((Rec &&) rec, (T &&) v);
  }

  Rec rec;
  T v;
};

template <typename T>
struct just_value_sender {
  using sender_tag = void;

  using value_types = detail::type_list<T>;
  using error_types = detail::type_list<>;

  template <typename Rec>
  auto connect(Rec&& rec) noexcept(std::is_nothrow_move_constructible_v<T>) {
    return just_value_op<Rec, T>{(Rec &&) rec, (T &&) v};
  }

  T v;
};

template <typename T>
auto just_value(T&& v) noexcept(std::is_nothrow_move_constructible_v<std::decay_t<T>>) {
  using D = std::decay_t<T>;
  return just_value_sender<D>{(D &&) v};
}

}  // namespace exe

int main() {
  std::cout << exe::just_value("Hello, World!").v << std::endl;
  return 0;
}