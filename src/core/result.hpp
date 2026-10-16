// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Patrick JAILLET

#pragma once
// Result<T> : valeur ou erreur (équivalent minimal de std::expected, C++23).
// Utilisé pour toutes les opérations d'I/O : pas d'exceptions dans le code applicatif.
#include <cassert>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace rigger {

struct Error {
    std::string message;
};

template <class T>
class Result {
public:
    Result(T value) : data_(std::move(value)) {}
    Result(Error error) : data_(std::move(error)) {}

    bool ok() const { return std::holds_alternative<T>(data_); }
    explicit operator bool() const { return ok(); }

    T& value() & {
        assert(ok());
        return std::get<T>(data_);
    }
    const T& value() const& {
        assert(ok());
        return std::get<T>(data_);
    }
    const Error& error() const {
        assert(!ok());
        return std::get<Error>(data_);
    }

private:
    std::variant<T, Error> data_;
};

template <>
class Result<void> {
public:
    Result() = default;
    Result(Error error) : error_(std::move(error)) {}

    bool ok() const { return !error_.has_value(); }
    explicit operator bool() const { return ok(); }
    const Error& error() const {
        assert(!ok());
        return *error_;
    }

private:
    std::optional<Error> error_;
};

}  // namespace rigger
