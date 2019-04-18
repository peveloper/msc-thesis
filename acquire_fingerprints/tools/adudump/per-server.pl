#!/usr/bin/perl -w
# per-server.pl - get think-time distributions per server
# output columns:
# ip.port N min median mean max

if (scalar(@ARGV) != 1) {
  die "usage: $0 dirname/thinktimes\n";
}

sub main
{
  my $lastsock;
  my @ttimes;
  open IN, "sort -s -k3,3 $ARGV[0] |"
    or die "couldn't pipe-open $ARGV[0]: $!\n";

  while (<IN>) {
    chomp;
    my @fields = (split);
    my $cursock = $fields[2];

    if ($lastsock && $lastsock ne $cursock) {
      distrib($lastsock, \@ttimes);
      @ttimes = ();
      $lastsock = $cursock;
    } elsif (!$lastsock) {
      $lastsock = $cursock;
    }
    push(@ttimes, $fields[7]);
  }
  distrib($lastsock, \@ttimes);

  close(IN);
}

sub distrib($$)
{
  my $sock = shift;
  my $aref = shift;
  my $n = scalar(@$aref);
  my $min = $aref->[0];
  my $med = median($aref, $n);
  my $max = $aref->[-1];
  #my $q50 = quantile(50, $aref, $n);
  #my $q75 = quantile(75, $aref, $n);
  #my $q90 = quantile(90, $aref, $n);
  #my $q95 = quantile(95, $aref, $n);
  #my $q99 = quantile(99, $aref, $n);
  my $sum;
  for ($i=0; $i<$n; $i++) {
    $sum += $aref->[$i];
  }
  my $avg = $sum / $n;
  #printf("$sock $n $min $max $avg $q50 $q75 $q90 $q95 $q99\n");
  printf("$sock %d %.6f %.6f %.6f %.6f\n", $n, $min, $med, $avg, $max);
}

# med(1,2) = 1.5 = a[.5]
# med(1,2,3) = 2 = a[1]
# med(1,2,3,4) = 2.5 = a[1.5]
sub median($$)
{
  my $aref = shift;
  my $n = shift;
  if ($n == 1) {
    return $aref->[0];
  } elsif ($n % 2 == 0) {
    return ($aref->[int($n/2)] + $aref->[int($n/2)-1]) / 2;
  } else {
    return $aref->[int(($n-1)/2)];
  }
}

sub quantile($$$)
{
  my $q = shift;
  my $aref = shift;
  my $n = shift;
  my $idx = int($n * $q / 100) - 1;
  if ($n % 2 == 0) {
    return ($aref->[$idx] + $aref->[$idx-1]) / 2;
  } else {
    return $aref->[$idx];
  }
}

&main;

