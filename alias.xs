#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "ppport.h"

#include "hook_op_check.h"

STATIC SV *
invoke_callback (pTHX_ SV *cb, SV *name)
{
    dSP;
    int count;
    SV *ret;

    ENTER;
    SAVETMPS;

    PUSHMARK (SP);
    XPUSHs (name);
    PUTBACK;

    count = call_sv (cb, G_SCALAR);

    SPAGAIN;

    if (count != 1) {
        croak ("namespace::alias callback didn't return a single argument");
    }

    ret = SvREFCNT_inc (POPs);

    PUTBACK;
    FREETMPS;
    LEAVE;

    return ret;
}

STATIC OP *
check_alias (pTHX_ OP *op, void *cb)
{
    SV *name = cSVOPx (op)->op_sv;
    SV *replacement;

    if (!SvPOK (name)) {
        return op;
    }

    if (PL_parser->lex_stuff) {
        return op;
    }

    switch (PL_parser->lex_inwhat) {
        case OP_QR:
        case OP_MATCH:
        case OP_SUBST:
        case OP_TRANS:
        case OP_BACKTICK:
        case OP_STRINGIFY:
            return op;
            break;
        default:
            break;
    }

    replacement = invoke_callback (aTHX_ cb, name);

    SvREFCNT_dec (name);
    cSVOPx (op)->op_sv = replacement;

    return op;
}

MODULE = namespace::alias  PACKAGE = namespace::alias

PROTOTYPES: DISABLE

hook_op_check_id
setup (class, cb)
        SV *cb
    CODE:
        RETVAL = hook_op_check (OP_CONST, check_alias, newSVsv (cb));
    OUTPUT:
        RETVAL

void
teardown (class, hook)
        hook_op_check_id hook
    CODE:
        SvREFCNT_dec (hook_op_check_remove (OP_CONST, hook));
