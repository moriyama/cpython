/***********************************************************
Copyright 1991, 1992, 1993 by Stichting Mathematisch Centrum,
Amsterdam, The Netherlands.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its 
documentation for any purpose and without fee is hereby granted, 
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in 
supporting documentation, and that the names of Stichting Mathematisch
Centrum or CWI not be used in advertising or publicity pertaining to
distribution of the software without specific, written prior permission.

STICHTING MATHEMATISCH CENTRUM DISCLAIMS ALL WARRANTIES WITH REGARD TO
THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS, IN NO EVENT SHALL STICHTING MATHEMATISCH CENTRUM BE LIABLE
FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

******************************************************************/

/* Built-in functions */

#include "allobjects.h"

#include "node.h"
#include "graminit.h"
#include "sysmodule.h"
#include "bltinmodule.h"
#include "import.h"
#include "pythonrun.h"
#include "ceval.h"
#include "modsupport.h"
#include "compile.h"
#include "eval.h"

/* Forward */
static object *filterstring PROTO((object *, object *));
static object *filtertuple  PROTO((object *, object *));
static object *exec_eval PROTO((object *v, int start));

static object *
builtin_abs(self, v)
	object *self;
	object *v;
{
	number_methods *nm;
	if (v == NULL || (nm = v->ob_type->tp_as_number) == NULL) {
		err_setstr(TypeError, "abs() requires numeric argument");
		return NULL;
	}
	return (*nm->nb_absolute)(v);
}

static object *
builtin_apply(self, args)
	object *self;
	object *args;
{
	object *func, *arglist;
	if (!getargs(args, "(OO)", &func, &arglist))
		return NULL;
	return call_object(func, arglist);
}

static object *
builtin_bagof(self, args)
	object *self;
	object *args;
{
	object *func, *seq, *arg, *result;
	sequence_methods *sqf;
	int len, newfunc = 0;
	register int i,j;
	static char bagof_err[] = "bagof() requires 1 or 2 args";

	if (args == NULL) {
		err_setstr(TypeError, bagof_err);
		return NULL;
	}

	if (is_tupleobject(args)) {
		if (gettuplesize(args) != 2) {
			err_setstr(TypeError, bagof_err);
			return NULL;
		}

		func = gettupleitem(args, 0);
		seq  = gettupleitem(args, 1);

		if (is_stringobject(func)) {
			if ((func = exec_eval(func, lambda_input)) == NULL)
				return NULL;
			newfunc = 1;
		}
	}
	else {
		func = None;
		seq  = args;
	}

	/* check for special cases; strings and tuples are returned as same */
	if (is_stringobject(seq)) {
		object *r = filterstring(func, seq);
		if (newfunc)
			DECREF(func);
		return r;
	}

	else if (is_tupleobject(seq)) {
		object *r = filtertuple(func, seq);
		if (newfunc)
			DECREF(func);
		return r;
	}

	if (! (sqf = seq->ob_type->tp_as_sequence)) {
		err_setstr(TypeError,
			   "argument to bagof() must be a sequence type");
		goto Fail_2;
	}

	if ((len = (*sqf->sq_length)(seq)) < 0)
		goto Fail_2;

	if (is_listobject(seq) && seq->ob_refcnt == 1) {
		INCREF(seq);
		result = seq;
	}
	else
		if ((result = newlistobject(len)) == NULL)
			goto Fail_2;

	if ((arg = newtupleobject(1)) == NULL)
		goto Fail_1;

	for (i = j = 0; i < len; ++i) {
		object *ele, *value;

		if (arg->ob_refcnt > 1) {
			DECREF(arg);
			if ((arg = newtupleobject(1)) == NULL)
				goto Fail_1;
		}

		if ((ele = (*sqf->sq_item)(seq, i)) == NULL)
			goto Fail_0;

		if (func == None)
			value = ele;
		else {
			if (settupleitem(arg, 0, ele) < 0)
				goto Fail_0;

			if ((value = call_object(func, arg)) == NULL)
				goto Fail_0;
		}

		if (testbool(value)) {
			INCREF(ele);
			if (setlistitem(result, j++, ele) < 0)
				goto Fail_0;
		}

		DECREF(value);
	}

	/* list_ass_slice() expects the rest of the list to be non-null */
	for (i = j; i < len; ++i) {
		INCREF(None);
		if (setlistitem(result, i, None) < 0)
			goto Fail_0;
	}

	DECREF(arg);
	if (newfunc)
		DECREF(func);

	(*result->ob_type->tp_as_sequence->sq_ass_slice)(result, j, len, NULL);
	return result;

Fail_0:
	DECREF(arg);
Fail_1:
	DECREF(result);
Fail_2:
	if (newfunc)
		DECREF(func);
	return NULL;
}

static object *
builtin_chr(self, args)
	object *self;
	object *args;
{
	long x;
	char s[1];
	if (!getargs(args, "l", &x))
		return NULL;
	if (x < 0 || x >= 256) {
		err_setstr(ValueError, "chr() arg not in range(256)");
		return NULL;
	}
	s[0] = x;
	return newsizedstringobject(s, 1);
}

static object *
builtin_cmp(self, args)
	object *self;
	object *args;
{
	object *a, *b;
	if (!getargs(args, "(OO)", &a, &b))
		return NULL;
	return newintobject((long)cmpobject(a, b));
}

static object *
builtin_coerce(self, args)
	object *self;
	object *args;
{
	object *v, *w;
	object *res;

	if (!getargs(args, "(OO)", &v, &w))
		return NULL;
	if (coerce(&v, &w) < 0)
		return NULL;
	res = mkvalue("(OO)", v, w);
	DECREF(v);
	DECREF(w);
	return res;
}

static object *
builtin_compile(self, args)
	object *self;
	object *args;
{
	char *str;
	char *filename;
	char *startstr;
	int start;
	if (!getargs(args, "(sss)", &str, &filename, &startstr))
		return NULL;
	if (strcmp(startstr, "exec") == 0)
		start = file_input;
	else if (strcmp(startstr, "eval") == 0)
		start = eval_input;
	else {
		err_setstr(ValueError,
			   "compile() mode must be 'exec' or 'eval'");
		return NULL;
	}
	return compile_string(str, filename, start);
}

static object *
builtin_dir(self, v)
	object *self;
	object *v;
{
	object *d;
	if (v == NULL) {
		d = getlocals();
		INCREF(d);
	}
	else {
		d = getattr(v, "__dict__");
		if (d == NULL) {
			err_setstr(TypeError,
				"dir() argument must have __dict__ attribute");
			return NULL;
		}
	}
	if (is_dictobject(d)) {
		v = getdictkeys(d);
		if (sortlist(v) != 0) {
			DECREF(v);
			v = NULL;
		}
	}
	else {
		v = newlistobject(0);
	}
	DECREF(d);
	return v;
}

static object *
builtin_divmod(self, args)
	object *self;
	object *args;
{
	object *v, *w, *x;
	if (!getargs(args, "(OO)", &v, &w))
		return NULL;
	if (v->ob_type->tp_as_number == NULL ||
				w->ob_type->tp_as_number == NULL) {
		err_setstr(TypeError, "divmod() requires numeric arguments");
		return NULL;
	}
	if (coerce(&v, &w) != 0)
		return NULL;
	x = (*v->ob_type->tp_as_number->nb_divmod)(v, w);
	DECREF(v);
	DECREF(w);
	return x;
}

static object *
exec_eval(v, start)
	object *v;
	int start;
{
	object *str = NULL, *globals = NULL, *locals = NULL;
	char *s;
	int n;
	/* XXX This is a bit of a mess.  Should make it varargs */
	if (v != NULL) {
		if (is_tupleobject(v) &&
				((n = gettuplesize(v)) == 2 || n == 3)) {
			str = gettupleitem(v, 0);
			globals = gettupleitem(v, 1);
			if (n == 3)
				locals = gettupleitem(v, 2);
		}
		else
			str = v;
	}
	if (str == NULL || (!is_stringobject(str) && !is_codeobject(str)) ||
			globals != NULL && !is_dictobject(globals) ||
			locals != NULL && !is_dictobject(locals)) {
		err_setstr(TypeError,
		  "eval/lambda arguments must be (string|code)[,dict[,dict]]");
		return NULL;
	}
	/* XXX The following is only correct for eval(), not for lambda() */
	if (is_codeobject(str))
		return eval_code((codeobject *) str, globals, locals,
				 (object *)NULL, (object *)NULL);
	s = getstringvalue(str);
	if (strlen(s) != getstringsize(str)) {
		err_setstr(ValueError, "embedded '\\0' in string arg");
		return NULL;
	}
	if (start == eval_input || start == lambda_input) {
		while (*s == ' ' || *s == '\t')
			s++;
	}
	return run_string(s, start, globals, locals);
}

static object *
builtin_eval(self, v)
	object *self;
	object *v;
{
	return exec_eval(v, eval_input);
}

static object *
builtin_execfile(self, v)
	object *self;
	object *v;
{
	object *str = NULL, *globals = NULL, *locals = NULL, *w;
	FILE* fp;
	char *s;
	int n;
	if (v != NULL) {
		if (is_stringobject(v))
			str = v;
		else if (is_tupleobject(v) &&
				((n = gettuplesize(v)) == 2 || n == 3)) {
			str = gettupleitem(v, 0);
			globals = gettupleitem(v, 1);
			if (n == 3)
				locals = gettupleitem(v, 2);
		}
	}
	if (str == NULL || !is_stringobject(str) ||
			globals != NULL && !is_dictobject(globals) ||
			locals != NULL && !is_dictobject(locals)) {
		err_setstr(TypeError,
		    "execfile arguments must be filename[,dict[,dict]]");
		return NULL;
	}
	s = getstringvalue(str);
	if (strlen(s) != getstringsize(str)) {
		err_setstr(ValueError, "embedded '\\0' in string arg");
		return NULL;
	}
	BGN_SAVE
	fp = fopen(s, "r");
	END_SAVE
	if (fp == NULL) {
		err_setstr(IOError, "execfile cannot open the file argument");
		return NULL;
	}
	w = run_file(fp, getstringvalue(str), file_input, globals, locals);
	BGN_SAVE
	fclose(fp);
	END_SAVE
	return w;
}

static object *
builtin_float(self, v)
	object *self;
	object *v;
{
	number_methods *nb;
	
	if (v == NULL || (nb = v->ob_type->tp_as_number) == NULL ||
	    nb->nb_float == NULL) {
		err_setstr(TypeError,
			   "float() argument can't be converted to float");
		return NULL;
	}
	return (*nb->nb_float)(v);
}

static object *
builtin_getattr(self, args)
	object *self;
	object *args;
{
	object *v;
	object *name;
	if (!getargs(args, "(OS)", &v, &name))
		return NULL;
	return getattro(v, name);
}

static object *
builtin_hasattr(self, args)
	object *self;
	object *args;
{
	object *v;
	object *name;
	if (!getargs(args, "(OS)", &v, &name))
		return NULL;
	v = getattro(v, name);
	if (v == NULL) {
		err_clear();
		return newintobject(0L);
	}
	DECREF(v);
	return newintobject(1L);
}

static object *
builtin_id(self, args)
	object *self;
	object *args;
{
	object *v;
	if (!getargs(args, "O", &v))
		return NULL;
	return newintobject((long)v);
}

static object *
builtin_map(self, args)
	object *self;
	object *args;
{
	typedef struct {
		object *seq;
		sequence_methods *sqf;
		int len;
	} sequence;

	object *func, *result;
	sequence *seqs = NULL, *sqp;
	int n, len, newfunc = 0;
	register int i, j;

	if (args == NULL || !is_tupleobject(args)) {
		err_setstr(TypeError, "map() requires at least two args");
		return NULL;
	}

	func = gettupleitem(args, 0);
	n    = gettuplesize(args) - 1;

	if (is_stringobject(func)) {
		if ((func = exec_eval(func, lambda_input)) == NULL)
			return NULL;
		newfunc = 1;
	}

	if ((seqs = (sequence *) malloc(n * sizeof(sequence))) == NULL)
		return err_nomem();

	for (len = -1, i = 0, sqp = seqs; i < n; ++i, ++sqp) {
		int curlen;
	
		if ((sqp->seq = gettupleitem(args, i + 1)) == NULL)
			goto Fail_2;

		if (! (sqp->sqf = sqp->seq->ob_type->tp_as_sequence)) {
			static char errmsg[] =
			    "argument %d to map() must be a sequence object";
			char errbuf[sizeof(errmsg) + 3];

			sprintf(errbuf, errmsg, i+2);
			err_setstr(TypeError, errbuf);
			goto Fail_2;
		}

		if ((curlen = sqp->len = (*sqp->sqf->sq_length)(sqp->seq)) < 0)
			goto Fail_2;

		if (curlen > len)
			len = curlen;
	}

	if ((result = (object *) newlistobject(len)) == NULL)
		goto Fail_2;

	if ((args = newtupleobject(n)) == NULL)
		goto Fail_1;

	for (i = 0; i < len; ++i) {
		object *arg, *value;

		if (args->ob_refcnt > 1) {
			DECREF(args);
			if ((args = newtupleobject(n)) == NULL)
				goto Fail_1;
		}

		for (j = 0, sqp = seqs; j < n; ++j, ++sqp) {
			if (i >= sqp->len) {
				INCREF(None);
				if (settupleitem(args, j, None) < 0)
					goto Fail_0;
				arg = None;
			}

			else {
				if ((arg = (*sqp->sqf->sq_item)(sqp->seq, i)) == NULL)
					goto Fail_0;

				if (settupleitem(args, j, arg) < 0)
					goto Fail_0;
			}
		}

		if (func == None) {
			if (n == 1)	{ /* avoid creating singleton */
				INCREF(arg);
				if (setlistitem(result, i, arg) < 0)
					goto Fail_0;
			}
			else {
				INCREF(args);
				if (setlistitem(result, i, args) < 0)
					goto Fail_0;
			}
		}
		else {
			if ((value = call_object(func, args)) == NULL)
				goto Fail_0;

			if (setlistitem((object *) result, i, value) < 0)
				goto Fail_0;
		}
	}

	if (seqs) free(seqs);

	DECREF(args);
	if (newfunc)
		DECREF(func);

	return result;

Fail_0:
	DECREF(args);
Fail_1:
	DECREF(result);
Fail_2:
	if (newfunc)
		DECREF(func);

	if (seqs) free(seqs);

	return NULL;
}

static object *
builtin_setattr(self, args)
	object *self;
	object *args;
{
	object *v;
	object *name;
	object *value;
	if (!getargs(args, "(OSO)", &v, &name, &value))
		return NULL;
	if (setattro(v, name, value) != 0)
		return NULL;
	INCREF(None);
	return None;
}

static object *
builtin_hash(self, args)
	object *self;
	object *args;
{
	object *v;
	long x;
	if (!getargs(args, "O", &v))
		return NULL;
	x = hashobject(v);
	if (x == -1)
		return NULL;
	return newintobject(x);
}

static object *
builtin_hex(self, v)
	object *self;
	object *v;
{
	number_methods *nb;
	
	if (v == NULL || (nb = v->ob_type->tp_as_number) == NULL ||
	    nb->nb_hex == NULL) {
		err_setstr(TypeError,
			   "hex() argument can't be converted to hex");
		return NULL;
	}
	return (*nb->nb_hex)(v);
}

static object *builtin_raw_input PROTO((object *, object *));

static object *
builtin_input(self, v)
	object *self;
	object *v;
{
	object *line = builtin_raw_input(self, v);
	if (line == NULL)
		return line;
	v = exec_eval(line, eval_input);
	DECREF(line);
	return v;
}

static object *
builtin_int(self, v)
	object *self;
	object *v;
{
	number_methods *nb;
	
	if (v == NULL || (nb = v->ob_type->tp_as_number) == NULL ||
	    nb->nb_int == NULL) {
		err_setstr(TypeError,
			   "int() argument can't be converted to int");
		return NULL;
	}
	return (*nb->nb_int)(v);
}

static object *
builtin_lambda(self, v)
	object *self;
	object *v;
{
	return exec_eval(v, lambda_input);
}

static object *
builtin_len(self, v)
	object *self;
	object *v;
{
	long len;
	typeobject *tp;
	if (v == NULL) {
		err_setstr(TypeError, "len() without argument");
		return NULL;
	}
	tp = v->ob_type;
	if (tp->tp_as_sequence != NULL) {
		len = (*tp->tp_as_sequence->sq_length)(v);
	}
	else if (tp->tp_as_mapping != NULL) {
		len = (*tp->tp_as_mapping->mp_length)(v);
	}
	else {
		err_setstr(TypeError, "len() of unsized object");
		return NULL;
	}
	if (len < 0)
		return NULL;
	else
		return newintobject(len);
}

static object *
builtin_long(self, v)
	object *self;
	object *v;
{
	number_methods *nb;
	
	if (v == NULL || (nb = v->ob_type->tp_as_number) == NULL ||
	    nb->nb_long == NULL) {
		err_setstr(TypeError,
			   "long() argument can't be converted to long");
		return NULL;
	}
	return (*nb->nb_long)(v);
}

static object *
min_max(v, sign)
	object *v;
	int sign;
{
	int i, n, cmp;
	object *w, *x;
	sequence_methods *sq;
	if (v == NULL) {
		err_setstr(TypeError, "min() or max() without argument");
		return NULL;
	}
	sq = v->ob_type->tp_as_sequence;
	if (sq == NULL) {
		err_setstr(TypeError, "min() or max() of non-sequence");
		return NULL;
	}
	n = (*sq->sq_length)(v);
	if (n < 0)
		return NULL;
	if (n == 0) {
		err_setstr(ValueError, "min() or max() of empty sequence");
		return NULL;
	}
	w = (*sq->sq_item)(v, 0); /* Implies INCREF */
	for (i = 1; i < n; i++) {
		x = (*sq->sq_item)(v, i); /* Implies INCREF */
		cmp = cmpobject(x, w);
		if (cmp * sign > 0) {
			DECREF(w);
			w = x;
		}
		else
			DECREF(x);
	}
	return w;
}

static object *
builtin_min(self, v)
	object *self;
	object *v;
{
	return min_max(v, -1);
}

static object *
builtin_max(self, v)
	object *self;
	object *v;
{
	return min_max(v, 1);
}

static object *
builtin_oct(self, v)
	object *self;
	object *v;
{
	number_methods *nb;
	
	if (v == NULL || (nb = v->ob_type->tp_as_number) == NULL ||
	    nb->nb_oct == NULL) {
		err_setstr(TypeError,
			   "oct() argument can't be converted to oct");
		return NULL;
	}
	return (*nb->nb_oct)(v);
}

static object *
builtin_open(self, args)
	object *self;
	object *args;
{
	char *name, *mode;
	if (!getargs(args, "(ss)", &name, &mode))
		return NULL;
	return newfileobject(name, mode);
}

static object *
builtin_ord(self, args)
	object *self;
	object *args;
{
	char *s;
	int len;
	if (!getargs(args, "s#", &s, &len))
		return NULL;
	if (len != 1) {
		err_setstr(ValueError, "ord() arg must have length 1");
		return NULL;
	}
	return newintobject((long)(s[0] & 0xff));
}

static object *
builtin_pow(self, args)
	object *self;
	object *args;
{
	object *v, *w, *x;
	if (!getargs(args, "(OO)", &v, &w))
		return NULL;
	if (v->ob_type->tp_as_number == NULL ||
				w->ob_type->tp_as_number == NULL) {
		err_setstr(TypeError, "pow() requires numeric arguments");
		return NULL;
	}
	if (coerce(&v, &w) != 0)
		return NULL;
	x = (*v->ob_type->tp_as_number->nb_power)(v, w);
	DECREF(v);
	DECREF(w);
	return x;
}

static object *
builtin_range(self, v)
	object *self;
	object *v;
{
	static char *errmsg = "range() requires 1-3 int arguments";
	int i, n;
	long ilow, ihigh, istep;
	if (v != NULL && is_intobject(v)) {
		ilow = 0; ihigh = getintvalue(v); istep = 1;
	}
	else if (v == NULL || !is_tupleobject(v)) {
		err_setstr(TypeError, errmsg);
		return NULL;
	}
	else {
		n = gettuplesize(v);
		if (n < 1 || n > 3) {
			err_setstr(TypeError, errmsg);
			return NULL;
		}
		for (i = 0; i < n; i++) {
			if (!is_intobject(gettupleitem(v, i))) {
				err_setstr(TypeError, errmsg);
				return NULL;
			}
		}
		if (n == 3) {
			istep = getintvalue(gettupleitem(v, 2));
			--n;
		}
		else
			istep = 1;
		ihigh = getintvalue(gettupleitem(v, --n));
		if (n > 0)
			ilow = getintvalue(gettupleitem(v, 0));
		else
			ilow = 0;
	}
	if (istep == 0) {
		err_setstr(ValueError, "zero step for range()");
		return NULL;
	}
	/* XXX ought to check overflow of subtraction */
	if (istep > 0)
		n = (ihigh - ilow + istep - 1) / istep;
	else
		n = (ihigh - ilow + istep + 1) / istep;
	if (n < 0)
		n = 0;
	v = newlistobject(n);
	if (v == NULL)
		return NULL;
	for (i = 0; i < n; i++) {
		object *w = newintobject(ilow);
		if (w == NULL) {
			DECREF(v);
			return NULL;
		}
		setlistitem(v, i, w);
		ilow += istep;
	}
	return v;
}

static object *
builtin_xrange(self, v)
	object *self;
	object *v;
{
	static char *errmsg = "xrange() requires 1-3 int arguments";
	int i, n;
	long start, stop, step, len;
	if (v != NULL && is_intobject(v))
		start = 0, stop = getintvalue(v), step = 1;

	else if (v == NULL || !is_tupleobject(v)) {
		err_setstr(TypeError, errmsg);
		return NULL;
	}
	else {
		n = gettuplesize(v);
		if (n < 1 || n > 3) {
			err_setstr(TypeError, errmsg);
			return NULL;
		}
		for (i = 0; i < n; i++) {
			if (!is_intobject(gettupleitem(v, i))) {
				err_setstr(TypeError, errmsg);
				return NULL;
			}
		}
		if (n == 3) {
			step = getintvalue(gettupleitem(v, 2));
			--n;
		}
		else
			step = 1;
		stop = getintvalue(gettupleitem(v, --n));
		if (n > 0)
			start = getintvalue(gettupleitem(v, 0));
		else
			start = 0;
	}

	if (step == 0) {
		err_setstr(ValueError, "zero step for xrange()");
		return NULL;
	}

	len = (stop - start + step + ((step > 0) ? -1 : 1)) / step;
	if (len < 0)
		len = 0;

	return newrangeobject(start, len, step, 1);
}

static object *
builtin_raw_input(self, v)
	object *self;
	object *v;
{
	object *f = sysget("stdout");
	if (f == NULL) {
		err_setstr(RuntimeError, "lost sys.stdout");
		return NULL;
	}
	flushline();
	if (v != NULL) {
		if (writeobject(v, f, PRINT_RAW) != 0)
			return NULL;
	}
	return filegetline(sysget("stdin"), -1);
}

static object *
builtin_reduce(self, args)
	object *self;
	object *args;
{
	object *seq, *func, *result;
	sequence_methods *sqf;
	static char reduce_err[] = "reduce() requires 2 or 3 args";
	register int i;
	int start = 0, newfunc = 0;
	int len;

	if (args == NULL || !is_tupleobject(args)) {
		err_setstr(TypeError, reduce_err);
		return NULL;
	}

	switch (gettuplesize(args)) {
	case 2:
		start = 1;		/* fall through */
	case 3:
		func = gettupleitem(args, 0);
		seq  = gettupleitem(args, 1);
		break;
	default:
		err_setstr(TypeError, reduce_err);
	}

	if ((sqf = seq->ob_type->tp_as_sequence) == NULL) {
		err_setstr(TypeError,
		    "2nd argument to reduce() must be a sequence object");
		return NULL;
	}

	if (is_stringobject(func)) {
		if ((func = exec_eval(func, lambda_input)) == NULL)
			return NULL;
		newfunc = 1;
	}

	if ((len = (*sqf->sq_length)(seq)) < 0)
		goto Fail_2;

	if (start == 1) {
		if (len == 0) {
			err_setstr(TypeError,
			    "reduce of empty sequence with no initial value");
			goto Fail_2;
		}

		if ((result = (*sqf->sq_item)(seq, 0)) == NULL)
			goto Fail_2;
	}
	else {
		result = gettupleitem(args, 2);
		INCREF(result);
	}

	if ((args = newtupleobject(2)) == NULL)
		goto Fail_1;

	for (i = start; i < len; ++i) {
		object *op2;

		if (args->ob_refcnt > 1) {
			DECREF(args);
			if ((args = newtupleobject(2)) == NULL)
				goto Fail_1;
		}

		if ((op2 = (*sqf->sq_item)(seq, i)) == NULL)
			goto Fail_2;

		settupleitem(args, 0, result);
		settupleitem(args, 1, op2);
		if ((result = call_object(func, args)) == NULL)
			goto Fail_0;
	}

	DECREF(args);
	if (newfunc)
		DECREF(func);

	return result;

	/* XXX I hate goto's. I hate goto's. I hate goto's. I hate goto's. */
Fail_0:
	DECREF(args);
	goto Fail_2;
Fail_1:
	DECREF(result);
Fail_2:
	if (newfunc)
		DECREF(func);
	return NULL;
}

static object *
builtin_reload(self, v)
	object *self;
	object *v;
{
	return reload_module(v);
}

static object *
builtin_repr(self, v)
	object *self;
	object *v;
{
	if (v == NULL) {
		err_badarg();
		return NULL;
	}
	return reprobject(v);
}

static object *
builtin_round(self, args)
	object *self;
	object *args;
{
	extern double floor PROTO((double));
	extern double ceil PROTO((double));
	double x;
	double f;
	int ndigits = 0;
	int sign = 1;
	int i;
	if (!getargs(args, "d", &x)) {
		err_clear();
		if (!getargs(args, "(di)", &x, &ndigits))
			return NULL;
	}
	f = 1.0;
	for (i = ndigits; --i >= 0; )
		f = f*10.0;
	for (i = ndigits; ++i <= 0; )
		f = f*0.1;
	if (x >= 0.0)
		return newfloatobject(floor(x*f + 0.5) / f);
	else
		return newfloatobject(ceil(x*f - 0.5) / f);
}

static object *
builtin_str(self, v)
	object *self;
	object *v;
{
	if (v == NULL) {
		err_badarg();
		return NULL;
	}
	if (is_stringobject(v)) {
		INCREF(v);
		return v;
	}
	else
		return reprobject(v);
}

static object *
builtin_type(self, v)
	object *self;
	object *v;
{
	if (v == NULL) {
		err_setstr(TypeError, "type() requires an argument");
		return NULL;
	}
	v = (object *)v->ob_type;
	INCREF(v);
	return v;
}

static struct methodlist builtin_methods[] = {
	{"abs",		builtin_abs},
	{"apply",	builtin_apply},
	{"bagof",	builtin_bagof},
	{"chr",		builtin_chr},
	{"cmp",		builtin_cmp},
	{"coerce",	builtin_coerce},
	{"compile",	builtin_compile},
	{"dir",		builtin_dir},
	{"divmod",	builtin_divmod},
	{"eval",	builtin_eval},
	{"execfile",	builtin_execfile},
	{"float",	builtin_float},
	{"getattr",	builtin_getattr},
	{"hasattr",	builtin_hasattr},
	{"hash",	builtin_hash},
	{"hex",		builtin_hex},
	{"id",		builtin_id},
	{"input",	builtin_input},
	{"int",		builtin_int},
	{"lambda",	builtin_lambda},
	{"len",		builtin_len},
	{"long",	builtin_long},
	{"map",		builtin_map},
	{"max",		builtin_max},
	{"min",		builtin_min},
	{"oct",		builtin_oct},
	{"open",	builtin_open},
	{"ord",		builtin_ord},
	{"pow",		builtin_pow},
	{"range",	builtin_range},
	{"raw_input",	builtin_raw_input},
	{"reduce",	builtin_reduce},
	{"reload",	builtin_reload},
	{"repr",	builtin_repr},
	{"round",	builtin_round},
	{"setattr",	builtin_setattr},
	{"str",		builtin_str},
	{"type",	builtin_type},
	{"xrange",	builtin_xrange},
	{NULL,		NULL},
};

static object *builtin_dict;

object *
getbuiltin(name)
	object *name;
{
	return dict2lookup(builtin_dict, name);
}

/* Predefined exceptions */

object *AccessError;
object *AttributeError;
object *ConflictError;
object *EOFError;
object *IOError;
object *ImportError;
object *IndexError;
object *KeyError;
object *KeyboardInterrupt;
object *MemoryError;
object *NameError;
object *OverflowError;
object *RuntimeError;
object *SyntaxError;
object *SystemError;
object *SystemExit;
object *TypeError;
object *ValueError;
object *ZeroDivisionError;

static object *
newstdexception(name)
	char *name;
{
	object *v = newstringobject(name);
	if (v == NULL || dictinsert(builtin_dict, name, v) != 0)
		fatal("no mem for new standard exception");
	return v;
}

static void
initerrors()
{
	AccessError = newstdexception("AccessError");
	AttributeError = newstdexception("AttributeError");
	ConflictError = newstdexception("ConflictError");
	EOFError = newstdexception("EOFError");
	IOError = newstdexception("IOError");
	ImportError = newstdexception("ImportError");
	IndexError = newstdexception("IndexError");
	KeyError = newstdexception("KeyError");
	KeyboardInterrupt = newstdexception("KeyboardInterrupt");
	MemoryError = newstdexception("MemoryError");
	NameError = newstdexception("NameError");
	OverflowError = newstdexception("OverflowError");
	RuntimeError = newstdexception("RuntimeError");
	SyntaxError = newstdexception("SyntaxError");
	SystemError = newstdexception("SystemError");
	SystemExit = newstdexception("SystemExit");
	TypeError = newstdexception("TypeError");
	ValueError = newstdexception("ValueError");
	ZeroDivisionError = newstdexception("ZeroDivisionError");
}

void
initbuiltin()
{
	object *m;
	m = initmodule("__builtin__", builtin_methods);
	builtin_dict = getmoduledict(m);
	INCREF(builtin_dict);
	initerrors();
	(void) dictinsert(builtin_dict, "None", None);
}

/* Coerce two numeric types to the "larger" one.
   Increment the reference count on each argument.
   Return -1 and raise an exception if no coercion is possible
   (and then no reference count is incremented).
*/

int
coerce(pv, pw)
	object **pv, **pw;
{
	register object *v = *pv;
	register object *w = *pw;
	int res;

	if (v->ob_type == w->ob_type && !is_instanceobject(v)) {
		INCREF(v);
		INCREF(w);
		return 0;
	}
	if (v->ob_type->tp_as_number && v->ob_type->tp_as_number->nb_coerce) {
		res = (*v->ob_type->tp_as_number->nb_coerce)(pv, pw);
		if (res <= 0)
			return res;
	}
	if (w->ob_type->tp_as_number && w->ob_type->tp_as_number->nb_coerce) {
		res = (*w->ob_type->tp_as_number->nb_coerce)(pw, pv);
		if (res <= 0)
			return res;
	}
	err_setstr(TypeError, "number coercion failed");
	return -1;
}


/* Filter a tuple through a function */

static object *
filtertuple(func, tuple)
	object *func;
	object *tuple;
{
	object *arg, *result;
	register int i, j;
	int len = gettuplesize(tuple), shared = 0;

	if (tuple->ob_refcnt == 1) {
		result = tuple;
		shared = 1;
		/* defer INCREF (resizetuple wants it to be one) */
	}
	else
		if ((result = newtupleobject(len)) == NULL)
			return NULL;

	if ((arg = newtupleobject(1)) == NULL)
		goto Fail_1;

	for (i = j = 0; i < len; ++i) {
		object *ele, *value;

		if (arg->ob_refcnt > 1) {
			DECREF(arg);
			if ((arg = newtupleobject(1)) == NULL)
				goto Fail_1;
		}

		if ((ele = gettupleitem(tuple, i)) == NULL)
			goto Fail_0;
		INCREF(ele);

		if (func == None)
			value = ele;
		else {
			if (settupleitem(arg, 0, ele) < 0)
				goto Fail_0;

			if ((value = call_object(func, arg)) == NULL)
				goto Fail_0;
		}

		if (testbool(value)) {
			INCREF(ele);
			if (settupleitem(result, j++, ele) < 0)
				goto Fail_0;
		}

		DECREF(value);
	}

	DECREF(arg);
	if (resizetuple(&result, j) < 0)
		return NULL;

	if (shared)
		INCREF(result);

	return result;

Fail_0:
	DECREF(arg);
Fail_1:
	if (!shared)
		DECREF(result);
	return NULL;
}


/* Filter a string through a function */

static object *
filterstring(func, strobj)
	object *func;
	object *strobj;
{
	object *arg, *result;
	register int i, j;
	int len = getstringsize(strobj), shared = 0;

	if (strobj->ob_refcnt == 1) {
		result = strobj;
		shared = 1;
		/* defer INCREF (resizestring wants it to be one) */

		if (func == None) {
			INCREF(result);
			return result;
		}
	}
	else {
		if ((result = newsizedstringobject(NULL, len)) == NULL)
			return NULL;

		if (func == None) {
			strcpy(GETSTRINGVALUE((stringobject *)result),
			       GETSTRINGVALUE((stringobject *)strobj));
			return result;
		}
	}

	if ((arg = newtupleobject(1)) == NULL)
		goto Fail_1;

	for (i = j = 0; i < len; ++i) {
		object *ele, *value;

		if (arg->ob_refcnt > 1) {
			DECREF(arg);
			if ((arg = newtupleobject(1)) == NULL)
				goto Fail_1;
		}

		if ((ele = (*strobj->ob_type->tp_as_sequence->sq_item)
		           (strobj, i)) == NULL)
			goto Fail_0;

		if (settupleitem(arg, 0, ele) < 0)
			goto Fail_0;

		if ((value = call_object(func, arg)) == NULL)
			goto Fail_0;

		if (testbool(value))
			GETSTRINGVALUE((stringobject *)result)[j++] =
				GETSTRINGVALUE((stringobject *)ele)[0];

		DECREF(value);
	}

	DECREF(arg);
	if (resizestring(&result, j) < 0)
		return NULL;

	if (shared)
		INCREF(result);

	return result;

Fail_0:
	DECREF(arg);
Fail_1:
	if (!shared)
		DECREF(result);
	return NULL;
}
