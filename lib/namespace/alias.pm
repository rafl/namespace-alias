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

    my $hook = $class->setup(sub {
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
