
/*
 * Neotonic ClearSilver Templating System
 *
 * This code is made available under the terms of the FSF's
 * Library Gnu Public License (LGPL).
 *
 * Copyright (C) 2001 by Brandon Long
 */

/* rfc2388 defines multipart/form-data which is primarily used for
 * HTTP file upload
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>
#include <ctype.h>
#include <string.h>
#include "util/neo_err.h"
#include "util/neo_misc.h"
#include "util/neo_str.h"
#include "cgi.h"
#include "cgiwrap.h"

static NEOERR * _header_value (char *hdr, char **val)
{
  char *p, *q;
  int l;

  *val = NULL;

  p = hdr;
  while (*p && isspace(*p)) p++;
  q = p;
  while (*q && !isspace(*q) && *q != ';') q++;
  if (!*p || p == q) return STATUS_OK;

  l = q - p;
  *val = (char *) malloc (l+1);
  if (*val == NULL)
    return nerr_raise (NERR_NOMEM, "Unable to allocate space for val");
  memcpy (*val, p, l);
  (*val)[l] = '\0';

  return STATUS_OK;
}

static NEOERR * _header_attr (char *hdr, char *attr, char **val)
{
  char *p, *k, *v;
  int found = 0;
  int l, al;
  char *r;

  *val = NULL;
  l = strlen(attr);

  /* skip value */
  p = hdr;
  while (*p && *p != ';') p++;
  if (!*p) return STATUS_OK;

  p++;
  while(*p && !found)
  {
    while (*p && isspace(*p)) p++;
    if (!*p) return STATUS_OK;
    /* attr name */
    k = p;
    while (*p && !isspace(*p) && *p != ';' && *p != '=') p++;
    if (!*p) return STATUS_OK;
    if (l == (p-k) && !strncasecmp(attr, k, l))
      found = 1;

    while (*p && isspace(*p)) p++;
    if (*p != ';' && *p != '=') return STATUS_OK;
    if (*p == ';')
    {
      if (found)
      {
	*val = strdup ("");
	if (*val == NULL) 
	  return nerr_raise (NERR_NOMEM, "Unable to allocate value");
	return STATUS_OK;
      }
    }
    else 
    {
      p++;
      if (*p == '"')
      {
	v = ++p;
	while (*p && *p != '"') p++;
	al = p-v;
	if (*p) p++;
      }
      else
      {
	v = p;
	while (*p && !isspace(*p) && *p != ';') p++;
	al = p-v;
      }
      if (found)
      {
	r = (char *) malloc (al+1);
	if (r == NULL) 
	  return nerr_raise (NERR_NOMEM, "Unable to allocate value");
	memcpy (r, v, al);
	r[al] = '\0';
	*val = r;
	return STATUS_OK;
      }
    }
    if (*p) p++;
  }
  return STATUS_OK;
}

static NEOERR * _read_line (CGI *cgi, char **s, int *l)
{
  int ofs = 0;
  char *p;

  if (cgi->buf == NULL)
  {
    cgi->buflen = 4096;
    cgi->buf = (char *) malloc (sizeof(char) * cgi->buflen);
    if (cgi->buf == NULL)
      return nerr_raise (NERR_NOMEM, "Unable to allocate cgi buf");
  }
  if (cgi->unget)
  {
    cgi->unget = FALSE;
    *s = cgi->buf;
    if (cgi->found_nl)
      *l = cgi->nl;
    else
      *l = cgi->readlen;
    return STATUS_OK;
  }
  if (cgi->found_nl)
  {
    if (cgi->readlen < cgi->buflen)
    {
      ofs = cgi->readlen - cgi->nl;
    }
    else
    {
      ofs = cgi->buflen - cgi->nl;
    }
    memmove (cgi->buf, cgi->buf + cgi->nl, ofs);
    if (cgi->readlen < cgi->buflen)
    {
      cgi->readlen = ofs;
      p = memchr (cgi->buf, '\n', cgi->readlen);
      if (!p)
      {
	cgi->found_nl = FALSE;
	*s = cgi->buf;
	*l = cgi->readlen;
	return STATUS_OK;
      }
      *s = cgi->buf;
      *l = p - cgi->buf + 1;
      cgi->found_nl = TRUE;
      cgi->nl = *l;
      return STATUS_OK;
    }
  }
  cgiwrap_read (cgi->buf + ofs, cgi->buflen - ofs, &(cgi->readlen));
  cgi->data_read += cgi->readlen;
  if (cgi->upload_cb)
  {
    if (cgi->upload_cb (cgi, cgi->data_read, cgi->data_expected))
      return nerr_raise (CGIUploadCancelled, "Upload Cancelled");
  }
  cgi->readlen += ofs;
  p = memchr (cgi->buf, '\n', cgi->readlen);
  if (!p)
  {
    cgi->found_nl = FALSE;
    *s = cgi->buf;
    *l = cgi->readlen;
    return STATUS_OK;
  }
  *s = cgi->buf;
  *l = p - cgi->buf + 1;
  cgi->found_nl = TRUE;
  cgi->nl = *l;
  return STATUS_OK;
}

static NEOERR * _read_header_line (CGI *cgi, STRING *line)
{
  NEOERR *err;
  char *s, *p;
  int l;

  err = _read_line (cgi, &s, &l);
  if (err) return nerr_pass (err);
  if (l == 0) return STATUS_OK;
  if (isspace (s[0])) return STATUS_OK;
  while (l && isspace(s[l-1])) l--;
  err = string_appendn (line, s, l);
  if (err) return nerr_pass (err);

  while (1)
  {
    err = _read_line (cgi, &s, &l);
    if (err) break;
    if (l == 0) break;
    if (!(s[0] == ' ' || s[0] == '\t'))
    {
      cgi->unget = TRUE;
      break;
    }
    while (l && isspace(s[l-1])) l--;
    p = s;
    while (*p && isspace(*p) && (p-s < l)) p++;
    err = string_append_char (line, ' ');
    if (err) break;
    err = string_appendn (line, p, l - (p-s));
    if (err) break;
  }
  return nerr_pass (err);
}

static BOOL _is_boundary (char *boundary, char *s, int l, int *done)
{
  static char *old_boundary = NULL;
  static int bl;

  /* cache the boundary strlen... more pointless optimization by blong */
  if (old_boundary != boundary)
  {
    old_boundary = boundary;
    bl = strlen(boundary);
  }

  if (s[l-1] != '\n')
    return FALSE;
  l--;
  if (s[l-1] == '\r')
    l--;

  if (bl+2 == l && s[0] == '-' && s[1] == '-' && !strncmp (s+2, boundary, bl))
    return TRUE;
  if (bl+4 == l && s[0] == '-' && s[1] == '-' && 
      !strncmp (s+2, boundary, bl) &&
      s[l-1] == '-' && s[l-2] == '-')
  {
    *done = 1;
    return TRUE;
  }
  return FALSE;
}

static NEOERR * _find_boundary (CGI *cgi, char *boundary, int *done)
{
  NEOERR *err;
  char *s;
  int l;

  *done = 0;
  while (1)
  {
    err = _read_line (cgi, &s, &l);
    if (err) return nerr_pass (err);
    if (l == 0) {
      *done = 1;
      return STATUS_OK;
    }
    if (_is_boundary(boundary, s, l, done))
      return STATUS_OK;
  }
  return STATUS_OK;
}

static NEOERR * _read_part (CGI *cgi, char *boundary, int *done)
{
  NEOERR *err = STATUS_OK;
  STRING str;
  FILE *fp = NULL;
  char buf[256];
  char *p;
  char *name = NULL, *filename = NULL;
  char *type = NULL, *tmp = NULL;
  char *last = NULL;

  string_init (&str);

  while (1)
  {
    err = _read_header_line (cgi, &str);
    if (err) break;
    if (str.buf == NULL || str.buf[0] == '\0') break;
    p = strchr (str.buf, ':');
    if (p)
    {
      *p = '\0';
      if (!strcasecmp(str.buf, "content-disposition"))
      {
	err = _header_attr (p+1, "name", &name);
	if (err) break;
	err = _header_attr (p+1, "filename", &filename);
	if (err) break;
      }
      else if (!strcasecmp(str.buf, "content-type"))
      {
	err = _header_value (p+1, &type);
	if (err) break;
      }
      else if (!strcasecmp(str.buf, "content-encoding"))
      {
	err = _header_value (p+1, &tmp);
	if (err) break;
	if (tmp && strcmp(tmp, "7bit") && strcmp(tmp, "8bit") && 
	    strcmp(tmp, "binary"))
	{
	  free(tmp);
	  err = nerr_raise (NERR_ASSERT, "form-data encoding is not supported");
	  break;
	}
	free(tmp);
      }
    }
    string_set(&str, "");
  }
  if (err) 
  {
    string_clear(&str);
    if (name) free(name);
    if (filename) free(filename);
    if (type) free(type);
    return nerr_pass (err);
  }

  do
  {
    if (filename)
    {
      char path[_POSIX_PATH_MAX];
      int fd;

      snprintf (path, sizeof(path), "/tmp/cgi_upload.XXXXXX");

      fd = mkstemp(path);
      if (fd == -1)
      {
	err = nerr_raise_errno (NERR_SYSTEM, "Unable to open temp file %s", 
	    path);
	break;
      }

      fp = fdopen (fd, "w+");
      if (fp == NULL)
      {
	close(fd);
	err = nerr_raise_errno (NERR_SYSTEM, "Unable to fdopen file %s", path);
	break;
      }
      unlink (path);
      if (cgi->files == NULL)
      {
	err = uListInit (&(cgi->files), 10, 0);
	if (err)
	{
	  fclose(fp);
	  break;
	}
      }
      err = uListAppend (cgi->files, fp);
      if (err)
      {
	fclose (fp);
	break;
      }
    }
    string_set(&str, "");
    while (1)
    {
      char *s;
      int l, w;

      err = _read_line (cgi, &s, &l);
      if (err) break;
      if (l == 0) break;
      if (_is_boundary(boundary, s, l, done)) break;
      if (filename)
      {
	if (last) fwrite (last, sizeof(char), strlen(last), fp);
	if (l > 2 && s[l-1] == '\n' && s[l-2] == '\r')
	{
	  last = "\r\n";
	  l-=2;
	}
	else if (l > 1 && s[l-1] == '\n')
	{
	  last = "\n";
	  l--;
	}
	else last = NULL;
	w = fwrite (s, sizeof(char), l, fp);
	if (w != l)
	{
	  err = nerr_raise_errno (NERR_IO, 
	      "Short write on file %s upload %d < %d", filename, w, l);
	  break;
	}
      }
      else
      {
	err = string_appendn(&str, s, l);
	if (err) break;
      }
    }
    if (err) break;
  } while (0);

  /* Set up the cgi data */
  if (!err)
  {
    if (filename)
    {
      fseek(fp, 0, SEEK_SET);
      snprintf (buf, sizeof(buf), "Query.%s", name);
      err = hdf_set_value (cgi->hdf, buf, filename);
      if (!err && type)
      {
	snprintf (buf, sizeof(buf), "Query.%s.Type", name);
	err = hdf_set_value (cgi->hdf, buf, type);
      }
      if (!err)
      {
	snprintf (buf, sizeof(buf), "Query.%s.FileHandle", name);
	err = hdf_set_int_value (cgi->hdf, buf, uListLength(cgi->files));
      }
    }
    else
    {
      snprintf (buf, sizeof(buf), "Query.%s", name);
      while (str.len && isspace(str.buf[str.len-1]))
      {
	str.buf[str.len-1] = '\0';
	str.len--;
      }
      if (!(cgi->ignore_empty_form_vars && str.len == 0))
	err = hdf_set_value (cgi->hdf, buf, str.buf);
    }
  }

  string_clear(&str);
  if (name) free(name);
  if (filename) free(filename);
  if (type) free(type);

  return nerr_pass (err);
}

NEOERR * parse_rfc2388 (CGI *cgi)
{
  NEOERR *err;
  char *ct_hdr;
  char *boundary = NULL;
  int l;
  int done = 0;

  l = hdf_get_int_value (cgi->hdf, "CGI.ContentLength", -1);
  ct_hdr = hdf_get_value (cgi->hdf, "CGI.ContentType", NULL);
  if (ct_hdr == NULL) 
    return nerr_raise (NERR_ASSERT, "No content type header?");

  cgi->data_expected = l;
  cgi->data_read = 0;
  if (cgi->upload_cb)
  {
    if (cgi->upload_cb (cgi, cgi->data_read, cgi->data_expected))
      return nerr_raise (CGIUploadCancelled, "Upload Cancelled");
  }

  err = _header_attr (ct_hdr, "boundary", &boundary);
  if (err) return nerr_pass (err);
  err = _find_boundary(cgi, boundary, &done);
  while (!err && !done)
  {
    err = _read_part (cgi, boundary, &done);
  }

  if (boundary) free(boundary);
  return nerr_pass(err);
}

/* this is here because it gets populated in this file */
FILE *cgi_filehandle (CGI *cgi, char *form_name)
{
  NEOERR *err;
  FILE *fp;
  char buf[256];
  int n;

  snprintf (buf, sizeof(buf), "Query.%s.FileHandle", form_name);
  n = hdf_get_int_value (cgi->hdf, buf, -1);
  if (n == -1) return NULL;
  err = uListGet(cgi->files, n-1, (void **)&fp);
  if (err)
  {
    nerr_ignore(&err);
    return NULL;
  }
  return fp;
}