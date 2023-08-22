Make sure we have a fresh build:

  $ export BUILDDIR=$TESTDIR/../build
  $ cd $BUILDDIR
  $ make 2> /dev/null >/dev/null
  $ cd $CRAMTMP

Prepare content and the attribution code:
  $ mkdir stagedir
  $ echo "content-a" >> stagedir/a
  $ sentinel="__MOZILLA__attribution-code"
  $ attribution_code="short"

Create the filesystem with the sentinel value in the attribute:

  $ mkdir output
  $ cp $TESTDIR/empty.hfs output/short.hfs
  $ $BUILDDIR/hfs/hfsplus output/short.hfs add stagedir/a a
  $ $BUILDDIR/hfs/hfsplus output/short.hfs setattr a attr-key "$sentinel"

Build a DMG:

  $ $BUILDDIR/dmg/dmg build output/short.hfs output/short.dmg "$sentinel" >/dev/null

Now attribute:

  $ $BUILDDIR/dmg/dmg attribute output/short.dmg output/short-attributed.dmg "__MOZILLA__" "$attribution_code" >/dev/null

Extract the HFS from the attributed DMG, and check to see if the code is correct:

  $ $BUILDDIR/dmg/dmg extract output/short-attributed.dmg output/short-attributed.hfs >/dev/null
  $ $BUILDDIR/hfs/hfsplus output/short-attributed.hfs getattr a attr-key | tr -d '\0'
  short (no-eol)
