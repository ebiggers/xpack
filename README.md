# Introduction

XPACK is an experimental compression format.  It is intended to have better
performance than DEFLATE as implemented in the zlib library and also produce a
notably better compression ratio on most inputs.  The format is not yet stable.

XPACK has been inspired by the DEFLATE, LZX, and Zstd formats, among others.
Originally envisioned as a DEFLATE replacement, it won't necessarily see a lot
of additional development since other solutions such as Zstd seem to have gotten
much closer to that goal first (great job to those involved!).  But I am
releasing the code anyway for anyone who may find it useful.

# Format

Like many other common compression formats, XPACK is based on the LZ77 method
(decomposition into literals and length/offset copy commands) with a number of
tricks on top.  Features include:

* Increased sliding window, or "dictionary", size (like LZX and Zstd)
* Entropy encoding with finite state entropy (FSE) codes, also known as
  table-based asymmetric numeral systems (tANS) (like Zstd)
* Minimum match length of 2 (like LZX)
* Lowest three bits of match offsets can be entropy-encoded (like LZX)
* Aligned and verbatim blocks (like LZX)
* Recent match offsets queue with three entries (like LZX)
* Literals packed separately from matches, and with two FSE streams (like older
  Zstd versions)
* Literal runs (like Zstd)
* Concise FSE header (state count list) representation
* Optional preprocessing step for x86 machine code (like LZX)

# Implementation overview

libxpack is a library containing an optimized, portable implementation of an
XPACK compressor and decompressor.  Features include:

* Whole-buffer compression and decompression only
* Multiple compression levels
* Fast hash chains-based matchfinder
* Greedy and lazy parsers
* Decompressor uses Intel BMI2 instructions when supported

In addition, the following command-line programs using libxpack are implemented:

* xpack (or xunpack), a program which behaves like a standard UNIX command-line
  compressor such as gzip
* benchmark, a program for benchmarking in-memory compression and decompression

All files may be modified and/or redistributed under the terms of the MIT
license.  There is NO WARRANTY, to the extent permitted by law.  See the COPYING
file for details.

# Building

## For UNIX

Just run `make`.  You need GNU Make and either GCC or Clang.  There is no `make
install` yet; just copy the file(s) to where you want.

By default, all targets are built, including the library and programs.  `make
help` shows the available targets.  There are also several options which can be
set on the `make` command line.  See the Makefile for details.

## For Windows

MinGW (GCC) is the recommended compiler to use when building binaries for
Windows.  MinGW can be used on either Windows or Linux.  Use a command like:

    $ make CC=x86_64-w64-mingw32-gcc

Alternatively, a separate Makefile, `Makefile.msc`, is provided for the tools
that come with Visual Studio, for those who strongly prefer that toolchain.
