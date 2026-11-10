#ifndef _PDE_LIB_XML_H
#define _PDE_LIB_XML_H
#define _PDE_XML_DEFINED

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct XMLAttribute {
  char *name;
  char *value;
} XMLAttribute;

typedef struct XMLElement_s XMLElement_s;
typedef struct XMLElement_s *XMLElement;

struct XMLElement_s {
  char *name;
  char *content;
  XMLAttribute *attributes;
  int attributeCount;
  int attributeCap;
  XMLElement *children;
  int childCount;
  int childCap;
  XMLElement parent;

  /* Method pointers for object-dispatch compatibility */
  XMLElement (*getChild)(XMLElement self, const char *name);
  XMLElement (*getChild_idx)(XMLElement self, int idx);
  int (*getChildCount)(XMLElement self);
  const char *(*getStringAttribute)(XMLElement self, const char *name);
  int (*getIntAttribute)(XMLElement self, const char *name);
  float (*getFloatAttribute)(XMLElement self, const char *name);
  const char *(*getContent)(XMLElement self);
  const char *(*getName)(XMLElement self);
};

static inline const char *_xml_skip_ws(const char *p) {
  while (*p && ((unsigned char)*p <= 32 || *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
  return p;
}

static inline bool _xml_is_all_ws(const char *s, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if ((unsigned char)s[i] > 32) return false;
  }
  return true;
}

static inline char *_xml_unescape(const char *in, size_t in_len) {
  char *out = (char *)malloc(in_len + 1);
  if (!out) return NULL;
  size_t o = 0;
  for (size_t i = 0; i < in_len; ) {
    if (in[i] == '&') {
      if (i + 4 <= in_len && strncmp(&in[i], "&lt;", 4) == 0) { out[o++] = '<'; i += 4; }
      else if (i + 4 <= in_len && strncmp(&in[i], "&gt;", 4) == 0) { out[o++] = '>'; i += 4; }
      else if (i + 5 <= in_len && strncmp(&in[i], "&amp;", 5) == 0) { out[o++] = '&'; i += 5; }
      else if (i + 6 <= in_len && strncmp(&in[i], "&quot;", 6) == 0) { out[o++] = '"'; i += 6; }
      else if (i + 6 <= in_len && strncmp(&in[i], "&apos;", 6) == 0) { out[o++] = '\''; i += 6; }
      else if (i + 3 <= in_len && in[i+1] == '#') {
        size_t semi = i + 2;
        while (semi < in_len && semi < i + 10 && in[semi] != ';') semi++;
        if (semi < in_len && in[semi] == ';') {
          long code = 0;
          if (in[i+2] == 'x' || in[i+2] == 'X') code = strtol(&in[i+3], NULL, 16);
          else code = strtol(&in[i+2], NULL, 10);
          if (code > 0 && code < 256) out[o++] = (char)code;
          i = semi + 1;
        } else {
          out[o++] = in[i++];
        }
      } else {
        out[o++] = in[i++];
      }
    } else {
      out[o++] = in[i++];
    }
  }
  out[o] = '\0';
  return out;
}

/* Forward declarations */
static inline const char *XMLElement_getName(XMLElement xml);
static inline const char *XMLElement_getContent(XMLElement xml);
static inline int XMLElement_getChildCount(XMLElement xml);
static inline XMLElement XMLElement_getChild_idx(XMLElement xml, int idx);
static inline XMLElement XMLElement_getChild_name(XMLElement xml, const char *name);
static inline const char *XMLElement_getStringAttribute(XMLElement xml, const char *name);
static inline int XMLElement_getIntAttribute(XMLElement xml, const char *name);
static inline float XMLElement_getFloatAttribute(XMLElement xml, const char *name);
static inline void XMLElement_free(XMLElement xml);

static inline void _xml_bind_methods(XMLElement xml) {
  if (!xml) return;
  xml->getChild = XMLElement_getChild_name;
  xml->getChild_idx = XMLElement_getChild_idx;
  xml->getChildCount = XMLElement_getChildCount;
  xml->getStringAttribute = XMLElement_getStringAttribute;
  xml->getIntAttribute = XMLElement_getIntAttribute;
  xml->getFloatAttribute = XMLElement_getFloatAttribute;
  xml->getContent = XMLElement_getContent;
  xml->getName = XMLElement_getName;
}

static inline XMLElement _xml_elem_new(const char *name) {
  XMLElement e = (XMLElement)calloc(1, sizeof(XMLElement_s));
  if (!e) return NULL;
  if (name) e->name = strdup(name);
  _xml_bind_methods(e);
  return e;
}

static inline void _xml_add_attribute(XMLElement e, const char *name, const char *value) {
  if (!e || !name) return;
  if (e->attributeCount >= e->attributeCap) {
    int ncap = e->attributeCap ? e->attributeCap * 2 : 4;
    XMLAttribute *na = (XMLAttribute *)realloc(e->attributes, sizeof(XMLAttribute) * ncap);
    if (!na) return;
    e->attributes = na;
    e->attributeCap = ncap;
  }
  e->attributes[e->attributeCount].name = strdup(name);
  e->attributes[e->attributeCount].value = strdup(value ? value : "");
  e->attributeCount++;
}

static inline void _xml_add_child(XMLElement parent, XMLElement child) {
  if (!parent || !child) return;
  if (parent->childCount >= parent->childCap) {
    int ncap = parent->childCap ? parent->childCap * 2 : 4;
    XMLElement *nc = (XMLElement *)realloc(parent->children, sizeof(XMLElement) * ncap);
    if (!nc) return;
    parent->children = nc;
    parent->childCap = ncap;
  }
  parent->children[parent->childCount++] = child;
  child->parent = parent;
}

static inline void _xml_append_content(XMLElement e, const char *str, size_t len) {
  if (!e || !str || len == 0) return;
  size_t cur = e->content ? strlen(e->content) : 0;
  char *nc = (char *)realloc(e->content, cur + len + 1);
  if (!nc) return;
  memcpy(nc + cur, str, len);
  nc[cur + len] = '\0';
  e->content = nc;
}

static inline XMLElement _xml_parse_node(const char **ptr) {
  const char *p = *ptr;
  while (*p) {
    p = _xml_skip_ws(p);
    if (*p == '<') {
      if (strncmp(p, "<?", 2) == 0) {
        const char *end = strstr(p, "?>");
        if (end) p = end + 2; else { *ptr = ""; return NULL; }
        continue;
      }
      if (strncmp(p, "<!--", 4) == 0) {
        const char *end = strstr(p, "-->");
        if (end) p = end + 3; else { *ptr = ""; return NULL; }
        continue;
      }
      if (strncasecmp(p, "<!DOCTYPE", 9) == 0) {
        while (*p && *p != '>') p++;
        if (*p == '>') p++;
        continue;
      }
      break;
    }
    p++;
  }

  if (!*p || *p != '<' || p[1] == '/') {
    *ptr = p;
    return NULL;
  }

  p++; /* skip '<' */
  const char *nstart = p;
  while (*p && !isspace((unsigned char)*p) && *p != '>' && *p != '/') p++;
  size_t nlen = p - nstart;
  char *tag_name = (char *)malloc(nlen + 1);
  if (!tag_name) { *ptr = p; return NULL; }
  memcpy(tag_name, nstart, nlen);
  tag_name[nlen] = '\0';

  XMLElement elem = _xml_elem_new(tag_name);
  free(tag_name);
  if (!elem) { *ptr = p; return NULL; }

  /* Parse attributes */
  while (*p) {
    p = _xml_skip_ws(p);
    if (*p == '>' || *p == '/' || *p == '\0') break;
    const char *aname_start = p;
    while (*p && !isspace((unsigned char)*p) && *p != '=' && *p != '>' && *p != '/') p++;
    size_t aname_len = p - aname_start;
    char *aname = (char *)malloc(aname_len + 1);
    if (!aname) break;
    memcpy(aname, aname_start, aname_len);
    aname[aname_len] = '\0';

    p = _xml_skip_ws(p);
    if (*p == '=') {
      p++;
      p = _xml_skip_ws(p);
      if (*p == '"' || *p == '\'') {
        char q = *p++;
        const char *vstart = p;
        while (*p && *p != q) p++;
        char *val = _xml_unescape(vstart, p - vstart);
        if (*p == q) p++;
        _xml_add_attribute(elem, aname, val);
        free(val);
      } else {
        const char *vstart = p;
        while (*p && !isspace((unsigned char)*p) && *p != '>' && *p != '/') p++;
        char *val = _xml_unescape(vstart, p - vstart);
        _xml_add_attribute(elem, aname, val);
        free(val);
      }
    } else {
      _xml_add_attribute(elem, aname, "");
    }
    free(aname);
  }

  p = _xml_skip_ws(p);
  if (*p == '/') {
    p++;
    p = _xml_skip_ws(p);
    if (*p == '>') p++;
    *ptr = p;
    return elem;
  }
  if (*p == '>') p++;

  /* Parse children and body */
  while (*p) {
    p = _xml_skip_ws(p);
    if (!*p) break;
    if (*p == '<') {
      if (p[1] == '/') {
        p += 2;
        p = _xml_skip_ws(p);
        while (*p && *p != '>') p++;
        if (*p == '>') p++;
        *ptr = p;
        return elem;
      }
      if (strncmp(p, "<!--", 4) == 0) {
        const char *end = strstr(p, "-->");
        if (end) p = end + 3; else { *ptr = ""; break; }
        continue;
      }
      if (strncmp(p, "<?", 2) == 0) {
        const char *end = strstr(p, "?>");
        if (end) p = end + 2; else { *ptr = ""; break; }
        continue;
      }
      if (strncmp(p, "<![CDATA[", 9) == 0) {
        p += 9;
        const char *cdata_end = strstr(p, "]]>");
        size_t clen = cdata_end ? (size_t)(cdata_end - p) : strlen(p);
        _xml_append_content(elem, p, clen);
        if (cdata_end) p = cdata_end + 3; else p += clen;
        continue;
      }
      XMLElement child = _xml_parse_node(&p);
      if (child) _xml_add_child(elem, child);
    } else {
      const char *tstart = p;
      while (*p && *p != '<') p++;
      size_t tlen = p - tstart;
      if (!_xml_is_all_ws(tstart, tlen)) {
        /* Trim leading and trailing whitespace */
        const char *ts = tstart;
        const char *te = tstart + tlen;
        while (ts < te && ((unsigned char)*ts <= 32)) ts++;
        while (te > ts && ((unsigned char)*(te - 1) <= 32)) te--;
        char *unesc = _xml_unescape(ts, te - ts);
        if (unesc) {
          _xml_append_content(elem, unesc, strlen(unesc));
          free(unesc);
        }
      }
    }
  }

  *ptr = p;
  return elem;
}

static inline const char *XMLElement_getName(XMLElement xml) {
  return (xml && xml->name) ? xml->name : "";
}

static inline const char *XMLElement_getContent(XMLElement xml) {
  return (xml && xml->content) ? xml->content : "";
}

static inline int XMLElement_getChildCount(XMLElement xml) {
  return xml ? xml->childCount : 0;
}

static inline XMLElement XMLElement_getChild_idx(XMLElement xml, int idx) {
  if (!xml || idx < 0 || idx >= xml->childCount) return NULL;
  return xml->children[idx];
}

static inline XMLElement XMLElement_getChild_name(XMLElement xml, const char *name) {
  if (!xml || !name) return NULL;
  for (int i = 0; i < xml->childCount; i++) {
    if (xml->children[i] && xml->children[i]->name && strcmp(xml->children[i]->name, name) == 0) {
      return xml->children[i];
    }
  }
  return NULL;
}

static inline XMLElement (XMLElement_getChild)(XMLElement xml, const char *name) {
  return XMLElement_getChild_name(xml, name);
}

#define XMLElement_getChild(xml, target) _Generic((target), \
  int: XMLElement_getChild_idx, \
  short: XMLElement_getChild_idx, \
  long: XMLElement_getChild_idx, \
  char *: XMLElement_getChild_name, \
  const char *: XMLElement_getChild_name, \
  default: (XMLElement_getChild) \
)((xml), (target))

static inline const char *XMLElement_getStringAttribute(XMLElement xml, const char *name) {
  if (!xml || !name) return NULL;
  for (int i = 0; i < xml->attributeCount; i++) {
    if (xml->attributes[i].name && strcmp(xml->attributes[i].name, name) == 0) {
      return xml->attributes[i].value;
    }
  }
  return NULL;
}

static inline int XMLElement_getIntAttribute(XMLElement xml, const char *name) {
  const char *v = XMLElement_getStringAttribute(xml, name);
  return v ? atoi(v) : 0;
}

static inline float XMLElement_getFloatAttribute(XMLElement xml, const char *name) {
  const char *v = XMLElement_getStringAttribute(xml, name);
  return v ? (float)atof(v) : 0.0f;
}

static inline void XMLElement_free(XMLElement xml) {
  if (!xml) return;
  for (int i = 0; i < xml->childCount; i++) {
    XMLElement_free(xml->children[i]);
  }
  free(xml->children);
  for (int i = 0; i < xml->attributeCount; i++) {
    free(xml->attributes[i].name);
    free(xml->attributes[i].value);
  }
  free(xml->attributes);
  free(xml->name);
  free(xml->content);
  free(xml);
}

static inline XMLElement XMLElement_ctor_impl(void *parent, const char *path_or_str) {
  (void)parent;
  if (!path_or_str) return NULL;
  const char *p = _xml_skip_ws(path_or_str);
  char *file_buf = NULL;
  if (*p != '<') {
    FILE *fp = fopen(path_or_str, "rb");
    if (!fp) {
      char dpath[512];
      snprintf(dpath, sizeof(dpath), "data/%s", path_or_str);
      fp = fopen(dpath, "rb");
    }
    if (fp) {
      fseek(fp, 0, SEEK_END);
      long sz = ftell(fp);
      fseek(fp, 0, SEEK_SET);
      if (sz >= 0) {
        file_buf = (char *)malloc(sz + 1);
        if (file_buf) {
          size_t rd = fread(file_buf, 1, sz, fp);
          file_buf[rd] = '\0';
          p = file_buf;
        }
      }
      fclose(fp);
    }
  }
  const char *cursor = p;
  XMLElement root = _xml_parse_node(&cursor);
  if (file_buf) free(file_buf);
  return root;
}

static inline XMLElement XMLElement_ctor1(const char *path_or_str) {
  return XMLElement_ctor_impl(NULL, path_or_str);
}
static inline XMLElement XMLElement_ctor2(void *parent, const char *path_or_str) {
  return XMLElement_ctor_impl(parent, path_or_str);
}

#define _PDE_XML_CHOOSER(_1, _2, NAME, ...) NAME
#define XMLElement_ctor(...) _PDE_XML_CHOOSER(__VA_ARGS__, XMLElement_ctor2, XMLElement_ctor1)(__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* _PDE_LIB_XML_H */
