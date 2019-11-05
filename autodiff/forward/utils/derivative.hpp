//                  _  _
//  _   _|_ _  _|o_|__|_
// (_||_||_(_)(_|| |  |
//
// automatic differentiation made easier in C++
// https://github.com/autodiff/autodiff
//
// Licensed under the MIT License <http://opensource.org/licenses/MIT>.
//
// Copyright (c) 2018-2020 Allan Leal
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// C++ includes
#include <cassert>
#include <cstddef>

// autodiff includes
#include <autodiff/common/meta.hpp>
#include <autodiff/common/vectortraits.hpp>

#pragma once

namespace autodiff {
namespace detail {

template<typename... Args>
struct At
{
    std::tuple<Args&...> args;
    constexpr static auto numArgs = sizeof...(Args);
};

template<typename... Args>
struct Wrt
{
    std::tuple<Args...> args;
    constexpr static auto numArgs = sizeof...(Args);
};

template<typename... Args>
struct Along
{
    std::tuple<Args&...> args;
    constexpr static auto numArgs = sizeof...(Args);
};

/// The keyword used to denote the variables *with respect to* the derivative is calculated.
template<typename... Args>
auto wrt(Args&&... args)
{
    return Wrt<Args&&...>{ std::forward_as_tuple(std::forward<Args>(args)...) };
}

/// The keyword used to denote the derivative order *N* and the variable *with respect to* the derivative is calculated.
template<std::size_t N, typename Arg>
auto wrt(Arg&& arg) // Universal references are needed here in case rvalue references are used (e.g., Eigen tail or indexed views used inside wrt function)
{
    static_assert(N > 0);
    auto head = std::forward_as_tuple(std::forward<Arg>(arg));
    if constexpr (N == 1)
        return head;
    else return std::tuple_cat(head, wrt<N - 1>(arg));
}

/// The keyword used to denote the variables *at* which the derivatives are calculated.
template<typename... Args>
auto at(Args&... args)
{
    return At<Args&...>{ std::forward_as_tuple(args...) };
}

/// The keyword used to denote the direction vector *along* which the derivatives are calculated.
template<typename Arg>
auto along(Arg& arg)
{
    return Along<Arg&>{ std::forward_as_tuple(arg) };
}

/// The keyword used to denote the direction vector *along* which the derivatives are calculated.
template<typename... Args>
auto along(Args&&... args)
{
    return Along<Args...>{ std::forward_as_tuple(args...) };
}

/// Seed each dual number in the **wrt** list using its position as the derivative order to be seeded.
/// Using `seed(wrt(x, y, z), 1)` will set the 1st order derivative of `x`, the
/// 2nd order derivative of `y`, and the 3rd order derivative of `z` to 1. If
/// these dual numbers have order greater than 3, then the last dual number will
/// be used for the remaining higher-order derivatives. For example, if these
/// numbers are 5th order, than the 4th and 5th order derivatives of `z` will be
/// set to 1 as well. In this example, `wrt(x, y, z)` is equivalent to `wrt(x,
/// y, z, z, z)`. This automatic seeding permits derivatives `fx`, `fxy`,
/// `fxyz`, `fxyzz`, and `fxyzzz` to be computed in a more convenient way.
template<typename Var, typename... Vars, typename T>
auto seed(const Wrt<Var&, Vars&...>& wrt, T&& seedval)
{
    constexpr auto N = Order<Var>;
    constexpr auto size = 1 + sizeof...(Vars);
    static_assert(size <= N, "It is not possible to compute higher-order derivatives with order greater than that of the autodiff number (e.g., using wrt(x, x, y, z) will fail if the autodiff numbers in use have order below 4).");
    For<N>([&](auto i) constexpr {
        if constexpr (i < size)
            seed<i.index + 1>(std::get<i>(wrt.args), seedval);
        else
            seed<i.index + 1>(std::get<size - 1>(wrt.args), seedval); // use the last variable in the wrt list as the variable for which the remaining higher-order derivatives are calculated (e.g., derivatives(f, wrt(x), at(x)) will produce [f0, fx, fxx, fxxx, fxxxx] when x is a 4th order dual number).
    });
}

template<typename... Vars>
auto seed(const Wrt<Vars&...>& wrt)
{
    seed(wrt, 1.0);
}

template<typename... Vars>
auto unseed(const Wrt<Vars&...>& wrt)
{
    seed(wrt, 0.0);
}

template<typename... Args, typename... Vecs>
auto seed(const At<Args...>& at, const Along<Vecs...>& along)
{
    static_assert(sizeof...(Args) == sizeof...(Vecs));

    ForEach(at.args, along.args, [&](auto& arg, auto&& dir) constexpr {
        if constexpr (isVector<decltype(arg)>) {
            static_assert(isVector<decltype(dir)>);
            assert(arg.size() == dir.size());
            const size_t len = dir.size();
            for(size_t i = 0; i < len; ++i)
                seed<1>(arg[i], dir[i]);
        }
        else seed<1>(arg, dir);
    });
}

template<typename... Args>
auto unseed(const At<Args...>& at)
{
    ForEach(at.args, [&](auto& arg) constexpr {
        if constexpr (isVector<decltype(arg)>) {
            const size_t len = arg.size();
            for(size_t i = 0; i < len; ++i)
                seed<1>(arg[i], 0.0);
        }
        else seed<1>(arg, 0.0);
    });
}

template<typename Fun, typename... Args, typename... Vars>
auto eval(const Fun& f, const At<Args&...>& at, const Wrt<Vars&...>& wrt)
{
    seed(wrt);
    auto u = std::apply(f, at.args);
    unseed(wrt);
    return u;
}

template<typename Fun, typename... Args, typename... Vecs>
auto eval(const Fun& f, const At<Args&...>& at, const Along<Vecs...>& along)
{
    seed(at, along);
    auto u = std::apply(f, at.args);
    unseed(at);
    return u;
}

/// Unpack the derivatives from the result of an @ref eval call into an array.
template<typename Result>
auto derivatives(const Result& result)
{
    if constexpr (isVector<Result>) // check if the argument is a vector container of dual/real numbers
    {
        size_t len = result.size(); // the length of the vector containing dual/real numbers
        using NumType = decltype(result[0]); // get the type of the dual/real number
        using T = NumericType<NumType>; // get the numeric/floating point type of the dual/real number
        using Vec = VectorReplaceValueType<Result, T>; // get the type of the vector containing numeric values instead of dual/real numbers (e.g., vector<real> becomes vector<double>, VectorXdual becomes VectorXd, etc.)
        constexpr auto N = Order<NumType>; // the order of the dual/real number
        std::array<Vec, N + 1> values; // create an array to store the derivatives stored inside the dual/real number
        For<N + 1>([&](auto i) constexpr {
            values[i].resize(len);
            for(size_t j = 0; j < len; ++j)
                values[i][j] = derivative<i>(result[j]); // get the ith derivative of the jth dual/real number
        });
        return values;
    }
    else // result is then just a dual/real number
    {
        using T = NumericType<Result>; // get the numeric/floating point type of the dual/real result number
        constexpr auto N = Order<Result>; // the order of the dual/real result number
        std::array<T, N + 1> values; // create an array to store the derivatives stored inside the dual/real number
        For<N + 1>([&](auto i) constexpr {
            values[i] = derivative<i>(result);
        });
        return values;
    }
}

template<typename Fun, typename... Vars, typename... Args>
auto derivatives(const Fun& f, const Wrt<Vars&...>& wrt, const At<Args&...>& at)
{
    return derivatives(eval(f, at, wrt));
}

template<size_t order=1, typename Fun, typename... Vars, typename... Args, typename Result>
auto derivative(const Fun& f, const Wrt<Vars&...>& wrt, const At<Args&...>& at, Result& u)
{
    u = derivatives(f, wrt, at);
    return derivative<order>(u);
}

template<size_t order=1, typename Fun, typename... Vars, typename... Args>
auto derivative(const Fun& f, const Wrt<Vars&...>& wrt, const At<Args&...>& at)
{
    auto u = eval(f, at, wrt);
    return derivative<order>(u);
}

template<typename Fun, typename... Vecs, typename... Args>
auto derivatives(const Fun& f, const Along<Vecs...>& along, const At<Args&...>& at)
{
    return derivatives(eval(f, at, along));
}

} // namespace detail

using detail::derivatives;
using detail::derivative;
using detail::along;
using detail::wrt;
using detail::at;

} // namespace autodiff
