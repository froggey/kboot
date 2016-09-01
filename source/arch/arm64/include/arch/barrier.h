// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#define DSB __asm__ volatile("dsb sy" ::: "memory")
#define ISB __asm__ volatile("isb" ::: "memory")
