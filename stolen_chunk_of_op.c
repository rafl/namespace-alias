#define PERL_EXT
#include "EXTERN.h"
#include "perl.h"

#define CALL_PEEP(o) CALL_FPTR(PL_peepp)(aTHX_ o)

#define PERL_ARGS_ASSERT_NO_BAREWORD_ALLOWED \
    assert(o)

#if !defined(PERL_IMPLICIT_CONTEXT)
#define no_bareword_allowed S_no_bareword_allowed
#else
#define no_bareword_allowed(a) S_no_bareword_allowed(aTHX_ a)
#endif

STATIC void
S_no_bareword_allowed(pTHX_ const OP *o)
{
    PERL_ARGS_ASSERT_NO_BAREWORD_ALLOWED;

    if (PL_madskills)
	return;		/* various ok barewords are hidden in extra OP_NULL */
    qerror(Perl_mess(aTHX_
		     "Bareword \"%"SVf"\" not allowed while \"strict subs\" in use",
		     SVfARG(cSVOPo_sv)));
}


/* A peephole optimizer.  We visit the ops in the order they're to execute.
 * See the comments at the top of this file for more details about when
 * peep() is called */

void
namespace_alias_peep(pTHX_ register OP *o)
{
    dVAR;
    register OP* oldop = NULL;

    if (!o || o->op_opt)
	return;
    ENTER;
    SAVEOP();
    SAVEVPTR(PL_curcop);
    for (; o; o = o->op_next) {
	if (o->op_opt)
	    break;
	/* By default, this op has now been optimised. A couple of cases below
	   clear this again.  */
	o->op_opt = 1;
	PL_op = o;
	switch (o->op_type) {
	case OP_SETSTATE:
	case OP_NEXTSTATE:
	case OP_DBSTATE:
	    PL_curcop = ((COP*)o);		/* for warnings */
	    break;

	case OP_CONST:
	    if (cSVOPo->op_private & OPpCONST_STRICT)
		no_bareword_allowed(o);
#ifdef USE_ITHREADS
	case OP_METHOD_NAMED:
	    /* Relocate sv to the pad for thread safety.
	     * Despite being a "constant", the SV is written to,
	     * for reference counts, sv_upgrade() etc. */
	    if (cSVOP->op_sv) {
		const PADOFFSET ix = pad_alloc(OP_CONST, SVs_PADTMP);
		if (o->op_type == OP_CONST && SvPADTMP(cSVOPo->op_sv)) {
		    /* If op_sv is already a PADTMP then it is being used by
		     * some pad, so make a copy. */
		    sv_setsv(PAD_SVl(ix),cSVOPo->op_sv);
		    SvREADONLY_on(PAD_SVl(ix));
		    SvREFCNT_dec(cSVOPo->op_sv);
		}
		else if (o->op_type == OP_CONST
			 && cSVOPo->op_sv == &PL_sv_undef) {
		    /* PL_sv_undef is hack - it's unsafe to store it in the
		       AV that is the pad, because av_fetch treats values of
		       PL_sv_undef as a "free" AV entry and will merrily
		       replace them with a new SV, causing pad_alloc to think
		       that this pad slot is free. (When, clearly, it is not)
		    */
		    SvOK_off(PAD_SVl(ix));
		    SvPADTMP_on(PAD_SVl(ix));
		    SvREADONLY_on(PAD_SVl(ix));
		}
		else {
		    SvREFCNT_dec(PAD_SVl(ix));
		    SvPADTMP_on(cSVOPo->op_sv);
		    PAD_SETSV(ix, cSVOPo->op_sv);
		    /* XXX I don't know how this isn't readonly already. */
		    SvREADONLY_on(PAD_SVl(ix));
		}
		cSVOPo->op_sv = NULL;
		o->op_targ = ix;
	    }
#endif
	    break;

	case OP_CONCAT:
	    if (o->op_next && o->op_next->op_type == OP_STRINGIFY) {
		if (o->op_next->op_private & OPpTARGET_MY) {
		    if (o->op_flags & OPf_STACKED) /* chained concats */
			break; /* ignore_optimization */
		    else {
			/* assert(PL_opargs[o->op_type] & OA_TARGLEX); */
			o->op_targ = o->op_next->op_targ;
			o->op_next->op_targ = 0;
			o->op_private |= OPpTARGET_MY;
		    }
		}
		op_null(o->op_next);
	    }
	    break;
	case OP_STUB:
	    if ((o->op_flags & OPf_WANT) != OPf_WANT_LIST) {
		break; /* Scalar stub must produce undef.  List stub is noop */
	    }
	    goto nothin;
	case OP_NULL:
	    if (o->op_targ == OP_NEXTSTATE
		|| o->op_targ == OP_DBSTATE
		|| o->op_targ == OP_SETSTATE)
	    {
		PL_curcop = ((COP*)o);
	    }
	    /* XXX: We avoid setting op_seq here to prevent later calls
	       to peep() from mistakenly concluding that optimisation
	       has already occurred. This doesn't fix the real problem,
	       though (See 20010220.007). AMS 20010719 */
	    /* op_seq functionality is now replaced by op_opt */
	    o->op_opt = 0;
	    /* FALL THROUGH */
	case OP_SCALAR:
	case OP_LINESEQ:
	case OP_SCOPE:
	nothin:
	    if (oldop && o->op_next) {
		oldop->op_next = o->op_next;
		o->op_opt = 0;
		continue;
	    }
	    break;

	case OP_PADAV:
	case OP_GV:
	    if (o->op_type == OP_PADAV || o->op_next->op_type == OP_RV2AV) {
		OP* const pop = (o->op_type == OP_PADAV) ?
			    o->op_next : o->op_next->op_next;
		IV i;
		if (pop && pop->op_type == OP_CONST &&
		    ((PL_op = pop->op_next)) &&
		    pop->op_next->op_type == OP_AELEM &&
		    !(pop->op_next->op_private &
		      (OPpLVAL_INTRO|OPpLVAL_DEFER|OPpDEREF|OPpMAYBE_LVSUB)) &&
		    (i = SvIV(((SVOP*)pop)->op_sv) - CopARYBASE_get(PL_curcop))
				<= 255 &&
		    i >= 0)
		{
		    GV *gv;
		    if (cSVOPx(pop)->op_private & OPpCONST_STRICT)
			no_bareword_allowed(pop);
		    if (o->op_type == OP_GV)
			op_null(o->op_next);
		    op_null(pop->op_next);
		    op_null(pop);
		    o->op_flags |= pop->op_next->op_flags & OPf_MOD;
		    o->op_next = pop->op_next->op_next;
		    o->op_ppaddr = PL_ppaddr[OP_AELEMFAST];
		    o->op_private = (U8)i;
		    if (o->op_type == OP_GV) {
			gv = cGVOPo_gv;
			GvAVn(gv);
		    }
		    else
			o->op_flags |= OPf_SPECIAL;
		    o->op_type = OP_AELEMFAST;
		}
		break;
	    }

	    if (o->op_next->op_type == OP_RV2SV) {
		if (!(o->op_next->op_private & OPpDEREF)) {
		    op_null(o->op_next);
		    o->op_private |= o->op_next->op_private & (OPpLVAL_INTRO
							       | OPpOUR_INTRO);
		    o->op_next = o->op_next->op_next;
		    o->op_type = OP_GVSV;
		    o->op_ppaddr = PL_ppaddr[OP_GVSV];
		}
	    }
	    else if ((o->op_private & OPpEARLY_CV) && ckWARN(WARN_PROTOTYPE)) {
		GV * const gv = cGVOPo_gv;
		if (SvTYPE(gv) == SVt_PVGV && GvCV(gv) && SvPVX_const(GvCV(gv))) {
		    /* XXX could check prototype here instead of just carping */
		    SV * const sv = sv_newmortal();
		    gv_efullname3(sv, gv, NULL);
		    Perl_warner(aTHX_ packWARN(WARN_PROTOTYPE),
				"%"SVf"() called too early to check prototype",
				SVfARG(sv));
		}
	    }
	    else if (o->op_next->op_type == OP_READLINE
		    && o->op_next->op_next->op_type == OP_CONCAT
		    && (o->op_next->op_next->op_flags & OPf_STACKED))
	    {
		/* Turn "$a .= <FH>" into an OP_RCATLINE. AMS 20010917 */
		o->op_type   = OP_RCATLINE;
		o->op_flags |= OPf_STACKED;
		o->op_ppaddr = PL_ppaddr[OP_RCATLINE];
		op_null(o->op_next->op_next);
		op_null(o->op_next);
	    }

	    break;

	case OP_MAPWHILE:
	case OP_GREPWHILE:
	case OP_AND:
	case OP_OR:
	case OP_DOR:
	case OP_ANDASSIGN:
	case OP_ORASSIGN:
	case OP_DORASSIGN:
	case OP_COND_EXPR:
	case OP_RANGE:
	case OP_ONCE:
	    while (cLOGOP->op_other->op_type == OP_NULL)
		cLOGOP->op_other = cLOGOP->op_other->op_next;
	    CALL_PEEP(cLOGOP->op_other); /* Recursive calls are not replaced by fptr calls */
	    break;

	case OP_ENTERLOOP:
	case OP_ENTERITER:
	    while (cLOOP->op_redoop->op_type == OP_NULL)
		cLOOP->op_redoop = cLOOP->op_redoop->op_next;
	    CALL_PEEP(cLOOP->op_redoop);
	    while (cLOOP->op_nextop->op_type == OP_NULL)
		cLOOP->op_nextop = cLOOP->op_nextop->op_next;
	    CALL_PEEP(cLOOP->op_nextop);
	    while (cLOOP->op_lastop->op_type == OP_NULL)
		cLOOP->op_lastop = cLOOP->op_lastop->op_next;
	    CALL_PEEP(cLOOP->op_lastop);
	    break;

	case OP_SUBST:
	    assert(!(cPMOP->op_pmflags & PMf_ONCE));
	    while (cPMOP->op_pmstashstartu.op_pmreplstart &&
		   cPMOP->op_pmstashstartu.op_pmreplstart->op_type == OP_NULL)
		cPMOP->op_pmstashstartu.op_pmreplstart
		    = cPMOP->op_pmstashstartu.op_pmreplstart->op_next;
	    CALL_PEEP(cPMOP->op_pmstashstartu.op_pmreplstart);
	    break;

	case OP_EXEC:
	    if (o->op_next && o->op_next->op_type == OP_NEXTSTATE
		&& ckWARN(WARN_SYNTAX))
	    {
		if (o->op_next->op_sibling) {
		    const OPCODE type = o->op_next->op_sibling->op_type;
		    if (type != OP_EXIT && type != OP_WARN && type != OP_DIE) {
			const line_t oldline = CopLINE(PL_curcop);
			CopLINE_set(PL_curcop, CopLINE((COP*)o->op_next));
			Perl_warner(aTHX_ packWARN(WARN_EXEC),
				    "Statement unlikely to be reached");
			Perl_warner(aTHX_ packWARN(WARN_EXEC),
				    "\t(Maybe you meant system() when you said exec()?)\n");
			CopLINE_set(PL_curcop, oldline);
		    }
		}
	    }
	    break;

	case OP_HELEM: {
	    UNOP *rop;
            SV *lexname;
	    GV **fields;
	    SV **svp, *sv;
	    const char *key = NULL;
	    STRLEN keylen;

	    if (((BINOP*)o)->op_last->op_type != OP_CONST)
		break;

	    /* Make the CONST have a shared SV */
	    svp = cSVOPx_svp(((BINOP*)o)->op_last);
	    if ((!SvFAKE(sv = *svp) || !SvREADONLY(sv)) && !IS_PADCONST(sv)) {
		key = SvPV_const(sv, keylen);
		lexname = newSVpvn_share(key,
					 SvUTF8(sv) ? -(I32)keylen : (I32)keylen,
					 0);
		SvREFCNT_dec(sv);
		*svp = lexname;
	    }

	    if ((o->op_private & (OPpLVAL_INTRO)))
		break;

	    rop = (UNOP*)((BINOP*)o)->op_first;
	    if (rop->op_type != OP_RV2HV || rop->op_first->op_type != OP_PADSV)
		break;
	    lexname = *av_fetch(PL_comppad_name, rop->op_first->op_targ, TRUE);
	    if (!SvPAD_TYPED(lexname))
		break;
	    fields = (GV**)hv_fetchs(SvSTASH(lexname), "FIELDS", FALSE);
	    if (!fields || !GvHV(*fields))
		break;
	    key = SvPV_const(*svp, keylen);
	    if (!hv_fetch(GvHV(*fields), key,
			SvUTF8(*svp) ? -(I32)keylen : (I32)keylen, FALSE))
	    {
		Perl_croak(aTHX_ "No such class field \"%s\" " 
			   "in variable %s of type %s", 
		      key, SvPV_nolen_const(lexname), HvNAME_get(SvSTASH(lexname)));
	    }

            break;
        }

	case OP_HSLICE: {
	    UNOP *rop;
	    SV *lexname;
	    GV **fields;
	    SV **svp;
	    const char *key;
	    STRLEN keylen;
	    SVOP *first_key_op, *key_op;

	    if ((o->op_private & (OPpLVAL_INTRO))
		/* I bet there's always a pushmark... */
		|| ((LISTOP*)o)->op_first->op_sibling->op_type != OP_LIST)
		/* hmmm, no optimization if list contains only one key. */
		break;
	    rop = (UNOP*)((LISTOP*)o)->op_last;
	    if (rop->op_type != OP_RV2HV)
		break;
	    if (rop->op_first->op_type == OP_PADSV)
		/* @$hash{qw(keys here)} */
		rop = (UNOP*)rop->op_first;
	    else {
		/* @{$hash}{qw(keys here)} */
		if (rop->op_first->op_type == OP_SCOPE 
		    && cLISTOPx(rop->op_first)->op_last->op_type == OP_PADSV)
		{
		    rop = (UNOP*)cLISTOPx(rop->op_first)->op_last;
		}
		else
		    break;
	    }
		    
	    lexname = *av_fetch(PL_comppad_name, rop->op_targ, TRUE);
	    if (!SvPAD_TYPED(lexname))
		break;
	    fields = (GV**)hv_fetchs(SvSTASH(lexname), "FIELDS", FALSE);
	    if (!fields || !GvHV(*fields))
		break;
	    /* Again guessing that the pushmark can be jumped over.... */
	    first_key_op = (SVOP*)((LISTOP*)((LISTOP*)o)->op_first->op_sibling)
		->op_first->op_sibling;
	    for (key_op = first_key_op; key_op;
		 key_op = (SVOP*)key_op->op_sibling) {
		if (key_op->op_type != OP_CONST)
		    continue;
		svp = cSVOPx_svp(key_op);
		key = SvPV_const(*svp, keylen);
		if (!hv_fetch(GvHV(*fields), key, 
			    SvUTF8(*svp) ? -(I32)keylen : (I32)keylen, FALSE))
		{
		    Perl_croak(aTHX_ "No such class field \"%s\" "
			       "in variable %s of type %s",
			  key, SvPV_nolen(lexname), HvNAME_get(SvSTASH(lexname)));
		}
	    }
	    break;
	}

	case OP_SORT: {
	    /* will point to RV2AV or PADAV op on LHS/RHS of assign */
	    OP *oleft;
	    OP *o2;

	    /* check that RHS of sort is a single plain array */
	    OP *oright = cUNOPo->op_first;
	    if (!oright || oright->op_type != OP_PUSHMARK)
		break;

	    /* reverse sort ... can be optimised.  */
	    if (!cUNOPo->op_sibling) {
		/* Nothing follows us on the list. */
		OP * const reverse = o->op_next;

		if (reverse->op_type == OP_REVERSE &&
		    (reverse->op_flags & OPf_WANT) == OPf_WANT_LIST) {
		    OP * const pushmark = cUNOPx(reverse)->op_first;
		    if (pushmark && (pushmark->op_type == OP_PUSHMARK)
			&& (cUNOPx(pushmark)->op_sibling == o)) {
			/* reverse -> pushmark -> sort */
			o->op_private |= OPpSORT_REVERSE;
			op_null(reverse);
			pushmark->op_next = oright->op_next;
			op_null(oright);
		    }
		}
	    }

	    /* make @a = sort @a act in-place */

	    oright = cUNOPx(oright)->op_sibling;
	    if (!oright)
		break;
	    if (oright->op_type == OP_NULL) { /* skip sort block/sub */
		oright = cUNOPx(oright)->op_sibling;
	    }

	    if (!oright ||
		(oright->op_type != OP_RV2AV && oright->op_type != OP_PADAV)
		|| oright->op_next != o
		|| (oright->op_private & OPpLVAL_INTRO)
	    )
		break;

	    /* o2 follows the chain of op_nexts through the LHS of the
	     * assign (if any) to the aassign op itself */
	    o2 = o->op_next;
	    if (!o2 || o2->op_type != OP_NULL)
		break;
	    o2 = o2->op_next;
	    if (!o2 || o2->op_type != OP_PUSHMARK)
		break;
	    o2 = o2->op_next;
	    if (o2 && o2->op_type == OP_GV)
		o2 = o2->op_next;
	    if (!o2
		|| (o2->op_type != OP_PADAV && o2->op_type != OP_RV2AV)
		|| (o2->op_private & OPpLVAL_INTRO)
	    )
		break;
	    oleft = o2;
	    o2 = o2->op_next;
	    if (!o2 || o2->op_type != OP_NULL)
		break;
	    o2 = o2->op_next;
	    if (!o2 || o2->op_type != OP_AASSIGN
		    || (o2->op_flags & OPf_WANT) != OPf_WANT_VOID)
		break;

	    /* check that the sort is the first arg on RHS of assign */

	    o2 = cUNOPx(o2)->op_first;
	    if (!o2 || o2->op_type != OP_NULL)
		break;
	    o2 = cUNOPx(o2)->op_first;
	    if (!o2 || o2->op_type != OP_PUSHMARK)
		break;
	    if (o2->op_sibling != o)
		break;

	    /* check the array is the same on both sides */
	    if (oleft->op_type == OP_RV2AV) {
		if (oright->op_type != OP_RV2AV
		    || !cUNOPx(oright)->op_first
		    || cUNOPx(oright)->op_first->op_type != OP_GV
		    ||  cGVOPx_gv(cUNOPx(oleft)->op_first) !=
		       	cGVOPx_gv(cUNOPx(oright)->op_first)
		)
		    break;
	    }
	    else if (oright->op_type != OP_PADAV
		|| oright->op_targ != oleft->op_targ
	    )
		break;

	    /* transfer MODishness etc from LHS arg to RHS arg */
	    oright->op_flags = oleft->op_flags;
	    o->op_private |= OPpSORT_INPLACE;

	    /* excise push->gv->rv2av->null->aassign */
	    o2 = o->op_next->op_next;
	    op_null(o2); /* PUSHMARK */
	    o2 = o2->op_next;
	    if (o2->op_type == OP_GV) {
		op_null(o2); /* GV */
		o2 = o2->op_next;
	    }
	    op_null(o2); /* RV2AV or PADAV */
	    o2 = o2->op_next->op_next;
	    op_null(o2); /* AASSIGN */

	    o->op_next = o2->op_next;

	    break;
	}

	case OP_REVERSE: {
	    OP *ourmark, *theirmark, *ourlast, *iter, *expushmark, *rv2av;
	    OP *gvop = NULL;
	    LISTOP *enter, *exlist;

	    enter = (LISTOP *) o->op_next;
	    if (!enter)
		break;
	    if (enter->op_type == OP_NULL) {
		enter = (LISTOP *) enter->op_next;
		if (!enter)
		    break;
	    }
	    /* for $a (...) will have OP_GV then OP_RV2GV here.
	       for (...) just has an OP_GV.  */
	    if (enter->op_type == OP_GV) {
		gvop = (OP *) enter;
		enter = (LISTOP *) enter->op_next;
		if (!enter)
		    break;
		if (enter->op_type == OP_RV2GV) {
		  enter = (LISTOP *) enter->op_next;
		  if (!enter)
		    break;
		}
	    }

	    if (enter->op_type != OP_ENTERITER)
		break;

	    iter = enter->op_next;
	    if (!iter || iter->op_type != OP_ITER)
		break;
	    
	    expushmark = enter->op_first;
	    if (!expushmark || expushmark->op_type != OP_NULL
		|| expushmark->op_targ != OP_PUSHMARK)
		break;

	    exlist = (LISTOP *) expushmark->op_sibling;
	    if (!exlist || exlist->op_type != OP_NULL
		|| exlist->op_targ != OP_LIST)
		break;

	    if (exlist->op_last != o) {
		/* Mmm. Was expecting to point back to this op.  */
		break;
	    }
	    theirmark = exlist->op_first;
	    if (!theirmark || theirmark->op_type != OP_PUSHMARK)
		break;

	    if (theirmark->op_sibling != o) {
		/* There's something between the mark and the reverse, eg
		   for (1, reverse (...))
		   so no go.  */
		break;
	    }

	    ourmark = ((LISTOP *)o)->op_first;
	    if (!ourmark || ourmark->op_type != OP_PUSHMARK)
		break;

	    ourlast = ((LISTOP *)o)->op_last;
	    if (!ourlast || ourlast->op_next != o)
		break;

	    rv2av = ourmark->op_sibling;
	    if (rv2av && rv2av->op_type == OP_RV2AV && rv2av->op_sibling == 0
		&& rv2av->op_flags == (OPf_WANT_LIST | OPf_KIDS)
		&& enter->op_flags == (OPf_WANT_LIST | OPf_KIDS)) {
		/* We're just reversing a single array.  */
		rv2av->op_flags = OPf_WANT_SCALAR | OPf_KIDS | OPf_REF;
		enter->op_flags |= OPf_STACKED;
	    }

	    /* We don't have control over who points to theirmark, so sacrifice
	       ours.  */
	    theirmark->op_next = ourmark->op_next;
	    theirmark->op_flags = ourmark->op_flags;
	    ourlast->op_next = gvop ? gvop : (OP *) enter;
	    op_null(ourmark);
	    op_null(o);
	    enter->op_private |= OPpITER_REVERSED;
	    iter->op_private |= OPpITER_REVERSED;
	    
	    break;
	}

	case OP_SASSIGN: {
	    OP *rv2gv;
	    UNOP *refgen, *rv2cv;
	    LISTOP *exlist;

	    if ((o->op_flags & OPf_WANT) != OPf_WANT_VOID)
		break;

	    if ((o->op_private & ~OPpASSIGN_BACKWARDS) != 2)
		break;

	    rv2gv = ((BINOP *)o)->op_last;
	    if (!rv2gv || rv2gv->op_type != OP_RV2GV)
		break;

	    refgen = (UNOP *)((BINOP *)o)->op_first;

	    if (!refgen || refgen->op_type != OP_REFGEN)
		break;

	    exlist = (LISTOP *)refgen->op_first;
	    if (!exlist || exlist->op_type != OP_NULL
		|| exlist->op_targ != OP_LIST)
		break;

	    if (exlist->op_first->op_type != OP_PUSHMARK)
		break;

	    rv2cv = (UNOP*)exlist->op_last;

	    if (rv2cv->op_type != OP_RV2CV)
		break;

	    assert ((rv2gv->op_private & OPpDONT_INIT_GV) == 0);
	    assert ((o->op_private & OPpASSIGN_CV_TO_GV) == 0);
	    assert ((rv2cv->op_private & OPpMAY_RETURN_CONSTANT) == 0);

	    o->op_private |= OPpASSIGN_CV_TO_GV;
	    rv2gv->op_private |= OPpDONT_INIT_GV;
	    rv2cv->op_private |= OPpMAY_RETURN_CONSTANT;

	    break;
	}

	
	case OP_QR:
	case OP_MATCH:
	    if (!(cPMOP->op_pmflags & PMf_ONCE)) {
		assert (!cPMOP->op_pmstashstartu.op_pmreplstart);
	    }
	    break;
	}
	oldop = o;
    }
    LEAVE;
}
