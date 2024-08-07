/******************************************************************************
 *   "Gif-Lib" - Yet another gif library.
 *
 * Written by:  Gershon Elber                Ver 1.1, Aug. 1990
 ******************************************************************************
 * The kernel of the GIF Encoding process can be found here.
 ******************************************************************************
 * History:
 * 14 Jun 89 - Version 1.0 by Gershon Elber.
 *  3 Sep 90 - Version 1.1 by Gershon Elber (Support for Gif89, Unique names).
 * 26 Jun 96 - Version 3.0 by Eric S. Raymond (Full GIF89 support)
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* Find a thirty-two bit int type */
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#ifdef __MSDOS__
#include <io.h>
#include <alloc.h>
#include <sys\stat.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#ifdef R6000

/* FIXME: What is sys/mode.h?  Can we substitute a check for this file rather
 * than a check based on machine type?
 */
#include <sys/mode.h>
#endif
#endif /* __MSDOS__ */

#ifdef HAVE_IO_H
#include <io.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif /* HAVE_FCNTL_H */
#ifdef _WIN32
  #ifdef HAVE_UNISTD_H
  #include <unistd.h>
  #endif /* HAVE_UNISTD_H */
#else
  #include <unistd.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "gif_lib.h"
#include "gif_lib_private.h"

/* #define DEBUG_NO_PREFIX                  Dump only compressed data. */

/* Masks given codes to BitsPerPixel, to make sure all codes are in range: */
static GifPixelType CodeMask[] = {
    0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff
};

static char GifVersionPrefix[GIF_STAMP_LEN + 1] = GIF87_STAMP;

#define WRITE(_gif,_buf,_len)   \
  (((GifFilePrivateType*)_gif->Private)->Write ?    \
   ((GifFilePrivateType*)_gif->Private)->Write(_gif,_buf,_len) :    \
   fwrite(_buf, 1, _len, ((GifFilePrivateType*)_gif->Private)->File))

static int EGifPutWord(int Word, GifFileType * GifFile);
static int EGifSetupCompress(GifFileType * GifFile,
                             GifCodeStruct * g, GifDiffStruct * gd // schnaader
                             );
static int EGifCompressLine(GifFileType * GifFile, GifPixelType * Line,
                            GifCodeStruct * g, GifDiffStruct * gd, // schnaader
                            int LineLen);
static int EGifCompressOutput(GifFileType * GifFile, int Code);
static int EGifBufferedOutput(GifFileType * GifFile, GifByteType * Buf,
                              int c);

void ClearCodeTable(int* ct) {
  int i;

  for (i = 0; i < 1024*1024; i++) {
    ct[i] = -1;
  }
}


/******************************************************************************
 * Open a new gif file for write, given by its name. If TestExistance then
 * if the file exists this routines fails (returns NULL).
 * Returns GifFileType pointer dynamically allocated which serves as the gif
 * info record. _GifError is cleared if succesfull.
 *****************************************************************************/
GifFileType *
EGifOpenFileName(const char *FileName,
                 int TestExistance) {

    int FileHandle;
    GifFileType *GifFile;

    if (TestExistance)
        #ifdef _WIN32
        FileHandle = open(FileName, O_WRONLY | O_CREAT | O_EXCL | O_BINARY
        #else
        // TODO: Does this work?
        FileHandle = open(FileName, O_WRONLY | O_CREAT | O_EXCL
        #endif
                          , S_IREAD | S_IWRITE);
    else
        #ifdef _WIN32
        FileHandle = open(FileName, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY 
        #else
        // TODO: Does this work?
        FileHandle = open(FileName, O_WRONLY | O_CREAT | O_TRUNC
        #endif
                          , S_IREAD | S_IWRITE);

    if (FileHandle == -1) {
        _GifError = E_GIF_ERR_OPEN_FAILED;
        return NULL;
    }
    GifFile = EGifOpenFileHandle(FileHandle);
    if (GifFile == (GifFileType *) NULL)
        close(FileHandle);
    return GifFile;
}

/******************************************************************************
 * Update a new gif file, given its file handle, which must be opened for
 * write in binary mode.
 * Returns GifFileType pointer dynamically allocated which serves as the gif
 * info record. _GifError is cleared if succesfull.
 *****************************************************************************/
GifFileType *
EGifOpenFileHandle(int FileHandle) {

    GifFileType *GifFile;
    GifFilePrivateType *Private;
    FILE *f;

    GifFile = (GifFileType *) malloc(sizeof(GifFileType));
    if (GifFile == NULL) {
        _GifError = E_GIF_ERR_NOT_ENOUGH_MEM;
        return NULL;
    }

    memset(GifFile, '\0', sizeof(GifFileType));

    Private = (GifFilePrivateType *)malloc(sizeof(GifFilePrivateType));
    if (Private == NULL) {
        free(GifFile);
        _GifError = E_GIF_ERR_NOT_ENOUGH_MEM;
        return NULL;
    }
    if ((Private->CodeTable = (int*)malloc(1024*1024*sizeof(int))) == NULL) {
        free(GifFile);
        free(Private);
        _GifError = E_GIF_ERR_NOT_ENOUGH_MEM;
        return NULL;
    }

    #ifdef _WIN32
    setmode(FileHandle, O_BINARY);    /* Make sure it is in binary mode. */
    #endif

    f = fdopen(FileHandle, "wb");    /* Make it into a stream: */

#ifdef __MSDOS__
    setvbuf(f, NULL, _IOFBF, GIF_FILE_BUFFER_SIZE);    /* And inc. stream
                                                        * buffer. */
#endif /* __MSDOS__ */

    GifFile->Private = (VoidPtr)Private;
    Private->FileHandle = FileHandle;
    Private->File = f;
    Private->FileState = FILE_STATE_WRITE;

    Private->Write = (OutputFunc) 0;    /* No user write routine (MRB) */
    GifFile->UserData = (VoidPtr) 0;    /* No user write handle (MRB) */

    _GifError = 0;

    return GifFile;
}

/******************************************************************************
 * Output constructor that takes user supplied output function.
 * Basically just a copy of EGifOpenFileHandle. (MRB)
 *****************************************************************************/
GifFileType *
EGifOpen(void *userData,
         OutputFunc writeFunc) {

    GifFileType *GifFile;
    GifFilePrivateType *Private;

    GifFile = (GifFileType *)malloc(sizeof(GifFileType));
    if (GifFile == NULL) {
        _GifError = E_GIF_ERR_NOT_ENOUGH_MEM;
        return NULL;
    }

    memset(GifFile, '\0', sizeof(GifFileType));

    Private = (GifFilePrivateType *)malloc(sizeof(GifFilePrivateType));
    if (Private == NULL) {
        free(GifFile);
        _GifError = E_GIF_ERR_NOT_ENOUGH_MEM;
        return NULL;
    }

    if ((Private->CodeTable = (int*)malloc(1024*1024*sizeof(int))) == NULL) {
        free (GifFile);
        free (Private);
        _GifError = E_GIF_ERR_NOT_ENOUGH_MEM;
        return NULL;
    }

    GifFile->Private = (VoidPtr) Private;
    Private->FileHandle = 0;
    Private->File = (FILE *) 0;
    Private->FileState = FILE_STATE_WRITE;

    Private->Write = writeFunc;    /* User write routine (MRB) */
    GifFile->UserData = userData;    /* User write handle (MRB) */

    _GifError = 0;

    return GifFile;
}

/******************************************************************************
 * Routine to set current GIF version. All files open for write will be
 * using this version until next call to this routine. Version consists of
 * 3 characters as "87a" or "89a". No test is made to validate the version.
 *****************************************************************************/
void
EGifSetGifVersion(const char *Version) {
    strncpy(GifVersionPrefix + GIF_VERSION_POS, Version, 3);
}

/******************************************************************************
 * This routine should be called before any other EGif calls, immediately
 * follows the GIF file openning.
 *****************************************************************************/
int
EGifPutScreenDesc(GifFileType * GifFile,
                  int Width,
                  int Height,
                  int ColorRes,
                  int BackGround,
                  int PixelAspectRatio,
                  const ColorMapObject * ColorMap) {

    int i;
    GifByteType Buf[3];
    GifFilePrivateType *Private = (GifFilePrivateType *) GifFile->Private;

    if (Private->FileState & FILE_STATE_SCREEN) {
        /* If already has screen descriptor - something is wrong! */
        _GifError = E_GIF_ERR_HAS_SCRN_DSCR;
        return GIF_ERROR;
    }
    if (!IS_WRITEABLE(Private)) {
        /* This file was NOT open for writing: */
        _GifError = E_GIF_ERR_NOT_WRITEABLE;
        return GIF_ERROR;
    }

/* First write the version prefix into the file. */
#ifndef DEBUG_NO_PREFIX
    if (WRITE(GifFile, (unsigned char *)GifVersionPrefix,
              strlen(GifVersionPrefix)) != strlen(GifVersionPrefix)) {
        _GifError = E_GIF_ERR_WRITE_FAILED;
        return GIF_ERROR;
    }
#endif /* DEBUG_NO_PREFIX */

    GifFile->SWidth = Width;
    GifFile->SHeight = Height;
    GifFile->SColorResolution = ColorRes;
    GifFile->SBackGroundColor = BackGround;

    GifFile->SPixelAspectRatio = PixelAspectRatio; // schnaader

    if (ColorMap) {
        GifFile->SColorMap = MakeMapObject(ColorMap->ColorCount,
                                           ColorMap->Colors);
        if (GifFile->SColorMap == NULL) {
            _GifError = E_GIF_ERR_NOT_ENOUGH_MEM;
            return GIF_ERROR;
        }
    } else
        GifFile->SColorMap = NULL;

    /*
     * Put the logical screen descriptor into the file:
     */
    /* Logical Screen Descriptor: Dimensions */
    EGifPutWord(Width, GifFile);
    EGifPutWord(Height, GifFile);

    /* Logical Screen Descriptor: Packed Fields */
    /* Note: We have actual size of the color table default to the largest
     * possible size (7+1 == 8 bits) because the decoder can use it to decide
     * how to display the files.
     */
    Buf[0] = (ColorMap ? 0x80 : 0x00) | /* Yes/no global colormap */
             ((ColorRes - 1) << 4) | /* Bits allocated to each primary color */
        (ColorMap ? ColorMap->BitsPerPixel - 1 : 0x07 ); /* Actual size of the
                                                            color table. */
    Buf[1] = BackGround;    /* Index into the ColorTable for background color */
    //Buf[2] = 0;             /* Pixel Aspect Ratio */

    Buf[2] = PixelAspectRatio;             // schnaader

#ifndef DEBUG_NO_PREFIX
    WRITE(GifFile, Buf, 3);
#endif /* DEBUG_NO_PREFIX */

    /* If we have Global color map - dump it also: */
#ifndef DEBUG_NO_PREFIX
    if (ColorMap != NULL)
        for (i = 0; i < ColorMap->ColorCount; i++) {
            /* Put the ColorMap out also: */
            Buf[0] = ColorMap->Colors[i].Red;
            Buf[1] = ColorMap->Colors[i].Green;
            Buf[2] = ColorMap->Colors[i].Blue;
            if (WRITE(GifFile, Buf, 3) != 3) {
                _GifError = E_GIF_ERR_WRITE_FAILED;
                return GIF_ERROR;
            }
        }
#endif /* DEBUG_NO_PREFIX */

    /* Mark this file as has screen descriptor, and no pixel written yet: */
    Private->FileState |= FILE_STATE_SCREEN;

    return GIF_OK;
}

/******************************************************************************
 * This routine should be called before any attempt to dump an image - any
 * call to any of the pixel dump routines.
 *****************************************************************************/
int
EGifPutImageDesc(GifFileType * GifFile,
                 GifCodeStruct * g,  // schnaader
                 GifDiffStruct * gd, // schnaader
                 int Left,
                 int Top,
                 int Width,
                 int Height,
                 int Interlace,
                 const ColorMapObject * ColorMap) {

    int i;
    GifByteType Buf[3];
    GifFilePrivateType *Private = (GifFilePrivateType *)GifFile->Private;

    if (Private->FileState & FILE_STATE_IMAGE &&
#if defined(__MSDOS__) || defined(__GNUC__)
        Private->PixelCount > 0xffff0000UL) {
#else
        Private->PixelCount > 0xffff0000) {
#endif /* __MSDOS__ */
        /* If already has active image descriptor - something is wrong! */
        _GifError = E_GIF_ERR_HAS_IMAG_DSCR;
        return GIF_ERROR;
    }
    if (!IS_WRITEABLE(Private)) {
        /* This file was NOT open for writing: */
        _GifError = E_GIF_ERR_NOT_WRITEABLE;
        return GIF_ERROR;
    }
    GifFile->Image.Left = Left;
    GifFile->Image.Top = Top;
    GifFile->Image.Width = Width;
    GifFile->Image.Height = Height;
    GifFile->Image.Interlace = Interlace;
    if (ColorMap) {
        GifFile->Image.ColorMap = MakeMapObject(ColorMap->ColorCount,
                                                ColorMap->Colors);
        if (GifFile->Image.ColorMap == NULL) {
            _GifError = E_GIF_ERR_NOT_ENOUGH_MEM;
            return GIF_ERROR;
        }
    } else {
        GifFile->Image.ColorMap = NULL;
    }

    /* Put the image descriptor into the file: */
    Buf[0] = ',';    /* Image seperator character. */
#ifndef DEBUG_NO_PREFIX
    WRITE(GifFile, Buf, 1);
#endif /* DEBUG_NO_PREFIX */
    EGifPutWord(Left, GifFile);
    EGifPutWord(Top, GifFile);
    EGifPutWord(Width, GifFile);
    EGifPutWord(Height, GifFile);
    Buf[0] = (ColorMap ? 0x80 : 0x00) |
       (Interlace ? 0x40 : 0x00) |
       (ColorMap ? ColorMap->BitsPerPixel - 1 : 0);
#ifndef DEBUG_NO_PREFIX
    WRITE(GifFile, Buf, 1);
#endif /* DEBUG_NO_PREFIX */

    /* If we have Global color map - dump it also: */
#ifndef DEBUG_NO_PREFIX
    if (ColorMap != NULL)
        for (i = 0; i < ColorMap->ColorCount; i++) {
            /* Put the ColorMap out also: */
            Buf[0] = ColorMap->Colors[i].Red;
            Buf[1] = ColorMap->Colors[i].Green;
            Buf[2] = ColorMap->Colors[i].Blue;
            if (WRITE(GifFile, Buf, 3) != 3) {
                _GifError = E_GIF_ERR_WRITE_FAILED;
                return GIF_ERROR;
            }
        }
#endif /* DEBUG_NO_PREFIX */
    if (GifFile->SColorMap == NULL && GifFile->Image.ColorMap == NULL) {
        _GifError = E_GIF_ERR_NO_COLOR_MAP;
        return GIF_ERROR;
    }

    /* Mark this file as has screen descriptor: */
    Private->FileState |= FILE_STATE_IMAGE;
    Private->PixelCount = (long)Width *(long)Height;

    EGifSetupCompress(GifFile, g, gd);    /* Reset compress algorithm parameters. */

    return GIF_OK;
}

/******************************************************************************
 * Put one full scanned line (Line) of length LineLen into GIF file.
 *****************************************************************************/
int
EGifPutLine(GifFileType * GifFile,
            GifPixelType * Line,
            GifCodeStruct * g,  // schnaader
            GifDiffStruct * gd, // schnaader
            int LineLen) {

    int i;
    GifPixelType Mask;
    GifFilePrivateType *Private = (GifFilePrivateType *) GifFile->Private;

    if (!IS_WRITEABLE(Private)) {
        /* This file was NOT open for writing: */
        _GifError = E_GIF_ERR_NOT_WRITEABLE;
        return GIF_ERROR;
    }

    if (!LineLen)
        LineLen = GifFile->Image.Width;
    if (Private->PixelCount < (unsigned)LineLen) {
        _GifError = E_GIF_ERR_DATA_TOO_BIG;
        return GIF_ERROR;
    }
    Private->PixelCount -= LineLen;

    /* Make sure the codes are not out of bit range, as we might generate
     * wrong code (because of overflow when we combine them) in this case: */
    Mask = CodeMask[Private->BitsPerPixel];
    for (i = 0; i < LineLen; i++)
        Line[i] &= Mask;

    return EGifCompressLine(GifFile, Line, g, gd, LineLen);
}

/******************************************************************************
 * Put one pixel (Pixel) into GIF file.
 *****************************************************************************/
int
EGifPutPixel(GifFileType * GifFile,
             GifPixelType Pixel) {

    GifFilePrivateType *Private = (GifFilePrivateType *)GifFile->Private;

    if (!IS_WRITEABLE(Private)) {
        /* This file was NOT open for writing: */
        _GifError = E_GIF_ERR_NOT_WRITEABLE;
        return GIF_ERROR;
    }

    if (Private->PixelCount == 0) {
        _GifError = E_GIF_ERR_DATA_TOO_BIG;
        return GIF_ERROR;
    }
    --Private->PixelCount;

    /* Make sure the code is not out of bit range, as we might generate
     * wrong code (because of overflow when we combine them) in this case: */
    Pixel &= CodeMask[Private->BitsPerPixel];

    return EGifCompressLine(GifFile, &Pixel, NULL, NULL, 1);
}

/******************************************************************************
 * Put a comment into GIF file using the GIF89 comment extension block.
 *****************************************************************************/
int
EGifPutComment(GifFileType * GifFile,
               const char *Comment) {
  
    unsigned int length = strlen(Comment);
    char *buf;

    length = strlen(Comment);
    if (length <= 255) {
        return EGifPutExtension(GifFile, COMMENT_EXT_FUNC_CODE,
                                length, Comment);
    } else {
        buf = (char *)Comment;
        if (EGifPutExtensionFirst(GifFile, COMMENT_EXT_FUNC_CODE, 255, buf)
                == GIF_ERROR) {
            return GIF_ERROR;
        }
        length -= 255;
        buf = buf + 255;

        /* Break the comment into 255 byte sub blocks */
        while (length > 255) {
            if (EGifPutExtensionNext(GifFile, 0, 255, buf) == GIF_ERROR) {
                return GIF_ERROR;
            }
            buf = buf + 255;
            length -= 255;
        }
        /* Output any partial block and the clear code. */
        if (length > 0) {
            if (EGifPutExtensionLast(GifFile, 0, length, buf) == GIF_ERROR) {
                return GIF_ERROR;
            }
        } else {
            if (EGifPutExtensionLast(GifFile, 0, 0, NULL) == GIF_ERROR) {
                return GIF_ERROR;
            }
        }
    }
    return GIF_OK;
}

/******************************************************************************
 * Put a first extension block (see GIF manual) into gif file.  Here more
 * extensions can be dumped using EGifPutExtensionNext until
 * EGifPutExtensionLast is invoked.
 *****************************************************************************/
int
EGifPutExtensionFirst(GifFileType * GifFile,
                      int ExtCode,
                      int ExtLen,
                      const VoidPtr Extension) {

    GifByteType Buf[3];
    GifFilePrivateType *Private = (GifFilePrivateType *)GifFile->Private;

    if (!IS_WRITEABLE(Private)) {
        /* This file was NOT open for writing: */
        _GifError = E_GIF_ERR_NOT_WRITEABLE;
        return GIF_ERROR;
    }

    if (ExtCode == 0) {
        WRITE(GifFile, (GifByteType *)&ExtLen, 1);
    } else {
        Buf[0] = '!';
        Buf[1] = ExtCode;
        Buf[2] = ExtLen;
        WRITE(GifFile, Buf, 3);
    }

    WRITE(GifFile, (GifByteType*)Extension, ExtLen);

    return GIF_OK;
}

/******************************************************************************
 * Put a middle extension block (see GIF manual) into gif file.
 *****************************************************************************/
int
EGifPutExtensionNext(GifFileType * GifFile,
                     int ExtCode,
                     int ExtLen,
                     const VoidPtr Extension) {

    GifByteType Buf;
    GifFilePrivateType *Private = (GifFilePrivateType *)GifFile->Private;

    if (!IS_WRITEABLE(Private)) {
        /* This file was NOT open for writing: */
        _GifError = E_GIF_ERR_NOT_WRITEABLE;
        return GIF_ERROR;
    }

    Buf = ExtLen;
    WRITE(GifFile, &Buf, 1);
    WRITE(GifFile, (GifByteType*)Extension, ExtLen);

    return GIF_OK;
}

/******************************************************************************
 * Put a last extension block (see GIF manual) into gif file.
 *****************************************************************************/
int
EGifPutExtensionLast(GifFileType * GifFile,
                     int ExtCode,
                     int ExtLen,
                     const VoidPtr Extension) {

    GifByteType Buf;
    GifFilePrivateType *Private = (GifFilePrivateType *)GifFile->Private;

    if (!IS_WRITEABLE(Private)) {
        /* This file was NOT open for writing: */
        _GifError = E_GIF_ERR_NOT_WRITEABLE;
        return GIF_ERROR;
    }

    /* If we are given an extension sub-block output it now. */
    if (ExtLen > 0) {
        Buf = ExtLen;
        WRITE(GifFile, &Buf, 1);
        WRITE(GifFile, (GifByteType*)Extension, ExtLen);
    }

    /* Write the block terminator */
    Buf = 0;
    WRITE(GifFile, &Buf, 1);

    return GIF_OK;
}

/******************************************************************************
 * Put an extension block (see GIF manual) into gif file.
 * Warning: This function is only useful for Extension blocks that have at
 * most one subblock.  Extensions with more than one subblock need to use the
 * EGifPutExtension{First,Next,Last} functions instead.
 *****************************************************************************/
int
EGifPutExtension(GifFileType * GifFile,
                 int ExtCode,
                 int ExtLen,
                 const VoidPtr Extension) {

    GifByteType Buf[3];
    GifFilePrivateType *Private = (GifFilePrivateType *)GifFile->Private;

    if (!IS_WRITEABLE(Private)) {
        /* This file was NOT open for writing: */
        _GifError = E_GIF_ERR_NOT_WRITEABLE;
        return GIF_ERROR;
    }

    if (ExtCode == 0)
        WRITE(GifFile, (GifByteType *)&ExtLen, 1);
    else {
        Buf[0] = '!';       /* Extension Introducer 0x21 */
        Buf[1] = ExtCode;   /* Extension Label */
        Buf[2] = ExtLen;    /* Extension length */
        WRITE(GifFile, Buf, 3);
    }
    WRITE(GifFile, (GifByteType*)Extension, ExtLen);
    Buf[0] = 0;
    WRITE(GifFile, Buf, 1);

    return GIF_OK;
}

/******************************************************************************
 * Put the image code in compressed form. This routine can be called if the
 * information needed to be piped out as is. Obviously this is much faster
 * than decoding and encoding again. This routine should be followed by calls
 * to EGifPutCodeNext, until NULL block is given.
 * The block should NOT be freed by the user (not dynamically allocated).
 *****************************************************************************/
int
EGifPutCode(GifFileType * GifFile,
            int CodeSize,
            const GifByteType * CodeBlock) {

    GifFilePrivateType *Private = (GifFilePrivateType *)GifFile->Private;

    if (!IS_WRITEABLE(Private)) {
        /* This file was NOT open for writing: */
        _GifError = E_GIF_ERR_NOT_WRITEABLE;
        return GIF_ERROR;
    }

    /* No need to dump code size as Compression set up does any for us: */
    /* 
     * Buf = CodeSize;
     * if (WRITE(GifFile, &Buf, 1) != 1) {
     *      _GifError = E_GIF_ERR_WRITE_FAILED;
     *      return GIF_ERROR;
     * }
     */

    return EGifPutCodeNext(GifFile, CodeBlock);
}

/******************************************************************************
 * Continue to put the image code in compressed form. This routine should be
 * called with blocks of code as read via DGifGetCode/DGifGetCodeNext. If
 * given buffer pointer is NULL, empty block is written to mark end of code.
 *****************************************************************************/
int
EGifPutCodeNext(GifFileType * GifFile,
                const GifByteType * CodeBlock) {

    GifByteType Buf;
    GifFilePrivateType *Private = (GifFilePrivateType *)GifFile->Private;

    if (CodeBlock != NULL) {
        if (WRITE(GifFile, CodeBlock, CodeBlock[0] + 1)
               != (unsigned)(CodeBlock[0] + 1)) {
            _GifError = E_GIF_ERR_WRITE_FAILED;
            return GIF_ERROR;
        }
    } else {
        Buf = 0;
        if (WRITE(GifFile, &Buf, 1) != 1) {
            _GifError = E_GIF_ERR_WRITE_FAILED;
            return GIF_ERROR;
        }
        Private->PixelCount = 0;    /* And local info. indicate image read. */
    }

    return GIF_OK;
}

/******************************************************************************
 * This routine should be called last, to close GIF file.
 *****************************************************************************/
int
EGifCloseFile(GifFileType * GifFile) {

    GifByteType Buf;
    GifFilePrivateType *Private;
    FILE *File;

    if (GifFile == NULL)
        return GIF_ERROR;

    Private = (GifFilePrivateType *) GifFile->Private;
    if (!IS_WRITEABLE(Private)) {
        /* This file was NOT open for writing: */
        _GifError = E_GIF_ERR_NOT_WRITEABLE;
        return GIF_ERROR;
    }

    File = Private->File;

    Buf = ';';
    WRITE(GifFile, &Buf, 1);

    if (GifFile->Image.ColorMap) {
        FreeMapObject(GifFile->Image.ColorMap);
        GifFile->Image.ColorMap = NULL;
    }
    if (GifFile->SColorMap) {
        FreeMapObject(GifFile->SColorMap);
        GifFile->SColorMap = NULL;
    }
    if (Private) {
        if (Private->CodeTable) {
          free(Private->CodeTable);
        }
        free((char *) Private);
    }
    free(GifFile);

    if (File && fclose(File) != 0) {
        _GifError = E_GIF_ERR_CLOSE_FAILED;
        return GIF_ERROR;
    }
    return GIF_OK;
}

/******************************************************************************
 * Put 2 bytes (word) into the given file:
 *****************************************************************************/
static int
EGifPutWord(int Word,
            GifFileType * GifFile) {

    unsigned char c[2];

    c[0] = Word & 0xff;
    c[1] = (Word >> 8) & 0xff;
#ifndef DEBUG_NO_PREFIX
    if (WRITE(GifFile, c, 2) == 2)
        return GIF_OK;
    else
        return GIF_ERROR;
#else
    return GIF_OK;
#endif /* DEBUG_NO_PREFIX */
}

/******************************************************************************
 * Setup the LZ compression for this image:
 *****************************************************************************/
static int
EGifSetupCompress(GifFileType * GifFile,
                  GifCodeStruct * g, GifDiffStruct * gd // schnaader
                 ) {

    int BitsPerPixel;
    GifByteType Buf;
    GifFilePrivateType *Private = (GifFilePrivateType *) GifFile->Private;
    #ifdef GIF_DEBUG
    int CrntCodeOrig; // schnaader
    #endif

    /* Test and see what color map to use, and from it # bits per pixel: */
    if (GifFile->Image.ColorMap)
        BitsPerPixel = GifFile->Image.ColorMap->BitsPerPixel;
    else if (GifFile->SColorMap)
        BitsPerPixel = GifFile->SColorMap->BitsPerPixel;
    else {
        _GifError = E_GIF_ERR_NO_COLOR_MAP;
        return GIF_ERROR;
    }

    Buf = BitsPerPixel = (BitsPerPixel < 2 ? 2 : BitsPerPixel);
    WRITE(GifFile, &Buf, 1);    /* Write the Code size to file. */

    Private->Buf[0] = 0;    /* Nothing was output yet. */
    Private->BitsPerPixel = BitsPerPixel;
    Private->ClearCode = (1 << BitsPerPixel);
    Private->EOFCode = Private->ClearCode + 1;
    Private->RunningCode = Private->EOFCode + 1;
    Private->RunningBits = BitsPerPixel + 1;    /* Number of bits per code. */
    Private->MaxCode1 = 1 << Private->RunningBits;    /* Max. code + 1. */
    Private->CrntCode = FIRST_CODE;    /* Signal that this is first one! */
    Private->CrntShiftState = 0;    /* No information in CrntShiftDWord. */
    Private->CrntShiftDWord = 0;

   /* Clear code table and send Clear to make sure the decoder do the same. */
    ClearCodeTable(Private->CodeTable);

    // schnaader
    if (g != NULL) {
      #ifdef GIF_DEBUG
      CrntCodeOrig = GifCodeGet(g);
      if (CrntCodeOrig != Private->ClearCode) {
         printf("Diff_5 at code %i - would be %i, but original was %i\n", g->GIFCodeIndexGet-1, Private->ClearCode, CrntCodeOrig);
      }
      #else
      GifCodeGet(g);
      #endif
    } else {
      gd->GIFCodeCount++;
    }

    if (EGifCompressOutput(GifFile, Private->ClearCode) == GIF_ERROR) {
        _GifError = E_GIF_ERR_DISK_IS_FULL;
        return GIF_ERROR;
    }
    return GIF_OK;
}

/******************************************************************************
 * The LZ compression routine:
 * This version compresses the given buffer Line of length LineLen.
 * This routine can be called a few times (one per scan line, for example), in
 * order to complete the whole image.
******************************************************************************/
static int
EGifCompressLine(GifFileType * GifFile,
                 GifPixelType * Line,
                 GifCodeStruct * g,  // schnaader
                 GifDiffStruct * gd, // schnaader
                 int LineLen) {

    int i = 0, CrntCode, NewCode;
    int CrntCodeOrig;
    int dont_clear;
    int CrntCodeIsNext;
    unsigned long NewKey;
    GifPixelType Pixel;
    int *CodeTable;
    GifFilePrivateType *Private = (GifFilePrivateType *) GifFile->Private;

    CodeTable = Private->CodeTable;

    if (Private->CrntCode == FIRST_CODE)    /* Its first time! */
        CrntCode = Line[i++];
    else
        CrntCode = Private->CrntCode;    /* Get last code in compression. */

    while (i < LineLen) {   /* Decode LineLen items. */
        // schnaader
        // If next original code should be a clear code, adjust the output
        if (g != NULL) {
          if (GifCodePeek(g) == Private->ClearCode) {
            #ifdef GIF_DEBUG
            printf("Diff 0 at code %i\n", g->GIFCodeIndexGet);
            #endif
            GifDiffStore(gd, 0, g->GIFCodeIndexGet, -1);
            GifCodeGet(g);
            if (EGifCompressOutput(GifFile, Private->ClearCode)
                    == GIF_ERROR) {
                _GifError = E_GIF_ERR_DISK_IS_FULL;
                return GIF_ERROR;
            }
            Private->RunningCode = Private->EOFCode + 1;
            Private->RunningBits = Private->BitsPerPixel + 1;
            Private->MaxCode1 = 1 << Private->RunningBits;
            ClearCodeTable(CodeTable);
          }
        } else { // If next diff code is a clear code, adjust the output
          if ((GifDiffPeekType(gd) == 0) && GifDiffIsPos(gd)) {
            #ifdef GIF_DEBUG
            printf("Adapting diff 0 at code %i\n", gd->GIFCodeCount);
            #endif
            GifDiffNext(gd);
            gd->GIFCodeCount++;
            if (EGifCompressOutput(GifFile, Private->ClearCode)
                    == GIF_ERROR) {
                _GifError = E_GIF_ERR_DISK_IS_FULL;
                return GIF_ERROR;
            }
            Private->RunningCode = Private->EOFCode + 1;
            Private->RunningBits = Private->BitsPerPixel + 1;
            Private->MaxCode1 = 1 << Private->RunningBits;
            ClearCodeTable(CodeTable);
          }
        }

        Pixel = Line[i++];  /* Get next pixel from stream. */
        /* Form a new unique key to search code table for the code combines 
         * CrntCode as Prefix string with Pixel as postfix char.
         */
        NewKey = (((UINT32) CrntCode) << 8) + Pixel;
        CrntCodeIsNext = 0;
        if ((g != NULL) && (GifCodePeek(g) == CrntCode)) CrntCodeIsNext = 1;
        if ((g == NULL) && (GifDiffPeekType(gd) == 2) && GifDiffIsPos(gd) && (GifDiffData(gd) == CrntCode)) CrntCodeIsNext = 1;

        if (((NewCode = CodeTable[NewKey]) > -1)
              && (!CrntCodeIsNext)) { // schnaader
            /* This Key is already there, or the string is old one, so
             * simple take new code as our CrntCode:
             */
            CrntCode = NewCode;
        } else {
            // schnaader
            if (g != NULL) {
              if ((NewCode > -1) && CrntCodeIsNext) {
                #ifdef GIF_DEBUG
                printf("Storing diff 2 (CrntCode = %i) at code %i\n", CrntCode, g->GIFCodeIndexGet);
                #endif
                GifDiffStore(gd, 2, g->GIFCodeIndexGet, CrntCode);
                GifCodeGet(g);
              } else {
                CrntCodeOrig = GifCodeGet(g);
                #ifdef GIF_DEBUG
                if (CrntCodeOrig != CrntCode) {
                   printf("Diff 3 at code %i - would be %i, but original was %i - NewCode = %i, RunningCode = %i\n", g->GIFCodeIndexGet-1, CrntCode, CrntCodeOrig, NewCode, Private->RunningCode);
                }
                #endif
              }
            } else {
              if ((NewCode > -1) && CrntCodeIsNext) {
                #ifdef GIF_DEBUG
                printf("Adapting diff 2 at code %i\n", gd->GIFCodeCount);
                #endif
                GifDiffNext(gd);
              }
              gd->GIFCodeCount++;
            }

            /* Put it in code table, output the prefix code, and make our
             * CrntCode equal to Pixel.
             */

            if (EGifCompressOutput(GifFile, CrntCode) == GIF_ERROR) {
                _GifError = E_GIF_ERR_DISK_IS_FULL;
                return GIF_ERROR;
            }
            CrntCode = Pixel;

            /* If however the CodeTable is full, we send a clear first and
             * Clear the code table.
             */
            if (Private->RunningCode >= LZ_MAX_CODE) {
                dont_clear = 0;
                // schnaader
                if (g != NULL) {
                  CrntCodeOrig = GifCodePeek(g);
                  dont_clear = (CrntCodeOrig != Private->ClearCode);
                  if (dont_clear) {
                    #ifdef GIF_DEBUG
                    printf("Diff 1 at code %i - would be ClearCode (%i), but original was %i\n", g->GIFCodeIndexGet-1, Private->ClearCode, CrntCodeOrig);
                    #endif
                    GifDiffStore(gd, 1, g->GIFCodeIndexGet, -1);            
                  } else {
                    GifCodeGet(g);
                  }
                } else {
                  if ((GifDiffPeekType(gd) == 1) && GifDiffIsPos(gd)) {
                    GifDiffNext(gd);
                    dont_clear = 1;
                  } else {
                    gd->GIFCodeCount++;
                  }
                }

                if (!dont_clear) {
                  /* Time to do some clearance: */
                  if (EGifCompressOutput(GifFile, Private->ClearCode)
                          == GIF_ERROR) {
                      _GifError = E_GIF_ERR_DISK_IS_FULL;
                      return GIF_ERROR;
                  }
                  Private->RunningCode = Private->EOFCode + 1;
                  Private->RunningBits = Private->BitsPerPixel + 1;
                  Private->MaxCode1 = 1 << Private->RunningBits;
                  ClearCodeTable(CodeTable);
                } else {
                  CodeTable[NewKey] = Private->RunningCode;
                }
            } else {
                /* Put this unique key with its relative Code in code table: */
                CodeTable[NewKey] = Private->RunningCode++;
            }
        }

    }

    /* Preserve the current state of the compression algorithm: */
    Private->CrntCode = CrntCode;

    if (Private->PixelCount == 0) {
        // schnaader
        if (g != NULL) {
          CrntCodeOrig = GifCodeGet(g);
          #ifdef GIF_DEBUG
          if (CrntCodeOrig != CrntCode) {
            printf("Diff 4 at code %i - would be %i, but original was %i\n", g->GIFCodeIndexGet-1, CrntCode, CrntCodeOrig);
          }
          #endif
        } else {
          gd->GIFCodeCount++;
        }

        /* We are done - output last Code and flush output buffers: */

        if (EGifCompressOutput(GifFile, CrntCode) == GIF_ERROR) {
            _GifError = E_GIF_ERR_DISK_IS_FULL;
            return GIF_ERROR;
        }
        if (EGifCompressOutput(GifFile, Private->EOFCode) == GIF_ERROR) {
            _GifError = E_GIF_ERR_DISK_IS_FULL;
            return GIF_ERROR;
        }
        if (EGifCompressOutput(GifFile, FLUSH_OUTPUT) == GIF_ERROR) {
            _GifError = E_GIF_ERR_DISK_IS_FULL;
            return GIF_ERROR;
        }
    }

    return GIF_OK;
}

/******************************************************************************
 * The LZ compression output routine:
 * This routine is responsible for the compression of the bit stream into
 * 8 bits (bytes) packets.
 * Returns GIF_OK if written succesfully.
 *****************************************************************************/
static int
EGifCompressOutput(GifFileType * GifFile,
                   int Code) {

    GifFilePrivateType *Private = (GifFilePrivateType *) GifFile->Private;
    int retval = GIF_OK;

    if (Code == FLUSH_OUTPUT) {
        while (Private->CrntShiftState > 0) {
            /* Get Rid of what is left in DWord, and flush it. */
            if (EGifBufferedOutput(GifFile, Private->Buf,
                                 Private->CrntShiftDWord & 0xff) == GIF_ERROR)
                retval = GIF_ERROR;
            Private->CrntShiftDWord >>= 8;
            Private->CrntShiftState -= 8;
        }
        Private->CrntShiftState = 0;    /* For next time. */
        if (EGifBufferedOutput(GifFile, Private->Buf,
                               FLUSH_OUTPUT) == GIF_ERROR)
            retval = GIF_ERROR;
    } else {
        Private->CrntShiftDWord |= ((long)Code) << Private->CrntShiftState;
        Private->CrntShiftState += Private->RunningBits;
        while (Private->CrntShiftState >= 8) {
            /* Dump out full bytes: */
            if (EGifBufferedOutput(GifFile, Private->Buf,
                                 Private->CrntShiftDWord & 0xff) == GIF_ERROR)
                retval = GIF_ERROR;
            Private->CrntShiftDWord >>= 8;
            Private->CrntShiftState -= 8;
        }
    }

    /* If code cannt fit into RunningBits bits, must raise its size. Note */
    /* however that codes above 4095 are used for special signaling.      */
    if (Private->RunningCode >= Private->MaxCode1 && Code <= 4095) {
       Private->MaxCode1 = 1 << ++Private->RunningBits;
    }

    return retval;
}

/******************************************************************************
 * This routines buffers the given characters until 255 characters are ready
 * to be output. If Code is equal to -1 the buffer is flushed (EOF).
 * The buffer is Dumped with first byte as its size, as GIF format requires.
 * Returns GIF_OK if written succesfully.
 *****************************************************************************/
static int
EGifBufferedOutput(GifFileType * GifFile,
                   GifByteType * Buf,
                   int c) {

    if (c == FLUSH_OUTPUT) {
        /* Flush everything out. */
        if (Buf[0] != 0
            && WRITE(GifFile, Buf, Buf[0] + 1) != (unsigned)(Buf[0] + 1)) {
            _GifError = E_GIF_ERR_WRITE_FAILED;
            return GIF_ERROR;
        }
        /* Mark end of compressed data, by an empty block (see GIF doc): */
        Buf[0] = 0;
        if (WRITE(GifFile, Buf, 1) != 1) {
            _GifError = E_GIF_ERR_WRITE_FAILED;
            return GIF_ERROR;
        }
    } else {
        //if (Buf[0] == 255) {
        if (Buf[0] == GifFile->BlockSize) {
            /* Dump out this buffer - it is full: */
            if (WRITE(GifFile, Buf, Buf[0] + 1) != (unsigned)(Buf[0] + 1)) {
                _GifError = E_GIF_ERR_WRITE_FAILED;
                return GIF_ERROR;
            }
            Buf[0] = 0;
        }
        Buf[++Buf[0]] = c;
    }

    return GIF_OK;
}
