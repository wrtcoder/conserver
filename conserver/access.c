/*
 *  $Id: access.c,v 5.73 2004/05/23 16:44:25 bryan Exp $
 *
 *  Copyright conserver.com, 2000
 *
 *  Maintainer/Enhancer: Bryan Stansell (bryan@conserver.com)
 *
 *  Copyright GNAC, Inc., 1998
 */

/*
 * Copyright 1992 Purdue Research Foundation, West Lafayette, Indiana
 * 47907.  All rights reserved.
 *
 * Written by Kevin S Braunsdorf, ksb@cc.purdue.edu, purdue!ksb
 *
 * This software is not subject to any license of the American Telephone
 * and Telegraph Company or the Regents of the University of California.
 *
 * Permission is granted to anyone to use this software for any purpose on
 * any computer system, and to alter it and redistribute it freely, subject
 * to the following restrictions:
 *
 * 1. Neither the authors nor Purdue University are responsible for any
 *    consequences of the use of this software.
 *
 * 2. The origin of this software must not be misrepresented, either by
 *    explicit claim or by omission.  Credit to the authors and Purdue
 *    University must appear in documentation and sources.
 *
 * 3. Altered versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 *
 * 4. This notice may not be removed or altered.
 */

#include <compat.h>

#include <cutil.h>
#include <access.h>
#include <consent.h>
#include <client.h>
#include <group.h>
#include <readcfg.h>
#include <main.h>


/* Compare an Internet address (IPv4 expected), with an address pattern
 * passed as a character string representing an address in the Internet
 * standard `.' notation, optionally followed by a slash and an integer
 * specifying the number of bits in the network portion of the address
 * (the netmask size). If not specified explicitly, the netmask size used
 * is that implied by the address class. If either the netmask is specified
 * explicitly, or the local network address part of the pattern is zero,
 * then only the network number parts of the addresses are compared;
 * otherwise the entire addresses are compared.
 *
 * Returns 0 if the addresses match, else returns 1.
 */
int
#if PROTOTYPES
AddrCmp(struct in_addr *addr, char *pattern)
#else
AddrCmp(addr, pattern)
    struct in_addr *addr;
    char *pattern;
#endif
{
    in_addr_t hostaddr, pattern_addr, netmask;
    char *p, *slash_posn;
    static STRING *buf = (STRING *)0;
#if HAVE_INET_ATON
    struct in_addr inetaddr;
#endif

    if (buf == (STRING *)0)
	buf = AllocString();
    slash_posn = strchr(pattern, '/');
    if (slash_posn != NULL) {
	BuildString((char *)0, buf);
	BuildString(pattern, buf);
	buf->string[slash_posn - pattern] = '\0';	/* isolate the address */
	p = buf->string;
    } else
	p = pattern;

#if HAVE_INET_ATON
    if (inet_aton(p, &inetaddr) == 0)
	return 1;
    pattern_addr = inetaddr.s_addr;
#else
    pattern_addr = inet_addr(p);
    if (pattern_addr == (in_addr_t) (-1))
	return 1;		/* malformed address */
#endif

    if (slash_posn) {
	/* convert explicit netmask */
	int mask_bits = atoi(slash_posn + 1);
	for (netmask = 0; mask_bits > 0; --mask_bits)
	    netmask = 0x80000000 | (netmask >> 1);
    } else {
	/* netmask implied by address class */
	in_addr_t ia = ntohl(pattern_addr);
	if (IN_CLASSA(ia))
	    netmask = IN_CLASSA_NET;
	else if (IN_CLASSB(ia))
	    netmask = IN_CLASSB_NET;
	else if (IN_CLASSC(ia))
	    netmask = IN_CLASSC_NET;
	else
	    return 1;		/* unsupported address class */
    }
    netmask = htonl(netmask);
    if (~netmask & pattern_addr)
	netmask = 0xffffffff;	/* compare entire addresses */
    hostaddr = addr->s_addr;

    CONDDEBUG((1, "AddrCmp(): host=%lx(%lx/%lx) acl=%lx(%lx/%lx)",
	       hostaddr & netmask, hostaddr, netmask,
	       pattern_addr & netmask, pattern_addr, netmask));
    return (hostaddr & netmask) != (pattern_addr & netmask);
}

/* return the access type for a given host entry			(ksb)
 */
char
#if PROTOTYPES
AccType(struct in_addr *addr, char **peername)
#else
AccType(addr, peername)
    struct in_addr *addr;
    char **peername;
#endif
{
    ACCESS *pACtmp;
    socklen_t so;
    struct hostent *he = (struct hostent *)0;
    int a;
    char ret;
#if TRUST_REVERSE_DNS
    char **revNames = (char **)0;
#endif

    CONDDEBUG((1, "AccType(): ip=%s", inet_ntoa(*addr)));

    ret = config->defaultaccess;
    so = sizeof(*addr);

#if TRUST_REVERSE_DNS
    /* if we trust reverse dns, we get the names associated with
     * the address we're checking and then check each of those
     * against the access list entries (below).
     */
    if ((he =
	 gethostbyaddr((char *)addr, so,
		       AF_INET)) == (struct hostent *)0) {
	Error("AccType(): gethostbyaddr(%s): %s", inet_ntoa(*addr),
	      hstrerror(h_errno));
    } else {
	char *hname;
	if (he->h_name != (char *)0) {
	    /* count up the number of names */
	    for (a = 0, hname = he->h_aliases[a]; hname != (char *)0;
		 hname = he->h_aliases[++a]);
	    a += 2;		/* h_name + (char *)0 */
	    /* now duplicate them */
	    if ((revNames =
		 (char **)calloc(a, sizeof(char *))) != (char **)0) {
		for (hname = he->h_name, a = 0; hname != (char *)0;
		     hname = he->h_aliases[a++]) {
		    if ((revNames[a] = StrDup(hname)) == (char *)0)
			break;
		    CONDDEBUG((1, "AccType(): revNames[%d]='%s'", a,
			       hname));
		}
	    }
	}
    }
#endif

    for (pACtmp = pACList; pACtmp != (ACCESS *)0; pACtmp = pACtmp->pACnext) {
	CONDDEBUG((1, "AccType(): who=%s, trust=%c", pACtmp->pcwho,
		   pACtmp->ctrust));
	if (pACtmp->isCIDR != 0) {
	    if (AddrCmp(addr, pACtmp->pcwho) == 0) {
		ret = pACtmp->ctrust;
		goto common_ret;
	    }
	    continue;
	}

	if ((he = gethostbyname(pACtmp->pcwho)) == (struct hostent *)0) {
	    Error("AccType(): gethostbyname(%s): %s", pACtmp->pcwho,
		  hstrerror(h_errno));
	} else if (4 != he->h_length || AF_INET != he->h_addrtype) {
	    Error
		("AccType(): gethostbyname(%s): wrong address size (4 != %d) or address family (%d != %d)",
		 pACtmp->pcwho, he->h_length, AF_INET, he->h_addrtype);
	} else {
	    for (a = 0; he->h_addr_list[a] != (char *)0; a++) {
		CONDDEBUG((1, "AccType(): addr=%s",
			   inet_ntoa(*(struct in_addr *)
				     (he->h_addr_list[a]))));
		if (
#if HAVE_MEMCMP
		       memcmp(&(addr->s_addr), he->h_addr_list[a],
			      he->h_length)
#else
		       bcmp(&(addr->s_addr), he->h_addr_list[a],
			    he->h_length)
#endif
		       == 0) {
		    ret = pACtmp->ctrust;
		    goto common_ret;
		}
	    }
	}
#if TRUST_REVERSE_DNS
	/* we chop bits off client names so that we can put domain
	 * names in access lists or even top-level domains.
	 *    allowed conserver.com, net;
	 * this allows anything from conserver.com and anything in
	 * the .net top-level.  without TRUST_REVERSE_DNS, those names
	 * better map to ip addresses for them to take effect.
	 */
	if (revNames != (char **)0) {
	    char *pcName;
	    int wlen;
	    int len;
	    wlen = strlen(pACtmp->pcwho);
	    for (a = 0; revNames[a] != (char *)0; a++) {
		for (pcName = revNames[a], len = strlen(pcName);
		     len >= wlen; len = strlen(++pcName)) {
		    CONDDEBUG((1, "AccType(): name=%s", pcName));
		    if (strcasecmp(pcName, pACtmp->pcwho) == 0) {
			if (peername != (char **)0)
			    *peername = StrDup(revNames[a]);
			ret = pACtmp->ctrust;
			goto common_ret2;
		    }
		    pcName = strchr(pcName, '.');
		    if (pcName == (char *)0)
			break;
		}
	    }
	}
#endif
    }

  common_ret:
    if (config->loghostnames == FLAGTRUE && peername != (char **)0) {
#if TRUST_REVERSE_DNS
	if (revNames != (char **)0 && revNames[0] != (char *)0)
	    *peername = StrDup(revNames[0]);
#else
	if ((he =
	     gethostbyaddr((char *)addr, so,
			   AF_INET)) != (struct hostent *)0) {
	    *peername = StrDup(he->h_name);
	}
#endif
    }
#if TRUST_REVERSE_DNS
  common_ret2:
    if (revNames != (char **)0) {
	for (a = 0; revNames[a] != (char *)0; a++)
	    free(revNames[a]);
	free(revNames);
    }
#endif
    return ret;
}

void
#if PROTOTYPES
SetDefAccess(struct in_addr *pAddr, char *pHost)
#else
SetDefAccess(pAddr, pHost)
    struct in_addr *pAddr;
    char *pHost;
#endif
{
    ACCESS *a;

    while (pACList != (ACCESS *)0) {
	a = pACList->pACnext;
	DestroyAccessList(pACList);
	pACList = a;
    }

#if USE_UNIX_DOMAIN_SOCKETS
    if ((pACList = (ACCESS *)calloc(1, sizeof(ACCESS))) == (ACCESS *)0)
	OutOfMem();
    if ((pACList->pcwho = StrDup("127.0.0.1")) == (char *)0)
	OutOfMem();
    pACList->ctrust = 'a';
    CONDDEBUG((1, "SetDefAccess(): trust=%c, who=%s", pACList->ctrust,
	       pACList->pcwho));
#else
    while (pAddr->s_addr != (in_addr_t) 0) {
	char *addr;

	addr = inet_ntoa(*pAddr);
	if ((a = (ACCESS *)calloc(1, sizeof(ACCESS))) == (ACCESS *)0)
	    OutOfMem();
	if ((a->pcwho = StrDup(addr)) == (char *)0)
	    OutOfMem();
	a->ctrust = 'a';
	a->pACnext = pACList;
	pACList = a;

	CONDDEBUG((1, "SetDefAccess(): trust=%c, who=%s", pACList->ctrust,
		   pACList->pcwho));
	pAddr++;
    }
#endif
}

void
#if PROTOTYPES
DestroyAccessList(ACCESS *pACList)
#else
DestroyAccessList(pACList)
    ACCESS *pACList;
#endif
{
    if (pACList == (ACCESS *)0)
	return;
    if (pACList->pcwho != (char *)0)
	free(pACList->pcwho);
    free(pACList);
}
