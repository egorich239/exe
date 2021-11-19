# A C++ sender/receiver model without pessimistic `std::exception_ptr`.

_Disclaimer_: This repository contains demo code, it is not intended as a full-featured replacement of either `unifex` or `std::execution`.
It takes multiple simplifications: there is not support CPO mechanics, a sender is determined by the presence of a dependent `sender_tag` type, 
`set_done()` call, etc. Nevertheless it reflects the core building blocks of `std::execution`.

tl;dr; Have a look at `main.cpp`, there are several examples, pay close attention to `static_assert()`-s on `is_noexcept_sender_v<Sender>`.

## Problem statement

The current state of P2300 pessimistically assumes that every sender might call `set_error(std::exception_ptr)` and every receiver should support
this `set_error()` signature. It can be partially attributed to the fact that it is challenging to determine what `error_types` exactly means for
exceptions, since they may pop up in various surprising parts of the program logic.

It also has unfortunate consequences. For example, there is a special rule about throwing from receiver's `set_value()` that allows the sender to
re-enter receiver into `set_error()` channel.

This demo proposes a model with clear ownership of thrown exceptions, that allows to avoid all the pitfalls of the above approach.

## Some goals

* Make it possible to have "noexcept" senders, i.e. the senders that do not have `std::exception_ptr` among their `error_types` variant;

* Avoid unneccessary try-catch blocks. E.g. a sequence of `auto x = source() | then() | ... | then() | consume_result()` must have no try-catch blocks.
A sequence `Expect<T, std::exception_ptr> x = source() | then() | ... | then() | expect()` should have 1 try-catch block.

* Clarify the rules of `noexcept(noexcept(...))` propagation for `set_value()`.

## Analysis

Let us first considers an inline sequence of senders, for example:

```c++
Expect<int> x = just_value(42)                 // #1
  | then([](int x) noexcept { return x * 2; }) // #2
  | then([](int x) noexcept { return x + 4; }) // #3
  | then([](int x) { if (rand() < 0.5) throw 12; else return x; } ) # 4
  | then([](int x) noexcept { return -x; })    // #5
  | consume_value();                           // #6
```

The deepest call stack that an unoptimised implementation of this algorithm will have would look like:

```
main()
...
...
start#6
start#5
start#4
start#3
start#2
start#1
set_value#2 (42)
set_value#3 (84)
set_value#4 (88)
set_value#5 (88)
set_value#6 (-88)

```

It is important to note that the whole chain of value propagations happens while all five start frames are present at the stack.

A naive approach to propagating an exception thrown at #4 is to wrap the lambda into try-catch block in the implementation of 
`then_op` and trigger `set_error#5(exc)` which in turn would trigger `set_error#6(exc)` and cause `consume_value()` to return the
`exception_ptr` instead of `int`. Such approach has significant drawbacks. Firstly, every `then_op()` implementation would have to
wrap lambda into try-catch block. More importantly: this will create a chain of 4 potentially nested try-catch blocks in place where
a human being would write just one.

The first cornerstone of my proposal is the following: let C++ do its thing - it knows how to throw exceptions. Since none of our
continuations catches the exception, we only need _one_ try-catch block, and this block must reside in `start#6` - i.e. in the `start()`\
routine of the operation implementing `consume_value()`.

This implies the second principle: `noexcept()` annotation of `set_value()` shall _NOT_ directly correspond to `error_types`. In fact they
must instead truthfully represent the `noexcept()` semantics of nested calls.

The third principle is: The default behaviour of a receiver w.r.t. an exception is to forward it to next receiver, i.e.

```c++
struct rec {
  next_rec& next;
  void set_error(std::exception_ptr exc) noexcept { std::execution::set_error(next, ptr); }
};
```

However if your receiver does _just that_, **omit** `set_error(std::exception_ptr)`, we will be able to generate better code, because every
explicit `set_error(std::exception_ptr)` in fact corresponds to a try/catch block. This principle has an additional benefit: if your C++
code lives in `-fno-exceptions` world, you won't have to clutter your code with these methods.

# Proposed rules

**1. `error_value` of a sender represents the errors that the sender might send from the `start()` of the corresponding operation until expressions
`e1, e2, ..., eN` and `rec` are evaluated in `set_value(rec, e1, e2, ..., eN)`. Types of `e1`, ... `eN` expressions must correspond to the types 
declared in exactly one signature of `value_types`.**

Lack of `std::exception_ptr` in `error_values` implies that this sender will **never** call `set_error(std::exception_ptr)`. It however can participate
in the stack unwinding, see below.

**2. In a chain of senders, the downstream sender accounts for the potential exceptions caused by implicit conversions during forwarding into its
`set_value(...)` method.** This means that `error_types` of the downstream sender should consider nothrow-constructibleness of `rec::set_value()`
arguments from the values of the types advertised by the upstream sender. In fact, that's really the only thing the downstream sender must do to
properly advertise exceptions.

**3. If exceptions happen, let them unwind the stack until someone can handle it. Represent the `noexcept(...)` properties of `set_value()` and 
`start()` based on what each of them actually does.**

**4. `connect(sender, rec)` checks whether `rec` implements `set_error(std::exception_ptr)`.** If it does, it transforms `sender -> sender | capture_exc()`
and _then_ connects the result to `rec`; `capture_exc()` simply wraps the execution sequence into trycatch block in `start()`, and calls 
`set_error(std::current_exception());` from the catch-all block.

**5. Sender should not throw after it called `set_*` on its receiver**.

