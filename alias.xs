#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#include "ppport.h"

#include "hook_op_check.h"

#include "stolen_chunk_of_op.h"

#define MG_UNSTRICT ((U16) (0xaffe))
#define enabled(u) S_enabled (aTHX_ u)

typedef struct user_data_St {
    char *file;
    SV *cb;
} user_data_t;

STATIC void (*real_peep) (pTHX_ OP *);

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

STATIC void
tag (OP *op)
{
    SV *sv;
    MAGIC *mg;

    assert (op->op_type == OP_CONST);

    sv = cSVOPx (op)->op_sv;
    mg = sv_magicext (sv, NULL, PERL_MAGIC_ext, NULL, NULL, 0);
    mg->mg_private = MG_UNSTRICT;
}

STATIC int
tagged (OP *op)
{
    SV *sv;
    MAGIC *mg;

    assert (op->op_type == OP_CONST);

    sv = cSVOPx (op)->op_sv;
    if (SvTYPE (sv) < SVt_PVMG) {
        return 0;
    }

    for (mg = SvMAGIC (sv); mg; mg = mg->mg_moremagic) {
        switch (mg->mg_type) {
            case PERL_MAGIC_ext:
                if (mg->mg_private == MG_UNSTRICT) {
                    return 1;
                }
                break;
            default:
                break;
        }
    }

    return 0;
}

STATIC int
S_enabled (pTHX_ user_data_t *ud)
{
    char *file = CopFILE (PL_curcop);

    if (file && strEQ (file, ud->file)) {
        return 1;
    }

    return 0;
}

STATIC OP *
check_alias (pTHX_ OP *op, void *user_data)
{
    user_data_t *ud = (user_data_t *)user_data;
    SV *name = cSVOPx (op)->op_sv;
    SV *replacement;

    if (!enabled (ud)) {
        return op;
    }

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

    /*
     * We explicitly don't handle the case of
     *
     *   MyAlias
     *   => 42
     *
     * here. We still call the alias expansion callback for that, but for some
     * obscure reason, perl won't pick up the replaced sv, so we don't need to
     * bother with scanning ahead in the linestr.
     */
    if (strnEQ (PL_parser->bufptr, SvPV_nolen (name), SvCUR (name))) {
        char *s = PL_parser->bufptr;
        s += SvCUR (name);
        while (s < PL_parser->bufend && isSPACE(*s)) {
            s++;
        }

        if ((PL_parser->bufend - s) >= 2 && strnEQ(s, "=>", 2)) {
            return op;
        }
    }

    replacement = invoke_callback (aTHX_ ud->cb, name);
    if (!SvTRUE (replacement)) {
        SvREFCNT_dec (replacement);
        return op;
    }

    SvREFCNT_dec (name);
    cSVOPx (op)->op_sv = replacement;

    tag (op);

    return op;
}

void
peep_unstrict (pTHX_ OP *first_op)
{
    OP *op;

    if (!first_op || first_op->op_opt) {
        return;
    }

    for (op = first_op; op; op = op->op_next) {
        switch (op->op_type) {
            case OP_CONST:
                if (tagged (op)) {
                    op->op_private &= ~OPpCONST_STRICT;
                }
                break;
            default:
                break;
        }
    }

    real_peep (aTHX_ first_op);
}

MODULE = namespace::alias  PACKAGE = namespace::alias

PROTOTYPES: DISABLE

hook_op_check_id
setup (class, file, cb)
        char *file
        SV *cb
    PREINIT:
        user_data_t *ud;
    INIT:
        if (!SvROK (cb) || SvTYPE (SvRV (cb)) != SVt_PVCV) {
            croak ("callback is not a code reference");
        }

        Newx (ud, 1, user_data_t);
        ud->file = strdup (file);
        ud->cb = newSVsv (cb);
    CODE:
        real_peep = namespace_alias_peep;
        PL_peepp = peep_unstrict;
        RETVAL = hook_op_check (OP_CONST, check_alias, ud);
    OUTPUT:
        RETVAL

void
teardown (class, hook)
        hook_op_check_id hook
    PREINIT:
        user_data_t *ud;
    CODE:
        ud = (user_data_t *)hook_op_check_remove (OP_CONST, hook);
        SvREFCNT_dec (ud->cb);
        free (ud->file);
        Safefree (ud);
