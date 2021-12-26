//========================================================================
//
// pdfimages.cc
//
// Copyright 1998-2003 Glyph & Cog, LLC
//
//========================================================================

#include <aconf.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include "gmem.h"
#include "gmempp.h"
#include "parseargs.h"
#include "GString.h"
#include "GlobalParams.h"
#include "Object.h"
#include "Stream.h"
#include "Array.h"
#include "Dict.h"
#include "XRef.h"
#include "Catalog.h"
#include "Page.h"
#include "PDFDoc.h"
#include "ImageOutputDev.h"
#include "CharTypes.h"
#include "UnicodeMap.h"
#include "TextString.h"
#include "UTF8.h"
#include "Zoox.h"
#include "Error.h"
#include "config.h"

static void printInfoString(Object* infoDict, const char* infoKey,
    ZxDoc* xmp, const char* xmpKey1,
    const char* xmpKey2,
    const char* text, GBool parseDate,
    UnicodeMap* uMap);
static GString* parseInfoDate(GString* s);
static GString* parseXMPDate(GString* s);

static int firstPage = 1;
static int lastPage = 0;
static GBool dumpJPEG = gFalse;
static GBool dumpRaw = gFalse;
static GBool list = gFalse;
static char ownerPassword[33] = "\001";
static char userPassword[33] = "\001";
static GBool quiet = gFalse;
static char cfgFileName[256] = "";
static GBool printVersion = gFalse;
static GBool printHelp = gFalse;

static ArgDesc argDesc[] = {
  {"-f",      argInt,      &firstPage,     0,
   "first page to convert"},
  {"-l",      argInt,      &lastPage,      0,
   "last page to convert"},
  {"-j",      argFlag,     &dumpJPEG,      0,
   "write JPEG images as JPEG files"},
  {"-raw",    argFlag,     &dumpRaw,       0,
   "write raw data in PDF-native formats"},
  {"-list",   argFlag,     &list,          0,
   "write information to stdout for each image"},
  {"-opw",    argString,   ownerPassword,  sizeof(ownerPassword),
   "owner password (for encrypted files)"},
  {"-upw",    argString,   userPassword,   sizeof(userPassword),
   "user password (for encrypted files)"},
  {"-q",      argFlag,     &quiet,         0,
   "don't print any messages or errors"},
  {"-cfg",        argString,      cfgFileName,    sizeof(cfgFileName),
   "configuration file to use in place of .xpdfrc"},
  {"-v",      argFlag,     &printVersion,  0,
   "print copyright and version info"},
  {"-h",      argFlag,     &printHelp,     0,
   "print usage information"},
  {"-help",   argFlag,     &printHelp,     0,
   "print usage information"},
  {"--help",  argFlag,     &printHelp,     0,
   "print usage information"},
  {"-?",      argFlag,     &printHelp,     0,
   "print usage information"},
  {NULL}
};

int main(int argc, char *argv[]) {
  PDFDoc *doc;
  char *fileName;
  char *imgRoot;
  GString *ownerPW, *userPW;
  ImageOutputDev *imgOut;
  GBool ok;
  int exitCode;
  ZxDoc* xmp;
  GString* metadata;
  Object info, xfa;
  Object* acroForm;
  CryptAlgorithm encAlgorithm;
  int permFlags, keyLength, encVersion;
  UnicodeMap* uMap;
  GBool rawDates = gFalse;
  GBool ownerPasswordOk;

  exitCode = 99;

  // parse args
  fixCommandLine(&argc, &argv);
  ok = parseArgs(argDesc, &argc, argv);
  if (!ok || argc != 3 || printVersion || printHelp) {
    fprintf(stderr, "pdfimages version %s [www.xpdfreader.com]\n", xpdfVersion);
    fprintf(stderr, "%s\n", xpdfCopyright);
    if (!printVersion) {
      printUsage("pdfimages", "<PDF-file> <image-root>", argDesc);
    }
    goto err0;
  }
  fileName = argv[1];
  imgRoot = argv[2];

  // read config file
  globalParams = new GlobalParams(cfgFileName);
  if (quiet) {
    globalParams->setErrQuiet(quiet);
  }

  // get mapping to output encoding
  if (!(uMap = globalParams->getTextEncoding())) {
      error(errConfig, -1, "Couldn't get text encoding");
      goto err2;
  }

  // open PDF file
  if (ownerPassword[0] != '\001') {
    ownerPW = new GString(ownerPassword);
  } else {
    ownerPW = NULL;
  }
  if (userPassword[0] != '\001') {
    userPW = new GString(userPassword);
  } else {
    userPW = NULL;
  }
  doc = new PDFDoc(fileName, ownerPW, userPW);
  if (userPW) {
    delete userPW;
  }
  if (ownerPW) {
    delete ownerPW;
  }
  if (!doc->isOk()) {
    exitCode = 1;
    goto err1;
  }

  // check for copy permission
  if (!doc->okToCopy()) {
    error(errNotAllowed, -1,
	  "Copying of images from this document is not allowed.");
    //exitCode = 3;
    //goto err1;
  }

  // get page range
  if (firstPage < 1)
    firstPage = 1;
  if (lastPage < 1 || lastPage > doc->getNumPages())
    lastPage = doc->getNumPages();

  // print doc info
  doc->getDocInfo(&info);
  if ((metadata = doc->readMetadata())) {
      xmp = ZxDoc::loadMem(metadata->getCString(), metadata->getLength());
  }
  else {
      xmp = NULL;
  }
  printInfoString(&info, "Title", xmp, "dc:title", NULL, "Title:          ", gFalse, uMap);
  printInfoString(&info, "Subject", xmp, "dc:description", NULL, "Subject:        ", gFalse, uMap);
  printInfoString(&info, "Keywords", xmp, "pdf:Keywords", NULL, "Keywords:       ", gFalse, uMap);
  printInfoString(&info, "Author", xmp, "dc:creator", NULL, "Author:         ", gFalse, uMap);
  printInfoString(&info, "Creator", xmp, "xmp:CreatorTool", NULL, "Creator:        ", gFalse, uMap);
  printInfoString(&info, "Producer", xmp, "pdf:Producer", NULL, "Producer:       ", gFalse, uMap);
  printInfoString(&info, "CreationDate", xmp, "xap:CreateDate", "xmp:CreateDate", "CreationDate:   ", !rawDates, uMap);
  printInfoString(&info, "ModDate", xmp, "xap:ModifyDate", "xmp:ModifyDate", "ModDate:        ", !rawDates, uMap);
  info.free();
  if (xmp) {
      delete xmp;
  }

  // print tagging info
  printf("Tagged:         %s\n",
      doc->getStructTreeRoot()->isDict() ? "yes" : "no");

  // print form info
  if ((acroForm = doc->getCatalog()->getAcroForm())->isDict()) {
      acroForm->dictLookup("XFA", &xfa);
      if (xfa.isStream() || xfa.isArray()) {
          if (doc->getCatalog()->getNeedsRendering()) {
              printf("Form:           dynamic XFA\n");
          }
          else {
              printf("Form:           static XFA\n");
          }
      }
      else {
          printf("Form:           AcroForm\n");
      }
      xfa.free();
  }
  else {
      printf("Form:           none\n");
  }

  // print page count
  printf("Pages:          %d\n", doc->getNumPages());

  // print encryption info
  if (doc->isEncrypted()) {
      doc->getXRef()->getEncryption(&permFlags, &ownerPasswordOk, &keyLength,
          &encVersion, &encAlgorithm);
      printf("Encrypted:      %s %d-bit\n",
          encAlgorithm == cryptRC4 ? "RC4" : "AES",
          keyLength * 8);
      printf("Permissions:    print:%s copy:%s change:%s addNotes:%s\n",
          doc->okToPrint(gTrue) ? "yes" : "no",
          doc->okToCopy(gTrue) ? "yes" : "no",
          doc->okToChange(gTrue) ? "yes" : "no",
          doc->okToAddNotes(gTrue) ? "yes" : "no");
  }
  else {
      printf("Encrypted:      no\n");
  }

  // print linearization info
  printf("Optimized:      %s\n", doc->isLinearized() ? "yes" : "no");

  // print PDF version
  printf("PDF version:    %.1f\n", doc->getPDFVersion());

  if (metadata) {
      delete metadata;
  }

  // write image files
  imgOut = new ImageOutputDev(imgRoot, dumpJPEG, dumpRaw, list);
  if (imgOut->isOk()) {
    doc->displayPages(imgOut, firstPage, lastPage, 72, 72, 0,
		      gFalse, gTrue, gFalse);
  }
  delete imgOut;

  exitCode = 0;

  // clean up
 err1:
  delete doc;
 err2:
  delete globalParams;
 err0:

  // check for memory leaks
  Object::memCheck(stderr);
  gMemReport(stderr);

  return exitCode;
}

static void printInfoString(Object* infoDict, const char* infoKey,
    ZxDoc* xmp, const char* xmpKey1,
    const char* xmpKey2,
    const char* text, GBool parseDate,
    UnicodeMap* uMap) {
    Object obj;
    TextString* s;
    Unicode* u;
    Unicode uu;
    char buf[8];
    GString* value, * tmp;
    ZxElement* rdf, * elem, * child;
    ZxNode* node, * node2;
    int i, n;

    value = NULL;

    //-- check the info dictionary
    if (infoDict->isDict()) {
        if (infoDict->dictLookup(infoKey, &obj)->isString()) {
            if (!parseDate || !(value = parseInfoDate(obj.getString()))) {
                s = new TextString(obj.getString());
                u = s->getUnicode();
                value = new GString();
                for (i = 0; i < s->getLength(); ++i) {
                    n = uMap->mapUnicode(u[i], buf, sizeof(buf));
                    value->append(buf, n);
                }
                delete s;
            }
        }
        obj.free();
    }

    //-- check the XMP metadata
    if (xmp) {
        rdf = xmp->getRoot();
        if (rdf->isElement("x:xmpmeta")) {
            rdf = rdf->findFirstChildElement("rdf:RDF");
        }
        if (rdf && rdf->isElement("rdf:RDF")) {
            for (node = rdf->getFirstChild(); node; node = node->getNextChild()) {
                if (node->isElement("rdf:Description")) {
                    if (!(elem = node->findFirstChildElement(xmpKey1)) && xmpKey2) {
                        elem = node->findFirstChildElement(xmpKey2);
                    }
                    if (elem) {
                        if ((child = elem->findFirstChildElement("rdf:Alt")) ||
                            (child = elem->findFirstChildElement("rdf:Seq"))) {
                            if ((node2 = child->findFirstChildElement("rdf:li"))) {
                                node2 = node2->getFirstChild();
                            }
                        }
                        else {
                            node2 = elem->getFirstChild();
                        }
                        if (node2 && node2->isCharData()) {
                            if (value) {
                                delete value;
                            }
                            if (!parseDate ||
                                !(value = parseXMPDate(((ZxCharData*)node2)->getData()))) {
                                tmp = ((ZxCharData*)node2)->getData();
                                int i = 0;
                                value = new GString();
                                while (getUTF8(tmp, &i, &uu)) {
                                    n = uMap->mapUnicode(uu, buf, sizeof(buf));
                                    value->append(buf, n);
                                }
                            }
                        }
                        break;
                    }
                }
            }
        }
    }

    if (value) {
        fputs(text, stdout);
        fwrite(value->getCString(), 1, value->getLength(), stdout);
        fputc('\n', stdout);
        delete value;
    }
}

static GString* parseInfoDate(GString* s) {
    int year, mon, day, hour, min, sec, n;
    struct tm tmStruct;
    char buf[256];
    char* p;

    p = s->getCString();
    if (p[0] == 'D' && p[1] == ':') {
        p += 2;
    }
    if ((n = sscanf(p, "%4d%2d%2d%2d%2d%2d",
        &year, &mon, &day, &hour, &min, &sec)) < 1) {
        return NULL;
    }
    switch (n) {
    case 1: mon = 1;
    case 2: day = 1;
    case 3: hour = 0;
    case 4: min = 0;
    case 5: sec = 0;
    }
    tmStruct.tm_year = year - 1900;
    tmStruct.tm_mon = mon - 1;
    tmStruct.tm_mday = day;
    tmStruct.tm_hour = hour;
    tmStruct.tm_min = min;
    tmStruct.tm_sec = sec;
    tmStruct.tm_wday = -1;
    tmStruct.tm_yday = -1;
    tmStruct.tm_isdst = -1;
    // compute the tm_wday and tm_yday fields
    if (!(mktime(&tmStruct) != (time_t)-1 &&
        strftime(buf, sizeof(buf), "%c", &tmStruct))) {
        return NULL;
    }
    return new GString(buf);
}

static GString* parseXMPDate(GString* s) {
    int year, mon, day, hour, min, sec, tz;
    struct tm tmStruct;
    char buf[256];
    char* p;

    p = s->getCString();
    if (isdigit(p[0]) && isdigit(p[1]) && isdigit(p[2]) && isdigit(p[3])) {
        buf[0] = p[0];
        buf[1] = p[1];
        buf[2] = p[2];
        buf[3] = p[3];
        buf[4] = '\0';
        year = atoi(buf);
        p += 4;
    }
    else {
        return NULL;
    }
    mon = day = 1;
    hour = min = sec = 0;
    tz = 2000;
    if (p[0] == '-' && isdigit(p[1]) && isdigit(p[2])) {
        buf[0] = p[1];
        buf[1] = p[2];
        buf[2] = '\0';
        mon = atoi(buf);
        p += 3;
        if (p[0] == '-' && isdigit(p[1]) && isdigit(p[2])) {
            buf[0] = p[1];
            buf[1] = p[2];
            buf[2] = '\0';
            day = atoi(buf);
            p += 3;
            if (p[0] == 'T' && isdigit(p[1]) && isdigit(p[2]) &&
                p[3] == ':' && isdigit(p[4]) && isdigit(p[5])) {
                buf[0] = p[1];
                buf[1] = p[2];
                buf[2] = '\0';
                hour = atoi(buf);
                buf[0] = p[4];
                buf[1] = p[5];
                buf[2] = '\0';
                min = atoi(buf);
                p += 6;
                if (p[0] == ':' && isdigit(p[1]) && isdigit(p[2])) {
                    buf[0] = p[1];
                    buf[1] = p[2];
                    buf[2] = '\0';
                    sec = atoi(buf);
                    if (p[0] == '.' && isdigit(p[1])) {
                        p += 2;
                    }
                }
                if ((p[0] == '+' || p[0] == '-') &&
                    isdigit(p[1]) && isdigit(p[2]) && p[3] == ':' &&
                    isdigit(p[4]) && isdigit(p[5])) {
                    buf[0] = p[1];
                    buf[1] = p[2];
                    buf[2] = '\0';
                    tz = atoi(buf);
                    buf[0] = p[4];
                    buf[1] = p[5];
                    buf[2] = '\0';
                    tz = tz * 60 + atoi(buf);
                    tz = tz * 60;
                    if (p[0] == '-') {
                        tz = -tz;
                    }
                }
            }
        }
    }

    tmStruct.tm_year = year - 1900;
    tmStruct.tm_mon = mon - 1;
    tmStruct.tm_mday = day;
    tmStruct.tm_hour = hour;
    tmStruct.tm_min = min;
    tmStruct.tm_sec = sec;
    tmStruct.tm_wday = -1;
    tmStruct.tm_yday = -1;
    tmStruct.tm_isdst = -1;
    // compute the tm_wday and tm_yday fields
    //~ this ignores the timezone
    if (!(mktime(&tmStruct) != (time_t)-1 &&
        strftime(buf, sizeof(buf), "%c", &tmStruct))) {
        return NULL;
    }
    return new GString(buf);
}
