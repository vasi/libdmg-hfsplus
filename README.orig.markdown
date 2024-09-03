README
======

This project was first conceived to manipulate Apple's software restore
packages (IPSWs) and hence much of it is geared specifically toward that
format. Useful tools to read and manipulate the internal data structures of
those files have been created to that end, and with minor changes, more
generality can be achieved in the general utility. An inexhaustive list of
such changes would be selectively enabling folder counts in HFS+, switching
between case sensitivity and non-sensitivity, and more fine-grained control
over the layout of created dmgs.

**THE CODE HEREIN SHOULD BE CONSIDERED HIGHLY EXPERIMENTAL**

Extensive testing have not been done, but comparatively simple tasks like
adding and removing files from a mostly contiguous filesystem are well
proven.

Please note that these tools and routines are currently only suitable to be
accessed by other programs that know what they're doing. I.e., doing
something you "shouldn't" be able to do, like removing non-existent files is
probably not a very good idea.

LICENSE
-------

This work is released under the terms of the GNU General Public License,
version 3. The full text of the license can be found in the LICENSE file.

DEPENDENCIES
------------

The HFS portion will work on any platform that supports GNU C and POSIX
conventions. The dmg portion has dependencies on zlib and optionally on
libcrypto from openssl. If libcrypto is not available, then all FileVault
related actions will fail, but everything else should still work. To
deliberately disable FileVault support, use `CFLAGS=-UHAVE_CRYPT`.

USING
-----

The targets of the current repository are three command-line utilities that
demonstrate the usage of the library functions. The dmg portion of the code
has dependencies on the HFS+ portion of the code. The "hdutil" section
contains a version of the HFS+ utility that supports directly reading from
dmgs. It is separate from the HFS+ utility in order that the hfs directory
does not have dependencies on the dmg directory.

To build everything:

    cmake .
    make

After running cmake, individual projects can also be built like:

    make -C hfs
    make -C dmg
    make -C hdutil
