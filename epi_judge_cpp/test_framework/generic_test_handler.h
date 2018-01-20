// @library
#pragma once

#include <iterator>
#include <numeric>
#include <regex>
#include <string>
#include <tuple>
#include <vector>

#include "test_output.h"
#include "test_utils.h"
#include "test_utils_meta.h"
#include "test_utils_serialization_traits.h"

/**
 * The central class in generic test runner framework.
 * It is responsible for asserting that the function signature matches
 * the one from the test file header and
 * executing tests on the provided function (which includes
 * the deserialization of the provided arguments
 * and the expected value,
 * invocation of the target method with these arguments and
 * comparison of the computed result with the expected value).
 *
 * ParseSignature() and RunTest() throw RuntimeException
 * in case of any error or assertion failure.
 * This exception terminates testing and,
 * consequently, the test program.
 * If tested method throws TestFailureException,
 * the current test is marked as failed and the execution goes on.
 * In case of any other exception thrown by the tested method,
 * the test program is terminated.
 */
template <typename Function, typename Comparator>
class GenericTestHandler {
 private:
  using has_default_comparator = std::is_same<Comparator, DefaultComparator>;

  using expected_value_t = typename SerializationTraits<std::conditional_t<
      has_default_comparator::value,
      typename FunctionalTraits<Function>::return_t,
      typename BiPredicateTraits<Comparator>::arg1_t>>::serialization_type;

  static_assert(
      std::is_same<expected_value_t, void>::value ==
          std::is_same<typename FunctionalTraits<Function>::return_t,
                       void>::value,
      "Expected type and return type are either both void or not");

  struct ExpectedIsVoidTag {};
  struct ExpectedIsValueTag {};
  using expected_tag =
      std::conditional_t<std::is_same<expected_value_t, void>::value,
                         ExpectedIsVoidTag, ExpectedIsValueTag>;

  Function func_;
  Comparator comp_;

 public:
  using test_output_t =
      TestOutput<expected_value_t,
                 typename FunctionalTraits<Function>::return_t>;

  GenericTestHandler(Function func_ptr, Comparator comp)
      : func_(func_ptr), comp_(comp) {}

  /**
   * This method ensures that test data header matches with the signature of
   * the tested function.
   *
   * @param arg_types - test data header
   */
  void ParseSignature(const std::vector<std::string>& arg_types) {
    using arg_tuple_t = typename FunctionalTraits<Function>::arg_tuple_t;
    using ret_t = typename FunctionalTraits<Function>::return_t;

    MatchFunctionSignature<expected_value_t, arg_tuple_t>(
        std::cbegin(arg_types), std::cend(arg_types));
  }

  /**
   * This method is invoked for each row
   * in a test data file (except the header).
   * It deserializes the list of arguments and
   * calls the user function with them.
   *
   * @param test_args - serialized arguments.
   * @return true if result generated by the user method
   *    matches with the expected value, false otherwise.
   */
  test_output_t RunTest(const std::vector<std::string>& test_args) const {
    using arg_tuple_t = typename FunctionalTraits<Function>::arg_tuple_t;
    using ret_t = typename FunctionalTraits<Function>::return_t;
    auto args = ParseSerializedArgs<arg_tuple_t>(
        std::cbegin(test_args),
        std::cend(test_args) - (ExpectedIsVoid() ? 0 : 1));
    return ParseExpectedAndInvoke(test_args.back(), args);
  }

  static constexpr bool ExpectedIsVoid() {
    return std::is_same<expected_tag, ExpectedIsVoidTag>::value;
  }

  static constexpr size_t ArgumentCount() {
    return std::tuple_size<
        typename FunctionalTraits<Function>::arg_tuple_t>::value;
  }

 private:
  /**
   * This method parses expected value (if return type is not void),
   * invokes the tested function and compares
   * the computed result with the expected value.
   *
   * The reason to put it in a separate function is that
   * the implementation differs in case the return type is void.
   * The two versions should be put in different functions and
   * the right overload is chosen with a tag dispatching.
   *
   * @param serialized_expected - string representation
   *    of the expected value or unknown string if return type is void.
   * @param args - deserialized function arguments, passed in a tuple.
   * @return tuple, that contains [result of comparison of expected and
   * result, optional<expected>, optional<result>].
   */
  template <typename ArgTuple>
  test_output_t ParseExpectedAndInvoke(const std::string& serialized_expected,
                                       ArgTuple& args) const {
    return ParseExpectedAndInvokeImpl(serialized_expected, args,
                                      expected_tag());
  }

  /**
   * ParseExpectedAndInvoke version for non-void return type
   */
  template <typename ArgTuple>
  test_output_t ParseExpectedAndInvokeImpl(
      const std::string& serialized_expected, ArgTuple& args,
      ExpectedIsValueTag) const {
    using arg_tuple_t = typename FunctionalTraits<Function>::arg_tuple_t;
    auto expected =
        SerializationTraits<expected_value_t>::Parse(serialized_expected);

    TestTimer timer;
    auto result = Invoke(
        timer, args,
        std::make_index_sequence<std::tuple_size<arg_tuple_t>::value>());

    return {comp_(expected, result), timer, std::move(expected),
            std::move(result)};
  }

  /**
   * ParseExpectedAndInvoke version for void return type
   */
  template <typename ArgTuple>
  test_output_t ParseExpectedAndInvokeImpl(
      const std::string& serialized_expected, ArgTuple& args,
      ExpectedIsVoidTag) const {
    using arg_tuple_t = typename FunctionalTraits<Function>::arg_tuple_t;
    TestTimer timer;
    Invoke(timer, args,
           std::make_index_sequence<std::tuple_size<arg_tuple_t>::value>());

    return {true, timer};
  }

  /**
   * Invokes the tested function with the provided set of arguments.
   * @param args - deserialized function arguments, passed in a tuple.
   * @return whatever the tested function returns
   */
  template <typename ArgTuple, std::size_t... I>
  decltype(auto) Invoke(TestTimer& timer, ArgTuple& args,
                        std::index_sequence<I...> /*unused*/) const {
    return FunctionalTraits<Function>::InvokeWithTimer(func_, timer,
                                                       std::get<I>(args)...);
  };
};
