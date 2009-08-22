use strict;
use warnings;

package namespace::alias;

use XSLoader;
use B::Hooks::OP::Check;
use B::Hooks::EndOfScope;

our $VERSION = '0.01';

XSLoader::load(__PACKAGE__, $VERSION);

sub import {
    my ($class, $package, $alias) = @_;

    ($alias) = $package =~ /(?:::|')(\w+)$/
        unless defined $alias;

    my $file = (caller)[1];

    my $hook = $class->setup($file => sub {
        my ($str) = @_;

        if ($str =~ s/^$alias\b/$package/) {
            return $str;
        }

        return;
    });

    on_scope_end {
        $class->teardown($hook);
    };
}

1;
