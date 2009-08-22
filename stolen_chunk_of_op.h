#ifndef __STOLEN_CHUNK_OF_OP_H__
#define __STOLEN_CHUNK_OF_OP_H__

#if (PERL_VERSION < 10)
#define NCA_OP_OPT(o) (o->op_seq)
#define NCA_PMOP_STASHSTARTU(o) (o->op_pmreplstart)
#else
#define NCA_OP_OPT(o) (o->op_opt)
#define NCA_PMOP_STASHSTARTU(o) (o->op_pmstashstartu.op_pmreplstart)
#endif

void namespace_alias_peep (pTHX_ OP *);

#endif
