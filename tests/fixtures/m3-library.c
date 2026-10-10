// SPDX-License-Identifier: GPL-3.0-or-later
static __thread long value = 7;
long probe(long input) { value += input; return value; }
