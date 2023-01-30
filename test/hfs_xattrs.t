Make sure we have a fresh build:

  $ export BUILDDIR=$TESTDIR/../build
  $ cd $BUILDDIR
  $ make 2>&1 > /dev/null
  $ cd $CRAMTMP

Prepare content:

  $ mkdir stagedir
  $ echo "content-a" >> stagedir/a
  $ echo "content-b" >> stagedir/b

Create reference DMGs using macOS:

  $ mkdir reference
  $ hdiutil create -megabytes 5 -fs HFS+ -volname myDisk reference/hdiutil.hfs
  created: */reference/hdiutil.hfs.dmg (glob)
  $ xattr -w 'attr-key-a' 'attr-value-a' stagedir/a
  $ hdiutil create -megabytes 5 -fs HFS+ -volname myDisk reference/hdiutila.hfs
  created: */reference/hdiutila.hfs.dmg (glob)
  $ xattr -w 'attr-key-b' 'attr-value-b' stagedir/b
  $ hdiutil create -megabytes 5 -fs HFS+ -volname myDisk reference/hdiutilab.hfs
  created: */reference/hdiutilab.hfs.dmg (glob)

Extract reference HFSs:

  $ $BUILDDIR/dmg/dmg extract reference/hdiutil.hfs.dmg reference/hdiutil.hfs
  $ $BUILDDIR/dmg/dmg extract reference/hdiutila.hfs.dmg reference/hdiutila.hfs
  $ $BUILDDIR/dmg/dmg extract reference/hdiutilab.hfs.dmg reference/hdiutilab.hfs
