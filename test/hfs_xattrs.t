Make sure we have a fresh build:

  $ export BUILDDIR=$TESTDIR/../build
  $ cd $BUILDDIR
  $ make 2> /dev/null >/dev/null
  $ cd $CRAMTMP

Prepare content:

  $ mkdir stagedir
  $ echo "content-a" >> stagedir/a
  $ echo "content-b" >> stagedir/b

Extract reference HFSs and attributes. We parse the debugattrs a bit because the attribute numbers from hdiutil and hfsplus may not match:

  $ mkdir hfs_xattrs_reference
  $ $BUILDDIR/dmg/dmg extract $TESTDIR/hfs_xattrs_reference/hdiutila.hfs.dmg $CRAMTMP/hfs_xattrs_reference/hdiutila.hfs > /dev/null
  $ $BUILDDIR/dmg/dmg extract $TESTDIR/hfs_xattrs_reference/hdiutilb.hfs.dmg $CRAMTMP/hfs_xattrs_reference/hdiutilb.hfs > /dev/null
  $ $BUILDDIR/dmg/dmg extract $TESTDIR/hfs_xattrs_reference/hdiutilab.hfs.dmg $CRAMTMP/hfs_xattrs_reference/hdiutilab.hfs > /dev/null
  $ $BUILDDIR/hfs/hfsplus hfs_xattrs_reference/hdiutila.hfs debugattrs verbose | grep attribute | sed -e 's/[0-9].*:/:/'  > hfs_xattrs_reference/hdiutila.attrs
  $ $BUILDDIR/hfs/hfsplus hfs_xattrs_reference/hdiutilb.hfs debugattrs verbose | grep attribute | sed -e 's/[0-9].*:/:/' > hfs_xattrs_reference/hdiutilb.attrs
  $ $BUILDDIR/hfs/hfsplus hfs_xattrs_reference/hdiutilab.hfs debugattrs verbose | grep attribute | sed -e 's/[0-9].*:/:/' > hfs_xattrs_reference/hdiutilab.attrs

Generate comparison HFSs:

  $ mkdir output
  $ cp $TESTDIR/empty.hfs output/stageda.hfs
  $ $BUILDDIR/hfs/hfsplus output/stageda.hfs add stagedir/a a
  $ $BUILDDIR/hfs/hfsplus output/stageda.hfs add stagedir/b b
  $ $BUILDDIR/hfs/hfsplus output/stageda.hfs setattr a 'attr-key-a' '__MOZILLA__attr-value-a__MOZILLA__'
  $ $BUILDDIR/hfs/hfsplus output/stageda.hfs debugattrs verbose | grep attribute | sed -e 's/[0-9].*:/:/' > output/stageda.attrs
  $ cp $TESTDIR/empty.hfs output/stagedab.hfs
  $ $BUILDDIR/hfs/hfsplus output/stagedab.hfs add stagedir/a a
  $ $BUILDDIR/hfs/hfsplus output/stagedab.hfs add stagedir/b b
  $ $BUILDDIR/hfs/hfsplus output/stagedab.hfs setattr a 'attr-key-a' '__MOZILLA__attr-value-a__MOZILLA__'
  $ $BUILDDIR/hfs/hfsplus output/stagedab.hfs setattr b 'attr-key-b' '__MOZILLA__attr-value-b__MOZILLA__'
  $ $BUILDDIR/hfs/hfsplus output/stagedab.hfs debugattrs verbose | grep attribute | sed -e 's/[0-9].*:/:/' > output/stagedab.attrs
  $ cp $TESTDIR/empty.hfs output/stagedb.hfs
  $ $BUILDDIR/hfs/hfsplus output/stagedb.hfs add stagedir/a a
  $ $BUILDDIR/hfs/hfsplus output/stagedb.hfs add stagedir/b b
  $ $BUILDDIR/hfs/hfsplus output/stagedb.hfs setattr b 'attr-key-b' '__MOZILLA__attr-value-b__MOZILLA__'
  $ $BUILDDIR/hfs/hfsplus output/stagedb.hfs debugattrs verbose | grep attribute | sed -e 's/[0-9].*:/:/' > output/stagedb.attrs

Compare attributes in the reference images and generated images:

  $ diff --unified=3 hfs_xattrs_reference/hdiutila.attrs output/stageda.attrs
  $ diff --unified=3 hfs_xattrs_reference/hdiutilb.attrs output/stagedb.attrs
  $ diff --unified=3 hfs_xattrs_reference/hdiutilab.attrs output/stagedab.attrs
