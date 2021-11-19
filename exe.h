#pragma once
#include <tuple>
#include <type_traits>
#include <utility>

namespace exe {
namespace detail {
template <typename... Ts>
struct type_list {
  template <template <typename> class P>
  using any_t = std::disjunction<P<Ts>...>;

  template <typename Fn>
  using invoke_result_t = std::invoke_result_t<Fn&&, Ts&&...>;

  template <typename Fn>
  using is_nothrow_invocable_t = std::is_nothrow_invocable<Fn&&, Ts&&...>;
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

template <template <class, class...> typename T, typename... Tail>
struct bind_construct_impl {
  using tail_t = std::tuple<std::decay_t<Tail>...>;

  template <typename S>
  auto operator()(S&& s) noexcept(
      noexcept(this->invoke((S &&) s, std::make_index_sequence<sizeof...(Tail)>{}))) {
    return invoke((S &&) s, std::make_index_sequence<sizeof...(Tail)>{});
  }

  template <typename S, size_t... Idx>
  auto invoke(S&& s, std::index_sequence<Idx...>) noexcept(noexcept(T<S, std::decay_t<Tail>...>{
      (S &&) s, std::get<Idx>(std::declval<tail_t&&>())...})) {
    return T<S, std::decay_t<Tail>...>{(S &&) s, std::get<Idx>(std::move(tail))...};
  }

  std::tuple<std::decay_t<Tail>...> tail;
};

template <template <class, class...> typename T, typename... Tail>
auto bind_construct(Tail&&... ts) noexcept {
  return bind_construct_impl<T, Tail...>{{(Tail &&) ts...}};
}

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
      noexcept(this->real_start(std::bool_constant<is_noexcept_sender_v<InputSender>>{}))) {
    real_start(std::bool_constant<is_noexcept_sender_v<InputSender>>{});
  }

  template <typename = void>
  void real_start(std::true_type /* is_noexcept_sender_v<InputSender> */) noexcept(
      noexcept(exe::start(std::declval<input_op_t&&>()))) {
    exe::start((input_op_t &&) op);
  }

  template <typename = void>
  void real_start(std::false_type /* is_noexcept_sender_v<InputSender> */) noexcept {
    try {
      exe::start((input_op_t &&) op);
    } catch (...) {
      exe::set_error((Rec &&) output, std::current_exception());
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
inline auto capture_exc() noexcept { return detail::bind_construct<capture_exc_sender>(); }

template <typename Rec, typename T>
struct just_value_op {
  just_value_op(const just_value_op&) = delete;
  just_value_op(just_value_op&&) = delete;

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

template <typename InputSender, typename Callback>
struct then_sender;
template <typename InputSender, typename Callback, typename OutputRec>
struct then_rec;
template <typename InputSender, typename Callback, typename OutputRec>
struct then_op;

template <typename InputSender, typename Callback, typename OutputRec>
struct then_rec {
  using next_sender = then_sender<InputSender, Callback>;

  template <typename T>
  void set_value(T&& v) noexcept(noexcept(set_value(std::declval<OutputRec&&>(),
                                                    (std::declval<Callback&&>())((T &&) v)))) {
    exe::set_value((OutputRec &&) op.rec, ((Callback &&) op.fn)((T &&) v));
  }

  then_op<InputSender, Callback, OutputRec>& op;
};

template <typename InputSender, typename Callback, typename OutputRec>
struct then_op {
  using input_op_t = connect_t<InputSender, then_rec<InputSender, Callback, OutputRec>>;

  explicit then_op(InputSender&& sender, Callback&& fn, OutputRec&& rec) noexcept
      : op{connect((InputSender &&) sender, then_rec<InputSender, Callback, OutputRec>{*this})},
        fn{(Callback &&) fn},
        rec{(OutputRec &&) rec} {}
  then_op(then_op&&) = delete;
  then_op(const then_op&) = delete;

  void start() noexcept(noexcept(exe::start((input_op_t &&) op))) {
    exe::start((input_op_t &&) op);
  }

  input_op_t op;
  Callback fn;
  OutputRec rec;
};

template <typename InputSender, typename Callback>
struct then_sender {
  using sender_tag = void;

  using value_types = typename InputSender::value_types::template invoke_result_t<Callback>;
  using error_types = detail::throws_if<
      !is_noexcept_sender_v<InputSender> ||
      !InputSender::value_types::template is_nothrow_invocable_t<Callback>::value>;

  template <typename Rec>
  auto connect(Rec&& rec) noexcept(
      std::is_nothrow_constructible_v<then_op<InputSender, Callback, Rec>, InputSender&&,
                                      Callback&&, Rec&&>) {
    return then_op<InputSender, Callback, Rec>{(InputSender &&) input, (Callback &&) fn,
                                               (Rec &&) rec};
  }

  InputSender input;
  Callback fn;
};

template <typename Callback>
auto then(Callback&& fn) noexcept {
  return detail::bind_construct<then_sender>((Callback &&) fn);
}

template <typename InputSender, typename Callback>
struct catch_exc_sender;
template <typename InputSender, typename Callback, typename OutputRec>
struct catch_exc_rec;
template <typename InputSender, typename Callback, typename OutputRec>
struct catch_exc_op;

template <typename InputSender, typename Callback, typename OutputRec>
struct catch_exc_rec {
  using next_sender = catch_exc_sender<InputSender, Callback>;

  template <typename T>
  void set_value(T&& v) noexcept(noexcept(exe::set_value(std::declval<OutputRec&&>(),
                                                         std::declval<T&&>()))) {
    exe::set_value(((OutputRec &&) op.rec), (T &&) v);
  }

  template <typename U = void, typename = std::enable_if_t<!is_noexcept_sender_v<InputSender>, U>>
  void set_error(std::exception_ptr exc) noexcept(
      noexcept(std::declval<Callback&&>()(std::declval<OutputRec&&>(), std::move(exc)))) {
    ((Callback &&) op.fn)((OutputRec &&) op.rec, std::move(exc));
  }

  catch_exc_op<InputSender, Callback, OutputRec>& op;
};

template <typename InputSender, typename Callback, typename OutputRec>
struct catch_exc_op {
  using input_op_t = connect_t<InputSender, catch_exc_rec<InputSender, Callback, OutputRec>>;

  explicit catch_exc_op(InputSender&& input, Callback&& fn, OutputRec&& rec) noexcept
      : op{exe::connect((InputSender &&) input,
                        catch_exc_rec<InputSender, Callback, OutputRec>{*this})},
        fn((Callback &&) fn),
        rec((OutputRec &&) rec) {}
  catch_exc_op(catch_exc_op&&) = delete;
  catch_exc_op(const catch_exc_op&) = delete;

  void start() noexcept(noexcept(exe::start(std::declval<input_op_t>()))) { exe::start(op); }

  input_op_t op;
  Callback fn;
  OutputRec rec;
};

template <typename InputSender, typename Callback>
struct catch_exc_sender {
  using sender_tag = void;

  using value_types = typename InputSender::value_types;
  using error_types = detail::throws_if<!is_noexcept_sender_v<InputSender>>;

  template <typename Rec>
  auto connect(Rec&& rec) noexcept(
      std::is_nothrow_constructible_v<catch_exc_op<InputSender, Callback, Rec>, InputSender&&,
                                      Callback&&, Rec&&>) {
    return catch_exc_op<InputSender, Callback, Rec>{(InputSender &&) input, (Callback &&) fn,
                                                    (Rec &&) rec};
  }

  InputSender input;
  Callback fn;
};

template <typename Callback>
auto catch_exc(Callback&& fn) noexcept {
  return detail::bind_construct<catch_exc_sender>((Callback &&) fn);
}

template <typename Sender, typename Rec, typename = std::enable_if_t<is_sender_v<Sender>>>
auto operator|(Sender&& sender, Rec&& rec) noexcept(noexcept(((Rec &&) rec)((Sender &&) sender))) {
  return ((Rec &&) rec)((Sender &&) sender);
}

}  // namespace exe
