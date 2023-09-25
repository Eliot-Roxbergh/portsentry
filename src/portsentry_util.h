#pragma once
/************************************************************************/
/*                                                                      */
/* PortSentry                                                           */
/*                                                                      */
/* This software is Copyright(c) 1997-2003 Craig Rowland                */
/*                                                                      */
/* This software is covered under the Common Public License v1.0        */
/* See the enclosed LICENSE file for more information.                  */
/*                                                                      */
/* Created: 10-12-1997                                                  */
/* Modified: 05-23-2003                                                 */
/*                                                                      */
/* Send all changes/modifications/bugfixes to:                          */
/* craigrowland at users dot sourceforge dot net                        */
/*                                                                      */
/* $Id: portsentry_util.h,v 1.10 2003/05/23 17:42:07 crowland Exp crowland $ */
/************************************************************************/

#include <stdint.h>
#include <sys/types.h>

/* IP address length plus null */
#define IPMAXBUF 16

char *SafeStrncpy(char *, const char *, size_t);
char *CleanIpAddr(char *, const char *);
int CleanAndResolve(char *, const char *);
void ResolveAddr(const struct sockaddr *saddr, const socklen_t saddrLen, char *resolvedHost, const int resolvedHostSize);
long getLong(char *buffer);
int DisposeTarget(char *, int, int);
int IsPortInUse(uint16_t port, int proto);
int EvalPortsInUse(int *portCount, int *ports);
