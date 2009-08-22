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
    is SomeModule::call_alias_paren, 'myalias';

    is MyAlias::foo, 'Foo::Bar::Baz::foo';
}

is MyAlias::foo, 'myalias';

done_testing;
