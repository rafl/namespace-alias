use strict;
use warnings;
use Test::More;

use FindBin;
use lib "$FindBin::Bin/lib";

{
    package Foo::Bar::Baz;
    sub foo { 'baz' }

    package MyAlias;
    sub foo { 'myalias' }
}

is MyAlias::foo, 'myalias';

{
    use namespace::alias 'Foo::Bar::Baz', 'MyAlias';

    use SomeModule;

    is MyAlias::foo(), 'baz';

    is SomeModule::call_alias, 'myalias';

    TODO: {
        local $TODO = 'unimplemented';
        is MyAlias::foo, 'baz';
    }

    TODO: {
        local $TODO = 'we are still leaking into require';
        is SomeModule::call_alias_paren, 'myalias';
    }
}

is MyAlias::foo, 'myalias';

done_testing;
