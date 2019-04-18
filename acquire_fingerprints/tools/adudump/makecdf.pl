#!/usr/bin/perl
# makecdf.pl - generate a file ready to be plugged in to gnuplot as a CDF
#
# input format (e.g. from `uniq -c`):
# count X
#
# output format:
# X quantile

# hopefully this won't suck too bad...CDFs are AFAIK inherently two-pass
# update 2007-04-25 - apparently it does suck too bad.  Need to use a temporary
# file in the current directory.

use strict;

open TMP, "> ./tmp.$$"
  or die "couldn't open ./tmp.$$ for writing: $!";
my (@a, $sum, $fmt, $cum);

while (<>) {
  chomp;
  @a = (split);
  $sum += $a[0];
  print TMP "$a[0] $a[1]\n";
}

close(TMP)
  or die "couldn't close ./tmp.$$ after writing: $!";

if ($a[1] =~ m/\./) {
  $fmt = '%.6lf';
} elsif (length($a[1]) > 9) {
  $fmt = '%lu';
} else {
  $fmt = '%d';
}

open TMP, "< ./tmp.$$"
  or die "couldn't open ./tmp.$$ for reading: $!";

while (<TMP>) {
  chomp;
  @a = (split);
  $cum += $a[0];
  printf("$fmt %.12lf\n", $a[1], $cum/$sum);
}

close TMP
  or die "couldn't close ./tmp.$$ after reading: $!";

`rm ./tmp.$$`;

