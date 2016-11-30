#!/bin/bash
#
# Renames the skeleton files and changes all file references
# so the program should build.
#

FN="$1"
UP="${FN^^}"

echo -n "Renaming skeleton.c..."
sed s/skeleton/$FN/g skeleton.c > c.1.t
sed s/Skeleton/$FN/g c.1.t > c.2.t
sed s/SKELETON/$UP/g c.2.t > c.3.t
echo "done"

echo -n "Renaming skeleton.h..."
sed s/skeleton/$FN/g skeleton.h > h.1.t
sed s/Skeleton/$FN/g h.1.t > h.2.t
sed s/SKELETON/$UP/g h.2.t > h.3.t
echo "done"

echo -n "Fixing up Makefile..."
sed s/skeleton/$FN/g Makefile > Makefile.t
echo "done"


echo -n "Fixing up gitignore..."
sed s/skeleton/$FN/g .gitignore > gitignore.t
echo "done"

echo -n "Cleaning up..."
mv c.3.t $FN.c
mv h.3.t $FN.h
mv Makefile.t Makefile
mv gitignore.t .gitignore
rm *.t
git rm skeleton.[ch]
git add $FN.[ch]
echo "done"
