/** \ingroup signature
 * \file lib/signature.c
 */

#include "system.h"

#include "rpmio_internal.h"
#include <rpmlib.h>
#include <rpmmacro.h>	/* XXX for rpmGetPath() */
#include "rpmdb.h"

#include "rpmts.h"

#include "misc.h"	/* XXX for dosetenv() and makeTempFile() */
#include "legacy.h"	/* XXX for mdbinfile() */
#include "rpmlead.h"
#include "signature.h"
#include "debug.h"

/*@access Header@*/		/* XXX compared with NULL */
/*@access FD_t@*/		/* XXX compared with NULL */
/*@access DIGEST_CTX@*/		/* XXX compared with NULL */
/*@access pgpDig@*/
/*@access pgpDigParams@*/

#if !defined(__GLIBC__)
char ** environ = NULL;
#endif

int rpmLookupSignatureType(int action)
{
    /*@unchecked@*/
    static int disabled = 0;
    int rc = 0;

    switch (action) {
    case RPMLOOKUPSIG_DISABLE:
	disabled = -2;
	break;
    case RPMLOOKUPSIG_ENABLE:
	disabled = 0;
	/*@fallthrough@*/
    case RPMLOOKUPSIG_QUERY:
	if (disabled)
	    break;	/* Disabled */
/*@-boundsread@*/
      { const char *name = rpmExpand("%{?_signature}", NULL);
	if (!(name && *name != '\0'))
	    rc = 0;
	else if (!xstrcasecmp(name, "none"))
	    rc = 0;
	else if (!xstrcasecmp(name, "pgp"))
	    rc = RPMSIGTAG_PGP;
	else if (!xstrcasecmp(name, "pgp5"))	/* XXX legacy */
	    rc = RPMSIGTAG_PGP;
	else if (!xstrcasecmp(name, "gpg"))
	    rc = RPMSIGTAG_GPG;
	else
	    rc = -1;	/* Invalid %_signature spec in macro file */
	name = _free(name);
      }	break;
/*@=boundsread@*/
    }
    return rc;
}

/* rpmDetectPGPVersion() returns the absolute path to the "pgp"  */
/* executable of the requested version, or NULL when none found. */

const char * rpmDetectPGPVersion(pgpVersion * pgpVer)
{
    /* Actually this should support having more then one pgp version. */
    /* At the moment only one version is possible since we only       */
    /* have one %_pgpbin and one %_pgp_path.                          */

    static pgpVersion saved_pgp_version = PGP_UNKNOWN;
    const char *pgpbin = rpmGetPath("%{?_pgpbin}", NULL);

    if (saved_pgp_version == PGP_UNKNOWN) {
	char *pgpvbin;
	struct stat st;

/*@-boundsread@*/
	if (!(pgpbin && pgpbin[0] != '\0')) {
	    pgpbin = _free(pgpbin);
	    saved_pgp_version = -1;
	    return NULL;
	}
/*@=boundsread@*/
/*@-boundswrite@*/
	pgpvbin = (char *)alloca(strlen(pgpbin) + sizeof("v"));
	(void)stpcpy(stpcpy(pgpvbin, pgpbin), "v");
/*@=boundswrite@*/

	if (stat(pgpvbin, &st) == 0)
	    saved_pgp_version = PGP_5;
	else if (stat(pgpbin, &st) == 0)
	    saved_pgp_version = PGP_2;
	else
	    saved_pgp_version = PGP_NOTDETECTED;
    }

/*@-boundswrite@*/
    if (pgpVer && pgpbin)
	*pgpVer = saved_pgp_version;
/*@=boundswrite@*/
    return pgpbin;
}

/**
 * Check package size.
 * @todo rpmio: use fdSize rather than fstat(2) to get file size.
 * @param fd			package file handle
 * @param siglen		signature header size
 * @param pad			signature padding
 * @param datalen		length of header+payload
 * @return 			rpmRC return code
 */
static inline rpmRC checkSize(FD_t fd, int siglen, int pad, int datalen)
	/*@globals fileSystem @*/
	/*@modifies fileSystem @*/
{
    struct stat st;
    int delta;
    rpmRC rc;

    if (fstat(Fileno(fd), &st))
	return RPMRC_FAIL;

    if (!S_ISREG(st.st_mode)) {
	rpmMessage(RPMMESS_DEBUG,
	    _("file is not regular -- skipping size check\n"));
	return RPMRC_OK;
    }

    /*@-sizeoftype@*/
    delta = (sizeof(struct rpmlead) + siglen + pad + datalen) - st.st_size;
    switch (delta) {
    case -32: /* XXX rpm-4.0 packages */
    case 32:  /* XXX Legacy headers have a HEADER_IMAGE tag added. */
    case 0:
	rc = RPMRC_OK;
	break;
    default:
	rc = RPMRC_BADSIZE;
	break;
    }

    rpmMessage((rc == RPMRC_OK ? RPMMESS_DEBUG : RPMMESS_WARNING),
	_("Expected size: %12d = lead(%d)+sigs(%d)+pad(%d)+data(%d)\n"),
		(int)sizeof(struct rpmlead)+siglen+pad+datalen,
		(int)sizeof(struct rpmlead), siglen, pad, datalen);
    /*@=sizeoftype@*/
    rpmMessage((rc == RPMRC_OK ? RPMMESS_DEBUG : RPMMESS_WARNING),
	_("  Actual size: %12d\n"), (int)st.st_size);

    return rc;
}

rpmRC rpmReadSignature(FD_t fd, Header * headerp, sigType sig_type)
{
    byte buf[2048];
    int sigSize, pad;
    int_32 type, count;
    int_32 *archSize;
    Header h = NULL;
    rpmRC rc = RPMRC_FAIL;		/* assume failure */

/*@-boundswrite@*/
    if (headerp)
	*headerp = NULL;
/*@=boundswrite@*/

    buf[0] = 0;
    switch (sig_type) {
    case RPMSIGTYPE_NONE:
	rpmMessage(RPMMESS_DEBUG, _("No signature\n"));
	rc = RPMRC_OK;
	break;
    case RPMSIGTYPE_PGP262_1024:
	rpmMessage(RPMMESS_DEBUG, _("Old PGP signature\n"));
	/* These are always 256 bytes */
	if (timedRead(fd, buf, 256) != 256)
	    break;
	h = headerNew();
	(void) headerAddEntry(h, RPMSIGTAG_PGP, RPM_BIN_TYPE, buf, 152);
	rc = RPMRC_OK;
	break;
    case RPMSIGTYPE_MD5:
    case RPMSIGTYPE_MD5_PGP:
	rpmError(RPMERR_BADSIGTYPE,
	      _("Old (internal-only) signature!  How did you get that!?\n"));
	break;
    case RPMSIGTYPE_HEADERSIG:
    case RPMSIGTYPE_DISABLE:
	/* This is a new style signature */
	h = headerRead(fd, HEADER_MAGIC_YES);
	if (h == NULL)
	    break;

	rc = RPMRC_OK;
	sigSize = headerSizeof(h, HEADER_MAGIC_YES);

	pad = (8 - (sigSize % 8)) % 8; /* 8-byte pad */
	if (sig_type == RPMSIGTYPE_HEADERSIG) {
	    if (! headerGetEntry(h, RPMSIGTAG_SIZE, &type,
				(void **)&archSize, &count))
		break;
/*@-boundsread@*/
	    rc = checkSize(fd, sigSize, pad, *archSize);
/*@=boundsread@*/
	}
	if (pad && timedRead(fd, buf, pad) != pad)
	    rc = RPMRC_SHORTREAD;
	break;
    default:
	break;
    }

/*@-boundswrite@*/
    if (headerp && rc == RPMRC_OK)
	*headerp = headerLink(h);
/*@=boundswrite@*/

    h = headerFree(h);

    return rc;
}

int rpmWriteSignature(FD_t fd, Header h)
{
    static byte buf[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    int sigSize, pad;
    int rc;

    rc = headerWrite(fd, h, HEADER_MAGIC_YES);
    if (rc)
	return rc;

    sigSize = headerSizeof(h, HEADER_MAGIC_YES);
    pad = (8 - (sigSize % 8)) % 8;
    if (pad) {
/*@-boundswrite@*/
	if (Fwrite(buf, sizeof(buf[0]), pad, fd) != pad)
	    rc = 1;
/*@=boundswrite@*/
    }
    rpmMessage(RPMMESS_DEBUG, _("Signature: size(%d)+pad(%d)\n"), sigSize, pad);
    return rc;
}

Header rpmNewSignature(void)
{
    Header h = headerNew();
    return h;
}

Header rpmFreeSignature(Header h)
{
    return headerFree(h);
}

/**
 * Generate PGP (aka RSA/MD5) signature(s) for a header+payload file.
 * @param file		header+payload file name
 * @retval pkt		signature packet(s)
 * @retval pktlen	signature packet(s) length
 * @param passPhrase	private key pass phrase
 * @return		0 on success, 1 on failure
 */
static int makePGPSignature(const char * file, /*@out@*/ byte ** pkt,
		/*@out@*/ int_32 * pktlen, /*@null@*/ const char * passPhrase)
	/*@globals errno, rpmGlobalMacroContext,
		fileSystem, internalState @*/
	/*@modifies errno, *pkt, *pktlen, rpmGlobalMacroContext,
		fileSystem, internalState @*/
{
    char * sigfile = alloca(1024);
    int pid, status;
    int inpipe[2];
    struct stat st;
    const char * cmd;
    char *const *av;
    int rc;

/*@-boundswrite@*/
    (void) stpcpy( stpcpy(sigfile, file), ".sig");
/*@=boundswrite@*/

    addMacro(NULL, "__plaintext_filename", NULL, file, -1);
    addMacro(NULL, "__signature_filename", NULL, sigfile, -1);

    inpipe[0] = inpipe[1] = 0;
/*@-boundsread@*/
    (void) pipe(inpipe);
/*@=boundsread@*/

    if (!(pid = fork())) {
	const char *pgp_path = rpmExpand("%{?_pgp_path}", NULL);
	const char *path;
	pgpVersion pgpVer;

	(void) close(STDIN_FILENO);
	(void) dup2(inpipe[0], 3);
	(void) close(inpipe[1]);

	(void) dosetenv("PGPPASSFD", "3", 1);
/*@-boundsread@*/
	if (pgp_path && *pgp_path != '\0')
	    (void) dosetenv("PGPPATH", pgp_path, 1);
/*@=boundsread@*/

	/* dosetenv("PGPPASS", passPhrase, 1); */

	if ((path = rpmDetectPGPVersion(&pgpVer)) != NULL) {
	    switch(pgpVer) {
	    case PGP_2:
		cmd = rpmExpand("%{?__pgp_sign_cmd}", NULL);
		rc = poptParseArgvString(cmd, NULL, (const char ***)&av);
/*@-boundsread@*/
		if (!rc)
		    rc = execve(av[0], av+1, environ);
/*@=boundsread@*/
		break;
	    case PGP_5:
		cmd = rpmExpand("%{?__pgp5_sign_cmd}", NULL);
		rc = poptParseArgvString(cmd, NULL, (const char ***)&av);
/*@-boundsread@*/
		if (!rc)
		    rc = execve(av[0], av+1, environ);
/*@=boundsread@*/
		break;
	    case PGP_UNKNOWN:
	    case PGP_NOTDETECTED:
		errno = ENOENT;
		break;
	    }
	}
	rpmError(RPMERR_EXEC, _("Could not exec %s: %s\n"), "pgp",
			strerror(errno));
	_exit(RPMERR_EXEC);
    }

    delMacro(NULL, "__plaintext_filename");
    delMacro(NULL, "__signature_filename");

    (void) close(inpipe[0]);
    if (passPhrase)
	(void) write(inpipe[1], passPhrase, strlen(passPhrase));
    (void) write(inpipe[1], "\n", 1);
    (void) close(inpipe[1]);

    (void)waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
	rpmError(RPMERR_SIGGEN, _("pgp failed\n"));
	return 1;
    }

    if (stat(sigfile, &st)) {
	/* PGP failed to write signature */
	if (sigfile) (void) unlink(sigfile);  /* Just in case */
	rpmError(RPMERR_SIGGEN, _("pgp failed to write signature\n"));
	return 1;
    }

/*@-boundswrite@*/
    *pktlen = st.st_size;
    rpmMessage(RPMMESS_DEBUG, _("PGP sig size: %d\n"), *pktlen);
    *pkt = xmalloc(*pktlen);
/*@=boundswrite@*/

/*@-boundsread@*/
    {	FD_t fd;

	rc = 0;
	fd = Fopen(sigfile, "r.fdio");
	if (fd != NULL && !Ferror(fd)) {
	    rc = timedRead(fd, *pkt, *pktlen);
	    if (sigfile) (void) unlink(sigfile);
	    (void) Fclose(fd);
	}
	if (rc != *pktlen) {
/*@-boundswrite@*/
	    *pkt = _free(*pkt);
/*@=boundswrite@*/
	    rpmError(RPMERR_SIGGEN, _("unable to read the signature\n"));
	    return 1;
	}
    }

    rpmMessage(RPMMESS_DEBUG, _("Got %d bytes of PGP sig\n"), *pktlen);
/*@=boundsread@*/

    return 0;
}

/**
 * Generate GPG (aka DSA) signature(s) for a header+payload file.
 * @param file		header+payload file name
 * @retval pkt		signature packet(s)
 * @retval pktlen	signature packet(s) length
 * @param passPhrase	private key pass phrase
 * @return		0 on success, 1 on failure
 */
static int makeGPGSignature(const char * file, /*@out@*/ byte ** pkt,
		/*@out@*/ int_32 * pktlen, /*@null@*/ const char * passPhrase)
	/*@globals rpmGlobalMacroContext,
		fileSystem, internalState @*/
	/*@modifies *pkt, *pktlen, rpmGlobalMacroContext,
		fileSystem, internalState @*/
{
    char * sigfile = alloca(1024);
    int pid, status;
    int inpipe[2];
    FILE * fpipe;
    struct stat st;
    const char * cmd;
    char *const *av;
    int rc;

/*@-boundswrite@*/
    (void) stpcpy( stpcpy(sigfile, file), ".sig");
/*@=boundswrite@*/

    addMacro(NULL, "__plaintext_filename", NULL, file, -1);
    addMacro(NULL, "__signature_filename", NULL, sigfile, -1);

    inpipe[0] = inpipe[1] = 0;
/*@-boundsread@*/
    (void) pipe(inpipe);
/*@=boundsread@*/

    if (!(pid = fork())) {
	const char *gpg_path = rpmExpand("%{?_gpg_path}", NULL);

	(void) close(STDIN_FILENO);
	(void) dup2(inpipe[0], 3);
	(void) close(inpipe[1]);

/*@-boundsread@*/
	if (gpg_path && *gpg_path != '\0')
	    (void) dosetenv("GNUPGHOME", gpg_path, 1);
/*@=boundsread@*/

	cmd = rpmExpand("%{?__gpg_sign_cmd}", NULL);
	rc = poptParseArgvString(cmd, NULL, (const char ***)&av);
/*@-boundsread@*/
	if (!rc)
	    rc = execve(av[0], av+1, environ);
/*@=boundsread@*/

	rpmError(RPMERR_EXEC, _("Could not exec %s: %s\n"), "gpg",
			strerror(errno));
	_exit(RPMERR_EXEC);
    }

    delMacro(NULL, "__plaintext_filename");
    delMacro(NULL, "__signature_filename");

    fpipe = fdopen(inpipe[1], "w");
    (void) close(inpipe[0]);
    if (fpipe) {
	fprintf(fpipe, "%s\n", (passPhrase ? passPhrase : ""));
	(void) fclose(fpipe);
    }

    (void) waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status)) {
	rpmError(RPMERR_SIGGEN, _("gpg failed\n"));
	return 1;
    }

    if (stat(sigfile, &st)) {
	/* GPG failed to write signature */
	if (sigfile) (void) unlink(sigfile);  /* Just in case */
	rpmError(RPMERR_SIGGEN, _("gpg failed to write signature\n"));
	return 1;
    }

/*@-boundswrite@*/
    *pktlen = st.st_size;
    rpmMessage(RPMMESS_DEBUG, _("GPG sig size: %d\n"), *pktlen);
    *pkt = xmalloc(*pktlen);
/*@=boundswrite@*/

/*@-boundsread@*/
    {	FD_t fd;

	rc = 0;
	fd = Fopen(sigfile, "r.fdio");
	if (fd != NULL && !Ferror(fd)) {
	    rc = timedRead(fd, *pkt, *pktlen);
	    if (sigfile) (void) unlink(sigfile);
	    (void) Fclose(fd);
	}
	if (rc != *pktlen) {
/*@-boundswrite@*/
	    *pkt = _free(*pkt);
/*@=boundswrite@*/
	    rpmError(RPMERR_SIGGEN, _("unable to read the signature\n"));
	    return 1;
	}
    }

    rpmMessage(RPMMESS_DEBUG, _("Got %d bytes of GPG sig\n"), *pktlen);
/*@=boundsread@*/

    return 0;
}

/*@unchecked@*/
static unsigned char header_magic[8] = {
	0x8e, 0xad, 0xe8, 0x01, 0x00, 0x00, 0x00, 0x00
};

/**
 * Generate header only signature(s) from a header+payload file.
 * @param sig		signature header
 * @param file		header+payload file name
 * @param sigTag	type of signature(s) to add
 * @param passPhrase	private key pass phrase
 * @return		0 on success, -1 on failure
 */
static int makeHDRSignature(Header sig, const char * file, int_32 sigTag,
		/*@null@*/ const char * passPhrase)
	/*@globals rpmGlobalMacroContext, fileSystem, internalState @*/
	/*@modifies sig, rpmGlobalMacroContext, fileSystem, internalState @*/
{
    Header h = NULL;
    FD_t fd = NULL;
    byte * pkt;
    int_32 pktlen;
    const char * fn = NULL;
    const char * SHA1 = NULL;
    int ret = -1;	/* assume failure. */

    switch (sigTag) {
    case RPMSIGTAG_SIZE:
    case RPMSIGTAG_MD5:
    case RPMSIGTAG_PGP5:	/* XXX legacy */
    case RPMSIGTAG_PGP:
    case RPMSIGTAG_GPG:
	goto exit;
	/*@notreached@*/ break;
    case RPMSIGTAG_SHA1:
	fd = Fopen(file, "r.fdio");
	if (fd == NULL || Ferror(fd))
	    goto exit;
	h = headerRead(fd, HEADER_MAGIC_YES);
	if (h == NULL)
	    goto exit;
	(void) Fclose(fd);	fd = NULL;

	if (headerIsEntry(h, RPMTAG_HEADERIMMUTABLE)) {
	    DIGEST_CTX ctx;
	    void * uh;
	    int_32 uht, uhc;
	
	    if (!headerGetEntry(h, RPMTAG_HEADERIMMUTABLE, &uht, &uh, &uhc)
	     ||  uh == NULL)
	    {
		h = headerFree(h);
		goto exit;
	    }
	    ctx = rpmDigestInit(PGPHASHALGO_SHA1, RPMDIGEST_NONE);
	    (void) rpmDigestUpdate(ctx, header_magic, sizeof(header_magic));
	    (void) rpmDigestUpdate(ctx, uh, uhc);
	    (void) rpmDigestFinal(ctx, (void **)&SHA1, NULL, 1);
	    uh = headerFreeData(uh, uht);
	}
	h = headerFree(h);

	if (SHA1 == NULL)
	    goto exit;
	if (!headerAddEntry(sig, RPMSIGTAG_SHA1, RPM_STRING_TYPE, SHA1, 1))
	    goto exit;
	ret = 0;
	break;
    case RPMSIGTAG_DSA:
	fd = Fopen(file, "r.fdio");
	if (fd == NULL || Ferror(fd))
	    goto exit;
	h = headerRead(fd, HEADER_MAGIC_YES);
	if (h == NULL)
	    goto exit;
	(void) Fclose(fd);	fd = NULL;
	if (makeTempFile(NULL, &fn, &fd))
	    goto exit;
	if (headerWrite(fd, h, HEADER_MAGIC_YES))
	    goto exit;
	(void) Fclose(fd);	fd = NULL;
	if (makeGPGSignature(fn, &pkt, &pktlen, passPhrase)
	 || !headerAddEntry(sig, sigTag, RPM_BIN_TYPE, pkt, pktlen))
	    goto exit;
	ret = 0;
	break;
    case RPMSIGTAG_RSA:
	fd = Fopen(file, "r.fdio");
	if (fd == NULL || Ferror(fd))
	    goto exit;
	h = headerRead(fd, HEADER_MAGIC_YES);
	if (h == NULL)
	    goto exit;
	(void) Fclose(fd);	fd = NULL;
	if (makeTempFile(NULL, &fn, &fd))
	    goto exit;
	if (headerWrite(fd, h, HEADER_MAGIC_YES))
	    goto exit;
	(void) Fclose(fd);	fd = NULL;
	if (makePGPSignature(fn, &pkt, &pktlen, passPhrase)
	 || !headerAddEntry(sig, sigTag, RPM_BIN_TYPE, pkt, pktlen))
	    goto exit;
	ret = 0;
	break;
    }

exit:
    if (fn) {
	(void) unlink(fn);
	fn = _free(fn);
    }
    SHA1 = _free(SHA1);
    h = headerFree(h);
    if (fd) (void) Fclose(fd);
    return ret;
}

int rpmAddSignature(Header sig, const char * file, int_32 sigTag,
		const char * passPhrase)
{
    struct stat st;
    byte * pkt;
    int_32 pktlen;
    int ret = -1;	/* assume failure. */

    switch (sigTag) {
    case RPMSIGTAG_SIZE:
	if (stat(file, &st) != 0)
	    break;
	pktlen = st.st_size;
	if (!headerAddEntry(sig, sigTag, RPM_INT32_TYPE, &pktlen, 1))
	    break;
	ret = 0;
	break;
    case RPMSIGTAG_MD5:
	pktlen = 16;
	pkt = xcalloc(1, pktlen);
	if (domd5(file, pkt, 0, NULL)
	 || !headerAddEntry(sig, sigTag, RPM_BIN_TYPE, pkt, pktlen))
	    break;
	ret = 0;
	break;
    case RPMSIGTAG_PGP5:	/* XXX legacy */
    case RPMSIGTAG_PGP:
	if (makePGPSignature(file, &pkt, &pktlen, passPhrase)
	 || !headerAddEntry(sig, sigTag, RPM_BIN_TYPE, pkt, pktlen))
	    break;
#ifdef	NOTYET	/* XXX needs hdrmd5ctx, like hdrsha1ctx. */
	/* XXX Piggyback a header-only RSA signature as well. */
	ret = makeHDRSignature(sig, file, RPMSIGTAG_RSA, passPhrase);
#endif
	ret = 0;
	break;
    case RPMSIGTAG_GPG:
	if (makeGPGSignature(file, &pkt, &pktlen, passPhrase)
	 || !headerAddEntry(sig, sigTag, RPM_BIN_TYPE, pkt, pktlen))
	    break;
	/* XXX Piggyback a header-only DSA signature as well. */
	ret = makeHDRSignature(sig, file, RPMSIGTAG_DSA, passPhrase);
	break;
    case RPMSIGTAG_RSA:
    case RPMSIGTAG_DSA:
    case RPMSIGTAG_SHA1:
	ret = makeHDRSignature(sig, file, sigTag, passPhrase);
	break;
    }

    return ret;
}

static int checkPassPhrase(const char * passPhrase, const int sigTag)
	/*@globals rpmGlobalMacroContext, fileSystem, internalState @*/
	/*@modifies rpmGlobalMacroContext, fileSystem, internalState @*/
{
    int passPhrasePipe[2];
    int pid, status;
    int rc;
    int xx;

    passPhrasePipe[0] = passPhrasePipe[1] = 0;
/*@-boundsread@*/
    xx = pipe(passPhrasePipe);
/*@=boundsread@*/
    if (!(pid = fork())) {
	const char * cmd;
	char *const *av;
	int fdno;

	xx = close(STDIN_FILENO);
	xx = close(STDOUT_FILENO);
	xx = close(passPhrasePipe[1]);
	if (! rpmIsVerbose())
	    xx = close(STDERR_FILENO);
	if ((fdno = open("/dev/null", O_RDONLY)) != STDIN_FILENO) {
	    xx = dup2(fdno, STDIN_FILENO);
	    xx = close(fdno);
	}
	if ((fdno = open("/dev/null", O_WRONLY)) != STDOUT_FILENO) {
	    xx = dup2(fdno, STDOUT_FILENO);
	    xx = close(fdno);
	}
	xx = dup2(passPhrasePipe[0], 3);

	switch (sigTag) {
	case RPMSIGTAG_DSA:
	case RPMSIGTAG_GPG:
	{   const char *gpg_path = rpmExpand("%{?_gpg_path}", NULL);

/*@-boundsread@*/
	    if (gpg_path && *gpg_path != '\0')
  		(void) dosetenv("GNUPGHOME", gpg_path, 1);
/*@=boundsread@*/

	    cmd = rpmExpand("%{?__gpg_check_password_cmd}", NULL);
	    rc = poptParseArgvString(cmd, NULL, (const char ***)&av);
/*@-boundsread@*/
	    if (!rc)
		rc = execve(av[0], av+1, environ);
/*@=boundsread@*/

	    rpmError(RPMERR_EXEC, _("Could not exec %s: %s\n"), "gpg",
			strerror(errno));
	}   /*@notreached@*/ break;
	case RPMSIGTAG_RSA:
	case RPMSIGTAG_PGP5:	/* XXX legacy */
	case RPMSIGTAG_PGP:
	{   const char *pgp_path = rpmExpand("%{?_pgp_path}", NULL);
	    const char *path;
	    pgpVersion pgpVer;

	    (void) dosetenv("PGPPASSFD", "3", 1);
/*@-boundsread@*/
	    if (pgp_path && *pgp_path != '\0')
		xx = dosetenv("PGPPATH", pgp_path, 1);
/*@=boundsread@*/

	    if ((path = rpmDetectPGPVersion(&pgpVer)) != NULL) {
		switch(pgpVer) {
		case PGP_2:
		    cmd = rpmExpand("%{?__pgp_check_password_cmd}", NULL);
		    rc = poptParseArgvString(cmd, NULL, (const char ***)&av);
/*@-boundsread@*/
		    if (!rc)
			rc = execve(av[0], av+1, environ);
/*@=boundsread@*/
		    /*@innerbreak@*/ break;
		case PGP_5:	/* XXX legacy */
		    cmd = rpmExpand("%{?__pgp5_check_password_cmd}", NULL);
		    rc = poptParseArgvString(cmd, NULL, (const char ***)&av);
/*@-boundsread@*/
		    if (!rc)
			rc = execve(av[0], av+1, environ);
/*@=boundsread@*/
		    /*@innerbreak@*/ break;
		case PGP_UNKNOWN:
		case PGP_NOTDETECTED:
		    /*@innerbreak@*/ break;
		}
	    }
	    rpmError(RPMERR_EXEC, _("Could not exec %s: %s\n"), "pgp",
			strerror(errno));
	    _exit(RPMERR_EXEC);
	}   /*@notreached@*/ break;
	default: /* This case should have been screened out long ago. */
	    rpmError(RPMERR_SIGGEN, _("Invalid %%_signature spec in macro file\n"));
	    _exit(RPMERR_SIGGEN);
	    /*@notreached@*/ break;
	}
    }

    xx = close(passPhrasePipe[0]);
    xx = write(passPhrasePipe[1], passPhrase, strlen(passPhrase));
    xx = write(passPhrasePipe[1], "\n", 1);
    xx = close(passPhrasePipe[1]);

    (void) waitpid(pid, &status, 0);

    return ((!WIFEXITED(status) || WEXITSTATUS(status)) ? 1 : 0);
}

char * rpmGetPassPhrase(const char * prompt, const int sigTag)
{
    char *pass;
    int aok;

    switch (sigTag) {
    case RPMSIGTAG_DSA:
    case RPMSIGTAG_GPG:
/*@-boundsread@*/
      { const char *name = rpmExpand("%{?_gpg_name}", NULL);
	aok = (name && *name != '\0');
	name = _free(name);
      }
/*@=boundsread@*/
	if (!aok) {
	    rpmError(RPMERR_SIGGEN,
		_("You must set \"%%_gpg_name\" in your macro file\n"));
	    return NULL;
	}
	break;
    case RPMSIGTAG_RSA:
    case RPMSIGTAG_PGP5: 	/* XXX legacy */
    case RPMSIGTAG_PGP:
/*@-boundsread@*/
      { const char *name = rpmExpand("%{?_pgp_name}", NULL);
	aok = (name && *name != '\0');
	name = _free(name);
      }
/*@=boundsread@*/
	if (!aok) {
	    rpmError(RPMERR_SIGGEN,
		_("You must set \"%%_pgp_name\" in your macro file\n"));
	    return NULL;
	}
	break;
    default:
	/* Currently the calling function (rpm.c:main) is checking this and
	 * doing a better job.  This section should never be accessed.
	 */
	rpmError(RPMERR_SIGGEN, _("Invalid %%_signature spec in macro file\n"));
	return NULL;
	/*@notreached@*/ break;
    }

/*@-moduncon -nullpass @*/
    pass = /*@-unrecog@*/ getpass( (prompt ? prompt : "") ) /*@=unrecog@*/ ;
/*@=moduncon -nullpass @*/

    if (checkPassPhrase(pass, sigTag))
	return NULL;

    return pass;
}

static /*@observer@*/ const char * rpmSigString(rpmVerifySignatureReturn res)
	/*@*/
{
    const char * str;
    switch (res) {
    case RPMSIG_OK:		str = "OK";		break;
    case RPMSIG_BAD:		str = "BAD";		break;
    case RPMSIG_NOKEY:		str = "NOKEY";		break;
    case RPMSIG_NOTTRUSTED:	str = "NOTRUSTED";	break;
    default:
    case RPMSIG_UNKNOWN: 	str = "UNKNOWN";	break;
    }
    return str;
}

/*@-boundswrite@*/
static rpmVerifySignatureReturn
verifySizeSignature(const rpmts ts, /*@out@*/ char * t)
	/*@modifies *t @*/
{
    const void * sig = rpmtsSig(ts);
    pgpDig dig = rpmtsDig(ts);
    rpmVerifySignatureReturn res;
    int_32 size = 0x7fffffff;

    *t = '\0';
    t = stpcpy(t, _("Header+Payload size: "));

    if (sig == NULL || dig == NULL || dig->nbytes == 0) {
	res = RPMSIG_NOKEY;		/* XXX RPMSIG_ARGS */
	res = RPMSIG_NOKEY;
	t = stpcpy(t, rpmSigString(res));
	goto exit;
    }

    memcpy(&size, sig, sizeof(size));

    if (size != dig->nbytes) {
	res = RPMSIG_BAD;
	t = stpcpy(t, rpmSigString(res));
	sprintf(t, " Expected(%d) != (%d)\n", size, dig->nbytes);
    } else {
	res = RPMSIG_OK;
	t = stpcpy(t, rpmSigString(res));
	sprintf(t, " (%d)", dig->nbytes);
    }

exit:
    t = stpcpy(t, "\n");
    return res;
}
/*@=boundswrite@*/

/*@-boundswrite@*/
static rpmVerifySignatureReturn
verifyMD5Signature(const rpmts ts, /*@out@*/ char * t,
		/*@null@*/ DIGEST_CTX md5ctx)
	/*@modifies *t @*/
{
    const void * sig = rpmtsSig(ts);
    int_32 siglen = rpmtsSiglen(ts);
    pgpDig dig = rpmtsDig(ts);
    rpmVerifySignatureReturn res;
    byte * md5sum = NULL;
    size_t md5len = 0;

    *t = '\0';
    t = stpcpy(t, _("MD5 digest: "));

    if (md5ctx == NULL || sig == NULL || dig == NULL) {
	res = RPMSIG_NOKEY;		/* XXX RPMSIG_ARGS */
	t = stpcpy(t, rpmSigString(res));
	goto exit;
    }

    (void) rpmDigestFinal(rpmDigestDup(md5ctx),
		(void **)&md5sum, &md5len, 0);

    if (md5len != siglen || memcmp(md5sum, sig, md5len)) {
	res = RPMSIG_BAD;
	t = stpcpy(t, rpmSigString(res));
	t = stpcpy(t, " Expected(");
	(void) pgpHexCvt(t, sig, siglen);
	t += strlen(t);
	t = stpcpy(t, ") != (");
    } else {
	res = RPMSIG_OK;
	t = stpcpy(t, rpmSigString(res));
	t = stpcpy(t, " (");
    }
    (void) pgpHexCvt(t, md5sum, md5len);
    t += strlen(t);
    t = stpcpy(t, ")");

exit:
    md5sum = _free(md5sum);
    t = stpcpy(t, "\n");
    return res;
}
/*@=boundswrite@*/

/*@-boundswrite@*/
/**
 * Verify header immutable region SHA1 digest.
 * @param ts		transaction set
 * @retval t		verbose success/failure text
 * @param sha1ctx
 * @return 		RPMSIG_OK on success
 */
static rpmVerifySignatureReturn
verifySHA1Signature(const rpmts ts, /*@out@*/ char * t,
		/*@null@*/ DIGEST_CTX sha1ctx)
	/*@modifies *t @*/
{
    const void * sig = rpmtsSig(ts);
#ifdef	NOTYET
    int_32 siglen = rpmtsSiglen(ts);
#endif
    pgpDig dig = rpmtsDig(ts);
    rpmVerifySignatureReturn res;
    const char * SHA1 = NULL;

    *t = '\0';
    t = stpcpy(t, _("Header SHA1 digest: "));

    if (sha1ctx == NULL || sig == NULL || dig == NULL) {
	res = RPMSIG_NOKEY;		/* XXX RPMSIG_ARGS */
	t = stpcpy(t, rpmSigString(res));
	goto exit;
    }

    (void) rpmDigestFinal(rpmDigestDup(sha1ctx),
		(void **)&SHA1, NULL, 1);

    if (SHA1 == NULL || strlen(SHA1) != strlen(sig) || strcmp(SHA1, sig)) {
	res = RPMSIG_BAD;
	t = stpcpy(t, rpmSigString(res));
	t = stpcpy(t, " Expected(");
	t = stpcpy(t, sig);
	t = stpcpy(t, ") != (");
    } else {
	res = RPMSIG_OK;
	t = stpcpy(t, rpmSigString(res));
	t = stpcpy(t, " (");
    }
    if (SHA1)
	t = stpcpy(t, SHA1);
    t = stpcpy(t, ")");

exit:
    SHA1 = _free(SHA1);
    t = stpcpy(t, "\n");
    return res;
}
/*@=boundswrite@*/

/**
 * Convert hex to binary nibble.
 * @param c            hex character
 * @return             binary nibble
 */
static inline unsigned char nibble(char c)
	/*@*/
{
    if (c >= '0' && c <= '9')
	return (c - '0');
    if (c >= 'A' && c <= 'F')
	return (c - 'A') + 10;
    if (c >= 'a' && c <= 'f')
	return (c - 'a') + 10;
    return 0;
}

/*@-boundswrite@*/
/**
 * Verify PGP (aka RSA/MD5) signature.
 * @param ts		transaction set
 * @retval t		verbose success/failure text
 * @param md5ctx
 * @return 		RPMSIG_OK on success
 */
static rpmVerifySignatureReturn
verifyPGPSignature(rpmts ts, /*@out@*/ char * t,
		/*@null@*/ DIGEST_CTX md5ctx)
	/*@globals rpmGlobalMacroContext, fileSystem, internalState @*/
	/*@modifies ts, *t, rpmGlobalMacroContext, fileSystem, internalState */
{
    const void * sig = rpmtsSig(ts);
#ifdef	NOTYET
    int_32 siglen = rpmtsSiglen(ts);
#endif
    int_32 sigtag = rpmtsSigtag(ts);
    pgpDig dig = rpmtsDig(ts);
    pgpDigParams sigp = rpmtsSignature(ts);
    rpmVerifySignatureReturn res;
    int xx;

    *t = '\0';
    t = stpcpy(t, _("V3 RSA/MD5 signature: "));

    if (md5ctx == NULL || sig == NULL || dig == NULL || sigp == NULL) {
	res = RPMSIG_NOKEY;		/* XXX RPMSIG_ARGS */
	goto exit;
    }

    /* XXX sanity check on sigtag and signature agreement. */
    if (!(sigtag == RPMSIGTAG_PGP
    	&& sigp->pubkey_algo == PGPPUBKEYALGO_RSA
    	&& sigp->hash_algo == PGPHASHALGO_MD5))
    {
	res = RPMSIG_NOKEY;
	goto exit;
    }

    {	DIGEST_CTX ctx = rpmDigestDup(md5ctx);
	byte signhash16[2];
	const char * s;

	if (sigp->hash != NULL)
	    xx = rpmDigestUpdate(ctx, sigp->hash, sigp->hashlen);

#ifdef	NOTYET	/* XXX not for binary/text document signatures. */
	if (sigp->sigtype == 4) {
	    int nb = dig->nbytes + sigp->hashlen;
	    byte trailer[6];
	    nb = htonl(nb);
	    trailer[0] = 0x4;
	    trailer[1] = 0xff;
	    memcpy(trailer+2, &nb, sizeof(nb));
	    xx = rpmDigestUpdate(ctx, trailer, sizeof(trailer));
	}
#endif

	xx = rpmDigestFinal(ctx, (void **)&dig->md5, &dig->md5len, 1);

	/* Compare leading 16 bits of digest for quick check. */
	s = dig->md5;
	signhash16[0] = (nibble(s[0]) << 4) | nibble(s[1]);
	signhash16[1] = (nibble(s[2]) << 4) | nibble(s[3]);
	if (memcmp(signhash16, sigp->signhash16, sizeof(signhash16))) {
	    res = RPMSIG_BAD;
	    goto exit;
	}

    }

    {	const char * prefix = "3020300c06082a864886f70d020505000410";
	unsigned int nbits = 1024;
	unsigned int nb = (nbits + 7) >> 3;
	const char * hexstr;
	char * tt;

	hexstr = tt = xmalloc(2 * nb + 1);
	memset(tt, 'f', (2 * nb));
	tt[0] = '0'; tt[1] = '0';
	tt[2] = '0'; tt[3] = '1';
	tt += (2 * nb) - strlen(prefix) - strlen(dig->md5) - 2;
	*tt++ = '0'; *tt++ = '0';
	tt = stpcpy(tt, prefix);
	tt = stpcpy(tt, dig->md5);

	mp32nzero(&dig->rsahm);	mp32nsethex(&dig->rsahm, hexstr);

	hexstr = _free(hexstr);

    }

    /* Retrieve the matching public key. */
    res = rpmtsFindPubkey(ts);
    if (res != RPMSIG_OK)
	goto exit;

    if (rsavrfy(&dig->rsa_pk, &dig->rsahm, &dig->c))
	res = RPMSIG_OK;
    else
	res = RPMSIG_BAD;

exit:
    t = stpcpy(t, rpmSigString(res));
    if (sigp != NULL) {
	t = stpcpy(t, ", key ID ");
	(void) pgpHexCvt(t, sigp->signid+4, sizeof(sigp->signid)-4);
	t += strlen(t);
    }
    t = stpcpy(t, "\n");
    return res;
}
/*@=boundswrite@*/

/**
 * Verify GPG (aka DSA) signature.
 * @param ts		transaction set
 * @retval t		verbose success/failure text
 * @param sha1ctx
 * @return 		RPMSIG_OK on success
 */
/*@-boundswrite@*/
static rpmVerifySignatureReturn
verifyGPGSignature(rpmts ts, /*@out@*/ char * t,
		/*@null@*/ DIGEST_CTX sha1ctx)
	/*@globals rpmGlobalMacroContext, fileSystem, internalState @*/
	/*@modifies ts, *t, rpmGlobalMacroContext, fileSystem, internalState */
{
    const void * sig = rpmtsSig(ts);
#ifdef	NOTYET
    int_32 siglen = rpmtsSiglen(ts);
#endif
    int_32 sigtag = rpmtsSigtag(ts);
    pgpDig dig = rpmtsDig(ts);
    pgpDigParams sigp = rpmtsSignature(ts);
    rpmVerifySignatureReturn res;
    int xx;

    *t = '\0';
    if (dig != NULL && dig->hdrsha1ctx == sha1ctx)
	t = stpcpy(t, _("Header "));
    t = stpcpy(t, _("V3 DSA signature: "));

    if (sha1ctx == NULL || sig == NULL || dig == NULL || sigp == NULL) {
	res = RPMSIG_NOKEY;		/* XXX RPMSIG_ARGS */
	goto exit;
    }

    /* XXX sanity check on sigtag and signature agreement. */
    if (!((sigtag == RPMSIGTAG_GPG || sigtag == RPMSIGTAG_DSA)
    	&& sigp->pubkey_algo == PGPPUBKEYALGO_DSA
    	&& sigp->hash_algo == PGPHASHALGO_SHA1))
    {
	res = RPMSIG_NOKEY;
	goto exit;
    }

    {	DIGEST_CTX ctx = rpmDigestDup(sha1ctx);
	byte signhash16[2];

	if (sigp->hash != NULL)
	    xx = rpmDigestUpdate(ctx, sigp->hash, sigp->hashlen);

#ifdef	NOTYET	/* XXX not for binary/text document signatures. */
	if (sigp->sigtype == 4) {
	    int nb = dig->nbytes + sigp->hashlen;
	    byte trailer[6];
	    nb = htonl(nb);
	    trailer[0] = 0x4;
	    trailer[1] = 0xff;
	    memcpy(trailer+2, &nb, sizeof(nb));
	    xx = rpmDigestUpdate(ctx, trailer, sizeof(trailer));
	}
#endif
	xx = rpmDigestFinal(ctx, (void **)&dig->sha1, &dig->sha1len, 1);

	mp32nzero(&dig->hm);	mp32nsethex(&dig->hm, dig->sha1);

	/* Compare leading 16 bits of digest for quick check. */
	signhash16[0] = (*dig->hm.data >> 24) & 0xff;
	signhash16[1] = (*dig->hm.data >> 16) & 0xff;
	if (memcmp(signhash16, sigp->signhash16, sizeof(signhash16))) {
	    res = RPMSIG_BAD;
	    goto exit;
	}
    }

    /* Retrieve the matching public key. */
    res = rpmtsFindPubkey(ts);
    if (res != RPMSIG_OK)
	goto exit;

    if (dsavrfy(&dig->p, &dig->q, &dig->g,
		&dig->hm, &dig->y, &dig->r, &dig->s))
	res = RPMSIG_OK;
    else
	res = RPMSIG_BAD;

exit:
    t = stpcpy(t, rpmSigString(res));
    if (sigp != NULL) {
	t = stpcpy(t, ", key ID ");
	(void) pgpHexCvt(t, sigp->signid+4, sizeof(sigp->signid)-4);
	t += strlen(t);
    }
    t = stpcpy(t, "\n");
    return res;
}
/*@=boundswrite@*/

rpmVerifySignatureReturn
rpmVerifySignature(const rpmts ts, char * result)
{
    const void * sig = rpmtsSig(ts);
    int_32 siglen = rpmtsSiglen(ts);
    int_32 sigtag = rpmtsSigtag(ts);
    pgpDig dig = rpmtsDig(ts);
    rpmVerifySignatureReturn res;

    if (sig == NULL || siglen <= 0 || dig == NULL) {
	sprintf(result, _("Verify signature: BAD PARAMETERS\n"));
	return RPMSIG_UNKNOWN;
    }

    switch (sigtag) {
    case RPMSIGTAG_SIZE:
	res = verifySizeSignature(ts, result);
	break;
    case RPMSIGTAG_MD5:
	res = verifyMD5Signature(ts, result, dig->md5ctx);
	break;
    case RPMSIGTAG_SHA1:
	res = verifySHA1Signature(ts, result, dig->hdrsha1ctx);
	break;
    case RPMSIGTAG_RSA:
    case RPMSIGTAG_PGP5:	/* XXX legacy */
    case RPMSIGTAG_PGP:
	res = verifyPGPSignature(ts, result, dig->md5ctx);
	break;
    case RPMSIGTAG_DSA:
	res = verifyGPGSignature(ts, result, dig->hdrsha1ctx);
	break;
    case RPMSIGTAG_GPG:
	res = verifyGPGSignature(ts, result, dig->sha1ctx);
	break;
    case RPMSIGTAG_LEMD5_1:
    case RPMSIGTAG_LEMD5_2:
	sprintf(result, _("Broken MD5 digest: UNSUPPORTED\n"));
	res = RPMSIG_UNKNOWN;
	break;
    default:
	sprintf(result, _("Signature: UNKNOWN (%d)\n"), sigtag);
	res = RPMSIG_UNKNOWN;
	break;
    }
    return res;
}
