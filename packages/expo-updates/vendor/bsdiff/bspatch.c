/*-
 * Copyright 2003-2005 Colin Percival
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted providing that the following conditions 
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#if 0
__FBSDID("$FreeBSD: src/usr.bin/bsdiff/bspatch/bspatch.c,v 1.1 2005/08/06 01:59:06 cperciva Exp $");
#endif

#include <bzlib.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

struct bspatch_context {
	FILE *cpf, *dpf, *epf;
	BZFILE *cpfbz2, *dpfbz2, *epfbz2;
	u_char *old, *new;
	int cbz2err, dbz2err, ebz2err;
};

static void cleanup_context(struct bspatch_context *ctx) {
	if (ctx->cpfbz2) BZ2_bzReadClose(&ctx->cbz2err, ctx->cpfbz2);
	if (ctx->dpfbz2) BZ2_bzReadClose(&ctx->dbz2err, ctx->dpfbz2);
	if (ctx->epfbz2) BZ2_bzReadClose(&ctx->ebz2err, ctx->epfbz2);
	if (ctx->cpf) fclose(ctx->cpf);
	if (ctx->dpf) fclose(ctx->dpf);
	if (ctx->epf) fclose(ctx->epf);
	if (ctx->old) free(ctx->old);
	if (ctx->new) free(ctx->new);
}

static off_t offtin(u_char *buf)
{
	off_t y;

	y=buf[7]&0x7F;
	y=y*256;y+=buf[6];
	y=y*256;y+=buf[5];
	y=y*256;y+=buf[4];
	y=y*256;y+=buf[3];
	y=y*256;y+=buf[2];
	y=y*256;y+=buf[1];
	y=y*256;y+=buf[0];

	if(buf[7]&0x80) y=-y;

	return y;
}

int bspatch_main(int argc, char * argv[])
{
	FILE * f;
	struct bspatch_context ctx = {0};
	int fd;
	ssize_t oldsize,newsize;
	ssize_t bzctrllen,bzdatalen;
	u_char header[32],buf[8];
	off_t oldpos,newpos;
	off_t ctrl[3];
	off_t lenread;
	off_t i;

	if(argc!=4) return 1;

	/* Open patch file */
	if ((f = fopen(argv[3], "r")) == NULL)
		return 1;

	/*
	File format:
		0	8	"BSDIFF40"
		8	8	X
		16	8	Y
		24	8	sizeof(newfile)
		32	X	bzip2(control block)
		32+X	Y	bzip2(diff block)
		32+X+Y	???	bzip2(extra block)
	with control block a set of triples (x,y,z) meaning "add x bytes
	from oldfile to x bytes from the diff block; copy y bytes from the
	extra block; seek forwards in oldfile by z bytes".
	*/

	/* Read header */
	if (fread(header, 1, 32, f) < 32) {
		fclose(f);
		return 1;
	}

	/* Check for appropriate magic */
	if (memcmp(header, "BSDIFF40", 8) != 0) {
		fclose(f);
		return 1;
	}

	/* Read lengths from header */
	bzctrllen=offtin(header+8);
	bzdatalen=offtin(header+16);
	newsize=offtin(header+24);
	if((bzctrllen<0) || (bzdatalen<0) || (newsize<0)) {
		fclose(f);
		return 1;
	}

	/* Close patch file and re-open it via libbzip2 at the right places */
	if (fclose(f))
		return 1;
	if ((ctx.cpf = fopen(argv[3], "r")) == NULL)
		return 1;
	if (fseeko(ctx.cpf, 32, SEEK_SET)) {
		cleanup_context(&ctx);
		return 1;
	}
	if ((ctx.cpfbz2 = BZ2_bzReadOpen(&ctx.cbz2err, ctx.cpf, 0, 0, NULL, 0)) == NULL) {
		cleanup_context(&ctx);
		return 1;
	}
	if ((ctx.dpf = fopen(argv[3], "r")) == NULL) {
		cleanup_context(&ctx);
		return 1;
	}
	if (fseeko(ctx.dpf, 32 + bzctrllen, SEEK_SET)) {
		cleanup_context(&ctx);
		return 1;
	}
	if ((ctx.dpfbz2 = BZ2_bzReadOpen(&ctx.dbz2err, ctx.dpf, 0, 0, NULL, 0)) == NULL) {
		cleanup_context(&ctx);
		return 1;
	}
	if ((ctx.epf = fopen(argv[3], "r")) == NULL) {
		cleanup_context(&ctx);
		return 1;
	}
	if (fseeko(ctx.epf, 32 + bzctrllen + bzdatalen, SEEK_SET)) {
		cleanup_context(&ctx);
		return 1;
	}
	if ((ctx.epfbz2 = BZ2_bzReadOpen(&ctx.ebz2err, ctx.epf, 0, 0, NULL, 0)) == NULL) {
		cleanup_context(&ctx);
		return 1;
	}

	if(((fd=open(argv[1],O_RDONLY,0))<0) ||
		((oldsize=lseek(fd,0,SEEK_END))==-1) ||
		((ctx.old=malloc(oldsize+1))==NULL) ||
		(lseek(fd,0,SEEK_SET)!=0) ||
		(read(fd,ctx.old,oldsize)!=oldsize) ||
		(close(fd)==-1)) {
		cleanup_context(&ctx);
		return 1;
	}
	if((ctx.new=malloc(newsize+1))==NULL) {
		cleanup_context(&ctx);
		return 1;
	}

	oldpos=0;newpos=0;
	while(newpos<newsize) {
		/* Read control data */
		for(i=0;i<=2;i++) {
			lenread = BZ2_bzRead(&ctx.cbz2err, ctx.cpfbz2, buf, 8);
			if ((lenread < 8) || ((ctx.cbz2err != BZ_OK) &&
			    (ctx.cbz2err != BZ_STREAM_END))) {
				cleanup_context(&ctx);
				return 1;
			}
			ctrl[i]=offtin(buf);
		};

		/* Sanity-check */
		if(newpos+ctrl[0]>newsize) {
			cleanup_context(&ctx);
			return 1;
		}

		/* Read diff string */
		lenread = BZ2_bzRead(&ctx.dbz2err, ctx.dpfbz2, ctx.new + newpos, ctrl[0]);
		if ((lenread < ctrl[0]) ||
		    ((ctx.dbz2err != BZ_OK) && (ctx.dbz2err != BZ_STREAM_END))) {
			cleanup_context(&ctx);
			return 1;
		}

		/* Add old data to diff string */
		for(i=0;i<ctrl[0];i++)
			if((oldpos+i>=0) && (oldpos+i<oldsize))
				ctx.new[newpos+i]+=ctx.old[oldpos+i];

		/* Adjust pointers */
		newpos+=ctrl[0];
		oldpos+=ctrl[0];

		/* Sanity-check */
		if(newpos+ctrl[1]>newsize) {
			cleanup_context(&ctx);
			return 1;
		}

		/* Read extra string */
		lenread = BZ2_bzRead(&ctx.ebz2err, ctx.epfbz2, ctx.new + newpos, ctrl[1]);
		if ((lenread < ctrl[1]) ||
		    ((ctx.ebz2err != BZ_OK) && (ctx.ebz2err != BZ_STREAM_END))) {
			cleanup_context(&ctx);
			return 1;
		}

		/* Adjust pointers */
		newpos+=ctrl[1];
		oldpos+=ctrl[2];
	};

	/* Clean up the bzip2 reads */
	BZ2_bzReadClose(&ctx.cbz2err, ctx.cpfbz2);
	BZ2_bzReadClose(&ctx.dbz2err, ctx.dpfbz2);
	BZ2_bzReadClose(&ctx.ebz2err, ctx.epfbz2);
	ctx.cpfbz2 = NULL;
	ctx.dpfbz2 = NULL;
	ctx.epfbz2 = NULL;
	if (fclose(ctx.cpf) || fclose(ctx.dpf) || fclose(ctx.epf)) {
		cleanup_context(&ctx);
		return 1;
	}
	ctx.cpf = NULL;
	ctx.dpf = NULL;
	ctx.epf = NULL;

	/* Write the new file */
	if(((fd=open(argv[2],O_CREAT|O_TRUNC|O_WRONLY,0666))<0) ||
		(write(fd,ctx.new,newsize)!=newsize) || (close(fd)==-1)) {
		cleanup_context(&ctx);
		return 1;
	}

	free(ctx.new);
	free(ctx.old);

	return 0;
}
