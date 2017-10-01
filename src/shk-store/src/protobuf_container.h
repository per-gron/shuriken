// Copyright 2017 Per Grön. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <utility>

namespace shk {

/**
 * Wraps (and owns) a protobuf object and exposes one of its repeated fields as
 * an STL-style container, for use with the From rs operator.
 */
template <
    typename Message,
    typename ElementType,
    int (Message::*GetSize)() const,
    ElementType *(Message::*GetElement)(int index)>
class ProtobufContainer {
 public:
  template <typename IteratorMessage, typename IteratorElementType>
  class Iterator {
   public:
    Iterator(IteratorMessage *message, int index)
        : message_(message), index_(index) {}

    IteratorElementType &operator*() {
      return *(message_->*GetElement)(index_);
    }

    const IteratorElementType &operator*() const {
      return *(message_->*GetElement)(index_);
    }

    Iterator &operator++() {
      index_++;
      return *this;
    }

    bool operator==(const Iterator &other) {
      return message_ == other.message_ && index_ == other.index_;
    }

   private:
    IteratorMessage *message_;
    int index_;
  };

  ProtobufContainer(Message &&message)
      : message_(std::move(message)) {}

  Iterator<Message, ElementType> begin() {
    return Iterator<Message, ElementType>(&message_, 0);
  }

  Iterator<const Message, const ElementType> begin() const {
    return Iterator<const Message, const ElementType>(&message_, 0);
  }

  Iterator<Message, ElementType> end() {
    return Iterator<Message, ElementType>(&message_, (message_.*GetSize)());
  }

  Iterator<const Message, const ElementType> end() const {
    return Iterator<const Message, const ElementType>(
        &message_, (message_.*GetSize())());
  }

 private:
  Message message_;
};

}  // namespace shk
