/*	$NetBSD: fio.c,v 1.44 2023/08/10 20:36:28 mrg Exp $	*/

/*
 * Copyright (c) 1980, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#ifndef lint
#if 0
static char sccsid[] = "@(#)fio.c	8.2 (Berkeley) 4/20/95";
#else
__RCSID("$NetBSD: fio.c,v 1.44 2023/08/10 20:36:28 mrg Exp $");
#endif
#endif /* not lint */

#include "rcv.h"
#include "extern.h"
#include "thread.h"
#include "sig.h"
#include <wordexp.h>

/*
 * Mail -- a mail program
 *
 * File I/O.
 */

#ifndef THREAD_SUPPORT
/************************************************************************/
/*
 * If we have threading support, these routines live in thread.c.
 */
static struct message *message;		/* message structure array */
static int msgCount;			/* Count of messages read in */

PUBLIC struct message *
next_message(struct message *mp)
{
	if (mp + 1 < message || mp + 1 >= message + msgCount)
		return NULL;

	return mp + 1;
}

PUBLIC struct message *
prev_message(struct message *mp)
{
	if (mp - 1 < message || mp - 1 >= message + msgCount)
		return NULL;

	return mp - 1;
}

PUBLIC struct message *
get_message(int msgnum)
{
	if (msgnum < 1 || msgnum > msgCount)
		return NULL;

	return message + msgnum - 1;
}

PUBLIC int
get_msgnum(struct message *mp)
{
	if (mp < message || mp >= message + msgCount)
		return 0;

	return mp - message + 1;
}

PUBLIC int
get_msgCount(void)
{
	return msgCount;
}
#endif /* THREAD_SUPPORT */
/************************************************************************/

/*
 * Initialize a message structure.
 */
static void
message_init(struct message *mp, off_t offset, short flags)
{
	/* use memset so new fields are always zeroed */
	(void)memset(mp, 0, sizeof(*mp));
	mp->m_flag = flags;
	mp->m_block = blockof(offset);
	mp->m_offset = blkoffsetof(offset);
}

/*
 * Take the data out of the passed ghost file and toss it into
 * a dynamically allocated message structure.
 */
static void
makemessage(FILE *f, int omsgCount, int nmsgCount)
{
	size_t size;
	struct message *omessage;	/* old message structure array */
	struct message *nmessage;
	ptrdiff_t off;

	omessage = get_abs_message(1);

	size = (nmsgCount + 1) * sizeof(*nmessage);

	if (omsgCount == 0 || omessage == NULL)
		off = 0;
	else
		off = dot - omessage;
	nmessage = realloc(omessage, size);
	if (nmessage == NULL)
		err(EXIT_FAILURE,
		    "Insufficient memory for %d messages", nmsgCount);
	dot = nmessage + off;

	thread_fix_old_links(nmessage, off, omsgCount);

#ifndef THREAD_SUPPORT
	message = nmessage;
#endif
	size -= (omsgCount + 1) * sizeof(*nmessage);
	(void)fflush(f);
	(void)lseek(fileno(f), (off_t)sizeof(*nmessage), SEEK_SET);
	if (read(fileno(f), &nmessage[omsgCount], size) != (ssize_t)size)
		errx(EXIT_FAILURE, "Message temporary file corrupted");

	message_init(&nmessage[nmsgCount], (off_t)0, 0); /* append a dummy */

	thread_fix_new_links(nmessage, omsgCount, nmsgCount);

	(void)Fclose(f);
}

/*
 * Append the passed message descriptor onto the temp file.
 * If the write fails, return 1, else 0
 */
static int
append(struct message *mp, FILE *f)
{
	return fwrite(mp, sizeof(*mp), 1, f) != 1;
}

/*
 * Set up the input pointers while copying the mail file into /tmp.
 */
PUBLIC void
setptr(FILE *ibuf, off_t offset)
{
	int c;
	size_t len;
	char *cp;
	const char *cp2;
	struct message this;
	FILE *mestmp;
	int maybe, inhead;
	char linebuf[LINESIZE];
	int omsgCount;
#ifdef THREAD_SUPPORT
	int nmsgCount;
#else
# define nmsgCount	msgCount
#endif

	/* Get temporary file. */
	(void)snprintf(linebuf, LINESIZE, "%s/mail.XXXXXX", tmpdir);
	if ((c = mkstemp(linebuf)) == -1 ||
	    (mestmp = Fdopen(c, "ref+")) == NULL) {
		(void)fprintf(stderr, "mail: can't open %s\n", linebuf);
		exit(1);
	}
	(void)unlink(linebuf);

	nmsgCount = get_abs_msgCount();
	if (offset == 0) {
		nmsgCount = 0;
	} else {
		/* Seek into the file to get to the new messages */
		(void)fseeko(ibuf, offset, 0);
		/*
		 * We need to make "offset" a pointer to the end of
		 * the temp file that has the copy of the mail file.
		 * If any messages have been edited, this will be
		 * different from the offset into the mail file.
		 */
		(void)fseek(otf, 0L, SEEK_END);
		offset = ftell(otf);
	}
	omsgCount = nmsgCount;
	maybe = 1;
	inhead = 0;
	message_init(&this, (off_t)0, MUSED|MNEW);

	for (;;) {
		if (fgets(linebuf, LINESIZE, ibuf) == NULL) {
			if (append(&this, mestmp))
				err(EXIT_FAILURE, "temporary file");
			makemessage(mestmp, omsgCount, nmsgCount);
			return;
		}
		len = strlen(linebuf);
		/*
		 * Transforms lines ending in <CR><LF> to just <LF>.
		 * This allows mail to be able to read Eudora mailboxes
		 * that reside on a DOS partition.
		 */
		if (len >= 2 && linebuf[len - 1] == '\n' &&
		    linebuf[len - 2] == '\r') {
			linebuf[len - 2] = '\n';
			len--;
		}
		(void)fwrite(linebuf, sizeof(*linebuf), len, otf);
		if (ferror(otf))
			err(EXIT_FAILURE, "/tmp");
		if (len)
			linebuf[len - 1] = 0;
		if (maybe && linebuf[0] == 'F' && ishead(linebuf)) {
			nmsgCount++;
			if (append(&this, mestmp))
				err(EXIT_FAILURE, "temporary file");
			message_init(&this, offset, MUSED|MNEW);
			inhead = 1;
		} else if (linebuf[0] == 0) {
			inhead = 0;
		} else if (inhead) {
			for (cp = linebuf, cp2 = "status";; cp++) {
				if ((c = *cp2++) == 0) {
					while (isspace((unsigned char)*cp++))
						continue;
					if (cp[-1] != ':')
						break;
					while ((c = *cp++) != '\0')
						if (c == 'R')
							this.m_flag |= MREAD;
						else if (c == 'O')
							this.m_flag &= ~MNEW;
					inhead = 0;
					break;
				}
				if (*cp != c && *cp != toupper(c))
					break;
			}
		}
		offset += len;
		this.m_size += len;
		this.m_lines++;
		if (!inhead) {
			int lines_plus_wraps = 1;
			int linelen = (int)strlen(linebuf);

			if (screenwidth && (int)linelen > screenwidth) {
				lines_plus_wraps = linelen / screenwidth;
				if (linelen % screenwidth != 0)
					++lines_plus_wraps;
			}
			this.m_blines += lines_plus_wraps;
		}
		maybe = linebuf[0] == 0;
	}
}

/*
 * Drop the passed line onto the passed output buffer.
 * If a write error occurs, return -1, else the count of
 * characters written, including the newline if requested.
 */
PUBLIC int
putline(FILE *obuf, const char *linebuf, int outlf)
{
	size_t c;

	c = strlen(linebuf);
	(void)fwrite(linebuf, sizeof(*linebuf), c, obuf);
	if (outlf) {
		(void)putc('\n', obuf);
		c++;
	}
	if (ferror(obuf))
		return -1;
	return (int)c;
}

/*
 * Read up a line from the specified input into the line
 * buffer.  Return the number of characters read.  Do not
 * include the newline at the end.
 */
PUBLIC int
readline(FILE *ibuf, char *linebuf, int linesize, int no_restart)
{
	struct sigaction osa_sigtstp;
	struct sigaction osa_sigttin;
	struct sigaction osa_sigttou;
	int n;

	clearerr(ibuf);

	sig_check();
	if (no_restart) {
		(void)sig_setflags(SIGTSTP, 0, &osa_sigtstp);
		(void)sig_setflags(SIGTTIN, 0, &osa_sigttin);
		(void)sig_setflags(SIGTTOU, 0, &osa_sigttou);
	}
	if (fgets(linebuf, linesize, ibuf) == NULL)
		n = -1;
	else {
		n = (int)strlen(linebuf);
		if (n > 0 && linebuf[n - 1] == '\n')
			linebuf[--n] = '\0';
	}
	if (no_restart) {
		(void)sigaction(SIGTSTP, &osa_sigtstp, NULL);
		(void)sigaction(SIGTTIN, &osa_sigttin, NULL);
		(void)sigaction(SIGTTOU, &osa_sigttou, NULL);
	}
	sig_check();
	return n;
}

/*
 * Return a file buffer all ready to read up the
 * passed message pointer.
 */
PUBLIC FILE *
setinput(const struct message *mp)
{

	(void)fflush(otf);
	if (fseek(itf, (long)positionof(mp->m_block, mp->m_offset), SEEK_SET) < 0)
		err(EXIT_FAILURE, "fseek");
	return itf;
}

/*
 * Delete a file, but only if the file is a plain file.
 */
PUBLIC int
rm(char *name)
{
	struct stat sb;

	if (stat(name, &sb) < 0)
		return -1;
	if (!S_ISREG(sb.st_mode)) {
		errno = EISDIR;
		return -1;
	}
	return unlink(name);
}

/*
 * Determine the size of the file possessed by
 * the passed buffer.
 */
PUBLIC off_t
fsize(FILE *iob)
{
	struct stat sbuf;

	if (fstat(fileno(iob), &sbuf) < 0)
		return 0;
	return sbuf.st_size;
}

/*
 * Determine the current folder directory name.
 */
PUBLIC int
getfold(char *name, size_t namesize)
{
	char unres[PATHSIZE], res[PATHSIZE];
	char *folder;

	if ((folder = value(ENAME_FOLDER)) == NULL)
		return -1;
	if (*folder != '/') {
		(void)snprintf(unres, sizeof(unres), "%s/%s", homedir, folder);
		folder = unres;
	}
	if (realpath(folder, res) == NULL)
		warn("Can't canonicalize folder `%s'", folder);
	else
		folder = res;
	(void)strlcpy(name, folder, namesize);
	return 0;
}

/*
 * Evaluate the string given as a new mailbox name.
 * Supported meta characters:
 *	%	for my system mail box
 *	%user	for user's system mail box
 *	#	for previous file
 *	&	invoker's mbox file
 *	+file	file in folder directory
 *	any shell meta character
 * Return the file name as a dynamic string.
 */
PUBLIC const char *
expand(const char *name)
{
	char xname[PATHSIZE];
	char cmdbuf[PATHSIZE];
	int e;
	wordexp_t we; 
	sigset_t nset, oset;

	/*
	 * The order of evaluation is "%" and "#" expand into constants.
	 * "&" can expand into "+".  "+" can expand into shell meta characters.
	 * Shell meta characters expand into constants.
	 * This way, we make no recursive expansion.
	 */
	switch (*name) {
	case '%':
		findmail(name[1] ? name + 1 : myname, xname, sizeof(xname));
		return savestr(xname);
	case '#':
		if (name[1] != 0)
			break;
		if (prevfile[0] == 0) {
			warnx("No previous file");
			return NULL;
		}
		return savestr(prevfile);
	case '&':
		if (name[1] == 0 && (name = value(ENAME_MBOX)) == NULL)
			name = "~/mbox";
		/* fall through */
	}
	if (name[0] == '+' && getfold(cmdbuf, sizeof(cmdbuf)) >= 0) {
		(void)snprintf(xname, sizeof(xname), "%s/%s", cmdbuf, name + 1);
		name = savestr(xname);
	}
	/* catch the most common shell meta character */
	if (name[0] == '~' && (name[1] == '/' || name[1] == '\0')) {
		(void)snprintf(xname, sizeof(xname), "%s%s", homedir, name + 1);
		name = savestr(xname);
	}
	if (strpbrk(name, "~{[*?$`'\"\\") == NULL)
		return name;

	*xname = '\0';

	sigemptyset(&nset);
	sigaddset(&nset, SIGCHLD);
	sigprocmask(SIG_BLOCK, &nset, &oset);
	e = wordexp(name, &we, WRDE_NOCMD);
	sigprocmask(SIG_SETMASK, &oset, NULL);

	switch (e) {
	case 0: /* OK */
		break;
	case WRDE_NOSPACE:
		warnx("Out of memory expanding `%s'", name);
		return NULL;
	case WRDE_BADVAL:
	case WRDE_BADCHAR:
	case WRDE_SYNTAX:
		warnx("Syntax error expanding `%s'", name);
		return NULL;
	case WRDE_CMDSUB:
		warnx("Command substitution not allowed expanding `%s'",
		    name);
		return NULL;
	default:
		warnx("Unknown expansion error %d expanding `%s'", e, name);
		return NULL;
	}

	switch (we.we_wordc) {
	case 0:
		warnx("No match for `%s'", name);
		break;
	case 1:
		if (strlen(we.we_wordv[0]) >= PATHSIZE)
			warnx("Expansion too long for `%s'", name);
		strlcpy(xname, we.we_wordv[0], PATHSIZE);
		break;
	default:
		warnx("Ambiguous expansion for `%s'", name);
		break;
	}

	wordfree(&we);
	if (!*xname)
		return NULL;
	else
		return savestr(xname);
}

/*
 * Return the name of the dead.letter file.
 */
PUBLIC const char *
getdeadletter(void)
{
	const char *cp;

	if ((cp = value(ENAME_DEAD)) == NULL || (cp = expand(cp)) == NULL)
		cp = expand("~/dead.letter");
	else if (*cp != '/') {
		char buf[PATHSIZE];
		(void)snprintf(buf, sizeof(buf), "~/%s", cp);
		cp = expand(buf);
	}
	return cp;
}
