#!/usr/bin/perl -w
# locals.pl - only print local think times

# This code only prints a "<" ADU record if it:
# a) has a think-time, and
# b) is followed by a ">" ADU
# ...unless it is the last ADU in the connection

die "Specify the input file (dirname/connections) as the only argument\n"
  if (scalar(@ARGV) != 1);

$connfile = shift;

open IN, "egrep '^ADU|^\$' $connfile |"
  or die "couldn't pipe-open input: $!\n";

while (<IN>) {
  chomp;
  if (!$_) {
    $last_dir = 0;
    $last_time = 0;
  } else {
    if (/>/) {
      print "$last_line\n" if $last_time && $last_dir == 1;
      $last_dir = 2;
    } else {
      $last_dir = 1;
    }
    if (/SEQ (\d*\.?\d+)$/) {
      $last_time = $1;
    } else {
      $last_time = 0;
    }
  }
  $last_line = $_;
}

