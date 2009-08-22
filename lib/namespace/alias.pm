use strict;
use warnings;

package namespace::alias;

use 5.008008;
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

__END__

=head1 NAME

namespace::alias - Foo

=head1 SYNOPSIS

use namespace::alias 'My::Company::Namespace::Customer';

my $cust = Customer->new;            # My::Company::Namespace::Customer->new
my $pref = Customer::Preferred->new; # My::Company::Namespace::Customer::Preferred->new

=head1 SEE ALSO

=over 4

=item L<aliased>

=back

=head1 AUTHOR

Florian Ragwitz E<lt>rafl@debian.orgE<gt>

With contributions from:

=over 4

=item Robert 'phaylon' Sedlacek E<lt>rs@474.atE<gt>

=item Steffen Schwigon E<lt>ss5@renormalist.netE<gt>

=back

=head1 COPYRIGHT AND LICENSE

Copyright (c) 2009  Florian Ragwitz

Licensed under the same terms as perl itself.

=cut
