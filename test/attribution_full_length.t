Make sure we have a fresh build:

  $ export BUILDDIR=$TESTDIR/../build
  $ cd $BUILDDIR
  $ make 2> /dev/null >/dev/null
  $ cd $CRAMTMP

Prepare content and the attribution code (997 is 1024 minus the length of the sentinel):
  $ mkdir stagedir
  $ echo "content-a" >> stagedir/a
  $ sentinel="__MOZILLA__attribution-code"
  $ code=$(printf "${sentinel}%*s" "997" | tr ' ' '\011')
  $ attributed_code=$(printf "${sentinel}%*s" "997" | tr ' ' '~')

Make a small HFS filesystem with a full length (1024 byte) attribution value:

  $ mkdir output
  $ cp $TESTDIR/empty.hfs output/full-length.hfs
  $ $BUILDDIR/hfs/hfsplus output/full-length.hfs add stagedir/a a
  $ $BUILDDIR/hfs/hfsplus output/full-length.hfs setattr a attr-key "$code"

Echo it back, make sure it is the right length in the HFS filesystem:

  $ $BUILDDIR/hfs/hfsplus output/full-length.hfs getattr a attr-key | wc -c
  \s*1024 (re)

Build a DMG:

  $ $BUILDDIR/dmg/dmg build output/full-length.hfs output/full-length.dmg "$sentinel" >/dev/null

Now attribute, using printable characters for the full length:

  $ $BUILDDIR/dmg/dmg attribute output/full-length.dmg output/full-length-attributed.dmg "$sentinel" "$attributed_code" >/dev/null

Extract the HFS from the attributed DMG, and check to see if the code is correct:

  $ $BUILDDIR/dmg/dmg extract output/full-length-attributed.dmg output/full-length-attributed.hfs >/dev/null
  $ $BUILDDIR/hfs/hfsplus output/full-length-attributed.hfs getattr a attr-key | grep -c "$attributed_code"
  1
