#!/usr/bin/perl
# Replaces text in a file that may have CRLF line endings, from "old" / "new" blocks in a patch file:
#
#     perl dev/tools/patch_text.pl <file to change> <patch file>
#
# The patch file is a sequence of blocks (the markers are lines of their own):
#
#     <<<<<<< OLD
#     text to find (as it is, line ends in LF)
#     =======
#     text to put there
#     >>>>>>> NEW
#
# The file's line endings (CRLF or LF) are kept.  Nothing is written unless every OLD text is found exactly once.
use strict;
use warnings;
use utf8;

my ($target, $patchfile) = @ARGV;
die "usage: patch_text.pl <file> <patch file>\n" unless defined $patchfile;

sub slurp {
    my ($path) = @_;
    open(my $h, '<:raw', $path) or die "cannot read $path: $!\n";
    my $s = do { local $/; <$h> };
    close $h;
    utf8::decode($s);
    return $s;
}

my $text = slurp($target);
my $crlf = ($text =~ s/\r\n/\n/g) ? 1 : 0;
my $patch = slurp($patchfile);
$patch =~ s/\r\n/\n/g;

my $count = 0;
while ($patch =~ /<<<<<<< OLD\n(.*?)\n=======\n(.*?)\n>>>>>>> NEW\n/gs) {
    my ($old, $new) = ($1, $2);
    my $first = index($text, $old);
    die "not found in $target:\n$old\n" if $first < 0;
    die "found more than once in $target:\n$old\n" if index($text, $old, $first + 1) >= 0;
    substr($text, $first, length($old)) = $new;
    $count++;
}
die "no blocks in $patchfile\n" unless $count;

$text =~ s/\n/\r\n/g if $crlf;
utf8::encode($text);
open(my $out, '>:raw', $target) or die "cannot write $target: $!\n";
print $out $text;
close $out;
print "$target: $count change(s), ", ($crlf ? 'CRLF' : 'LF'), "\n";
