#!/bin/sh
set -e

OUTDIR="/breezy-gnome/out"
mkdir -p $OUTDIR

cp -r /usr/lib $OUTDIR
cp -r /usr/bin $OUTDIR
cp -r /usr/share $OUTDIR