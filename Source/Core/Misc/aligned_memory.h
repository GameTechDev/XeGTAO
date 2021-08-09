// see https://github.com/adobe/chromium/blob/master/LICENSE or chromium_license.txt
//
// file originally from https://github.com/adobe/chromium/blob/master/base/memory/aligned_memory.h

// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// AlignedMemory is a POD type that gives you a portable way to specify static
// or local stack data of a given alignment and size. For example, if you need
// static storage for a class, but you want manual control over when the object
// is constructed and destructed (you don't want static initialization and
// destruction), use AlignedMemory:
//
//   static AlignedMemory<sizeof(MyClass), ALIGNOF(MyClass)> my_class;
//
//   // ... at runtime:
//   new(my_class.void_data()) MyClass();
//
//   // ... use it:
//   MyClass* mc = my_class.data_as<MyClass>();
//
//   // ... later, to destruct my_class:
//   my_class.data_as<MyClass>()->MyClass::~MyClass();

#ifndef BASE_MEMORY_ALIGNED_MEMORY_H_
#define BASE_MEMORY_ALIGNED_MEMORY_H_
#pragma once

#include "compiler_specific.h"

namespace chromium {

namespace base {

// AlignedMemory is specialized for all supported alignments.
// Make sure we get a compiler error if someone uses an unsupported alignment.
template <size_t Size, size_t ByteAlignment>
struct AlignedMemory {};

#define BASE_DECL_ALIGNED_MEMORY(byte_alignment) \
    template <size_t Size> \
    class AlignedMemory<Size, byte_alignment> { \
     public: \
      ALIGNAS(byte_alignment) char data_[Size]; \
      void* void_data() { return reinterpret_cast<void*>(data_); } \
      const void* void_data() const { \
        return reinterpret_cast<const void*>(data_); \
      } \
      template<typename Type> \
      Type* data_as() { return reinterpret_cast<Type*>(void_data()); } \
      template<typename Type> \
      const Type* data_as() const { \
        return reinterpret_cast<const Type*>(void_data()); \
      } \
     private: \
      void* operator new(size_t); \
      void operator delete(void*); \
    }

// Specialization for all alignments is required because MSVC (as of VS 2008)
// does not understand ALIGNAS(ALIGNOF(Type)) or ALIGNAS(template_param).
// Greater than 4096 alignment is not supported by some compilers, so 4096 is
// the maximum specified here.
BASE_DECL_ALIGNED_MEMORY(1);
BASE_DECL_ALIGNED_MEMORY(2);
BASE_DECL_ALIGNED_MEMORY(4);
BASE_DECL_ALIGNED_MEMORY(8);
BASE_DECL_ALIGNED_MEMORY(16);
BASE_DECL_ALIGNED_MEMORY(32);
BASE_DECL_ALIGNED_MEMORY(64);
BASE_DECL_ALIGNED_MEMORY(128);
BASE_DECL_ALIGNED_MEMORY(256);
BASE_DECL_ALIGNED_MEMORY(512);
BASE_DECL_ALIGNED_MEMORY(1024);
BASE_DECL_ALIGNED_MEMORY(2048);
BASE_DECL_ALIGNED_MEMORY(4096);

}  // base

} // namespace chromium

#endif  // BASE_MEMORY_ALIGNED_MEMORY_H_