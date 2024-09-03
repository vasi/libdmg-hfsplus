# README

Ever wanted to convert a raw partition to .dmg, _fast_? This repo is what you need!

See also [the original README for libdmg-hfsplus](README.orig.markdown).

## Why?

Sometimes you have really big files to convert to .dmg, and want it to go fast. Other
builds of libdmg-hfsplus will be quite slow and use just one core.

An example use case is backing up Mac partitions on a multi-boot system. You can make a quick
backup with dd or clonezilla, and it's easy to restore. But should you ever need to restore
to a partition of a different size, you'll need to convert to .dmg and use Disk Utility.

With other tools, that conversion can be slow. On a 12-core system, my 200 GB partition
(pre-compression) took over two hours to convert. But this build of libdmg took just 10 minutes!

## Installation

* Install cmake, and headers for zlib and lzfse.
   * Eg: `brew install cmake lzfse`
   * Eg: `apt install cmake zlib-dev liblzfse-dev`
* Configure with cmake: `cmake . -DCMAKE_BUILD_TYPE=Release`
* Build: `make -j6`
* Copy `dmg/dmg` somewhere in your path

## Usage

* Convert an image to dmg: `dmg dmg input.img output.dmg`
* Or use `-` as a file argument to use standard input/output

Example, with a disk image compressed with [pixz](https://github.com/vasi/pixz):

`cat giant.img.pxz | pixz -d | dmg dmg - output.dmg`

## How

* Changed dmg compression to use a parallel algorithm
* Made CRC32 calculation parallel, too
* Allowed streaming input in most cases
