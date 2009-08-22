#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"

#define NEED_PL_parser
#include "ppport.h"

#if (PERL_BCDVERSION >= 0x5009005)
#define PL_lex_inwhat (PL_parser->lex_inwhat)
#endif

#include "hook_op_check.h"

#include "stolen_chunk_of_op.h"

#define MG_UNSTRICT ((U16) (0xaffe))
#define MG_UNCALL   ((U16) (0xafff))

#define enabled(u) S_enabled (aTHX_ u)

typedef struct user_data_St {
    char *file;
    SV *cb;
    hook_op_check_id gv;
    hook_op_check_id entersub;
} user_data_t;

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
tag (OP *op, int type, void *data)
{
    SV *sv;
    MAGIC *mg;

    assert (op->op_type == OP_CONST || op->op_type == OP_GV);

    if (op->op_type == OP_CONST) {
        sv = cSVOPx (op)->op_sv;
    }
    else {
        sv = (SV *)cGVOPx_gv (op);
    }

    mg = sv_magicext (sv, NULL, PERL_MAGIC_ext, NULL, NULL, 0);
    mg->mg_private = type;
    mg->mg_ptr = data;
}

STATIC int
tagged (OP *op, int type, void **data)
{
    SV *sv;
    MAGIC *mg;

    assert (op->op_type == OP_CONST || op->op_type == OP_GV);

    if (op->op_type == OP_CONST) {
        sv = cSVOPx (op)->op_sv;
    }
    else {
        sv = (SV *)cGVOPx_gv (op);
    }

    if (SvTYPE (sv) < SVt_PVMG) {
        return 0;
    }

    for (mg = SvMAGIC (sv); mg; mg = mg->mg_moremagic) {
        switch (mg->mg_type) {
            case PERL_MAGIC_ext:
                if (mg->mg_private == type) {
                    if (data) {
                        *data = mg->mg_ptr;
                    }
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

    if (PL_lex_stuff) {
        return op;
    }

    switch (PL_lex_inwhat) {
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
    if (strnEQ (PL_bufptr, SvPV_nolen (name), SvCUR (name))) {
        char *s = PL_bufptr;
        s += SvCUR (name);
        while (s < PL_bufend && isSPACE(*s)) {
            s++;
        }

        if ((PL_bufend - s) >= 2 && strnEQ(s, "=>", 2)) {
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

    tag (op, MG_UNSTRICT, NULL);

    return op;
}

void
peep_unstrict (pTHX_ OP *first_op)
{
    OP *op;

    if (!first_op || NCA_OP_OPT(first_op)) {
        return;
    }

    for (op = first_op; op; op = op->op_next) {
        switch (op->op_type) {
            case OP_CONST:
                if (tagged (op, MG_UNSTRICT, NULL)) {
                    op->op_private &= ~OPpCONST_STRICT;
                }
                break;
            default:
                break;
        }
    }

    namespace_alias_peep (aTHX_ first_op);
}

STATIC OP *
check_gv (pTHX_ OP *op, void *user_data)
{
    user_data_t *ud = (user_data_t *)user_data;
    GV *gv;
    SV *name, *replacement;

    if (!enabled (ud)) {
        return op;
    }

    gv = cGVOPx_gv (op);
    if (!gv || !GvSTASH (gv)) {
        return op;
    }

    name = newSVpv (HvNAME (GvSTASH (gv)), 0);
    sv_catpvs (name, "::");
    sv_catpv (name, GvNAME (gv));

    replacement = invoke_callback (aTHX_ ud->cb, name);
    if (!SvTRUE (replacement)) {
        SvREFCNT_dec (replacement);
        return op;
    }

    tag (op, MG_UNCALL, replacement);

    return op;
}

STATIC OP *
check_entersub (pTHX_ OP *op, void *user_data)
{
    user_data_t *ud = (user_data_t *)user_data;
    OP *prev = ((cUNOPx (op)->op_first->op_sibling)
        ? cUNOPx (op) : ((UNOP *)cUNOPx (op)->op_first))->op_first;
    OP *op2 = prev->op_sibling;
    OP *cvop, *gvop;
    SV *replacement;

    if (!enabled (ud)) {
        return op;
    }

    for (cvop = op2; cvop->op_sibling; cvop = cvop->op_sibling) ;

    if (cvop->op_type != OP_NULL) {
        return op;
    }

    gvop = cUNOPx (cvop)->op_first;
    if (gvop->op_type != OP_GV) {
        return op;
    }

    if (!tagged (gvop, MG_UNCALL, (void **)&replacement)) {
        return op;
    }

    op_free (op);

    return newSVOP (OP_CONST, 0, replacement);
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
        PL_peepp = peep_unstrict;
        ud->entersub = hook_op_check (OP_ENTERSUB, check_entersub, ud);
        ud->gv = hook_op_check (OP_GV, check_gv, ud);
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
        (void)hook_op_check_remove (OP_GV, ud->gv);
        (void)hook_op_check_remove (OP_ENTERSUB, ud->entersub);
        SvREFCNT_dec (ud->cb);
        free (ud->file);
        Safefree (ud);
