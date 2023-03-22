Make sure we have a fresh build:

  $ export BUILDDIR=$TESTDIR/../build
  $ cd $BUILDDIR
  $ make &> /dev/null
  $ cd $CRAMTMP

Prepare content:

  $ mkdir stagedir
  $ echo "content-a" >> stagedir/a
  $ echo "content-b" >> stagedir/b

Create reference DMGs using macOS:

  $ mkdir reference
  $ hdiutil create -megabytes 5 -fs HFS+ -volname myDisk -srcfolder stagedir reference/hdiutil.hfs
  created: */reference/hdiutil.hfs.dmg (glob)
  $ xattr -w 'attr-key-a' '__MOZILLA__attr-value-a' stagedir/a
  $ hdiutil create -megabytes 5 -fs HFS+ -volname myDisk -srcfolder stagedir reference/hdiutila.hfs
  created: */reference/hdiutila.hfs.dmg (glob)
  $ xattr -w 'attr-key-b' '__MOZILLA__attr-value-b' stagedir/b
  $ hdiutil create -megabytes 5 -fs HFS+ -volname myDisk -srcfolder stagedir reference/hdiutilab.hfs
  created: */reference/hdiutilab.hfs.dmg (glob)
  $ xattr -c stagedir/a
  $ hdiutil create -megabytes 5 -fs HFS+ -volname myDisk -srcfolder stagedir reference/hdiutilb.hfs

Extract reference HFSs:

  $ $BUILDDIR/dmg/dmg extract reference/hdiutil.hfs.dmg reference/hdiutil.hfs > /dev/null
  $ $BUILDDIR/dmg/dmg extract reference/hdiutila.hfs.dmg reference/hdiutila.hfs > /dev/null
  $ $BUILDDIR/dmg/dmg extract reference/hdiutilb.hfs.dmg reference/hdiutilb.hfs > /dev/null
  $ $BUILDDIR/dmg/dmg extract reference/hdiutilab.hfs.dmg reference/hdiutilab.hfs > /dev/null

Generate comparison HFSs:

  $ mkdir output
  $ cp $TESTDIR/empty.hfs output/stageda.hfs
  $ $BUILDDIR/hfs/hfsplus output/stageda.hfs addall stagedir
  file: /a
  Setting permissions to 100644 for /a
  file: /b
  Setting permissions to 100644 for /b
  $ $BUILDDIR/hfs/hfsplus output/stageda.hfs setattr a 'attr-key-a' '__MOZILLA__attr-value-a__MOZILLA__'
  $ cp $TESTDIR/empty.hfs output/stagedap.hfs
  $ $BUILDDIR/hfs/hfsplus output/stagedap.hfs addall stagedir
  file: /a
  Setting permissions to 100644 for /a
  file: /b
  Setting permissions to 100644 for /b
  $ $BUILDDIR/hfs/hfsplus output/stagedap.hfs setattr a 'attr-key-a' '__MOZILLA__attr-value-p__MOZILLA__'
  $ cp $TESTDIR/empty.hfs output/stagedab.hfs
  $ $BUILDDIR/hfs/hfsplus output/stagedab.hfs addall stagedir
  file: /a
  Setting permissions to 100644 for /a
  file: /b
  Setting permissions to 100644 for /b
  $ $BUILDDIR/hfs/hfsplus output/stagedab.hfs setattr a 'attr-key-a' '__MOZILLA__attr-value-a__MOZILLA__'
  $ $BUILDDIR/hfs/hfsplus output/stagedab.hfs setattr b 'attr-key-b' '__MOZILLA__attr-value-b__MOZILLA__'
  $ cp $TESTDIR/empty.hfs output/stagedb.hfs
  $ $BUILDDIR/hfs/hfsplus output/stagedb.hfs addall stagedir
  file: /a
  Setting permissions to 100644 for /a
  file: /b
  Setting permissions to 100644 for /b
  $ $BUILDDIR/hfs/hfsplus output/stagedb.hfs setattr b 'attr-key-b' '__MOZILLA__attr-value-b__MOZILLA__'
