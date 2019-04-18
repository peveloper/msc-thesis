#!/usr/bin/perl -w
# only print connections incoming to 152.2, 152.4, 152.19, 152.23, 204.84, and
# 204.85 subnets.

die "specify the filename of ADU text sorted by connection ID\n"
  if (scalar(@ARGV) != 1);

my $infile = shift;

open(INPUT, "perl -pe 's/^SYN/\\nSYN/' $infile |")
  or die "couldn't open input";

# The input record separator
$/ = "\n\n";

sub main()
{
  my ($outgoing, $passed, $noadu, $conc, $toobig) =
     (0,0,0,0,0,0,0);
  while (<INPUT>) {
    my $line1 = (split(/\n/,$_))[0];
    next if $line1 !~ m/^SYN/;
    if ($line1 !~ m/</) {
      $outgoing++;
      next;
    }

    if ($_ !~ m/ADU:/) {
      $noadu++;
      next;
    }
    if ($_ =~ m/CONC/) {
      $conc++;
      next;
    }
    if (toobig(\$_)) {
      $toobig++;
      next;
    }

    $passed++;
    print $_;
  }

  print STDERR "outgoing: $outgoing\n";
  print STDERR "noadu: $noadu\n";
  print STDERR "concurrent: $conc\n";
  print STDERR "toobig: $toobig\n";
  print STDERR "passed: $passed\n";
}

sub toobig($)
{
  my @lens = ($_ =~ m/^ADU: .* (\d+) SEQ.*$/m);
  my @lines = (split(/\n/,$_));
  my $first = (split(/ /, $lines[0]))[1];
  my $last  = (split(/ /, $lines[-1]))[1];
  my $sum;
  foreach $i (@lens) {
    $sum += $i if $i ne "X.X";
  }
  if (!$last || !$first || $last eq "X.X" || $first eq "X.X") {
    return 0;
  }
  if ($sum / ($last - $first) > 2e6 && ($sum > 1e6)) {
    # sustained average sending rate of two megabytes per second is too fast
    return 1;
  }
  if ($sum < 0) {
    # we had a large ADU and an overflow...we'll assume that's spurious
    return 1;
  }
  return 0;
}

main();
